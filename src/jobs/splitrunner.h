// splitrunner.h — 分割执行器(QObject,信号驱动,UI 无关)。
//
// 流程(照 M3 计划书语义):
//   plan(用调用方给定的 probe 数据与预检上限调 core::planSplit)
//   → core::segmentCommand 产出的命令交 FfmpegJob 同款 QProcess 编排
//     (流复制 -c copy;编排层只插 -progress 三 token、替换首 token,红线一致)
//   → 逐段 ffprobe 后验(实测体积+时长)
//   → 超限段递归再分割(T = T × (limit/实际体积) × 0.9,最多 2 层,仍超限→警告)
//   → 全部完成后按最终顺序 partName 改名 <stem>.partNNofMM.<ext>
// 分割过程文件先落在输出目录下的临时子目录(pattern seg%05d.<ext>),改名后清理。
//
// 进度语义(简单百分比):任意 ffmpeg 遍映射到 0..89,后验/改名按已定段时长
// 占比推进 90..99,完成 100。单调不减。
//
// 关键帧稀疏警告:最终段数 < 计划段数,或任一段时长 > 1.5×计划段长 →
// "源视频关键帧间隔过大,分段不均"(segment -segment_time 只在关键帧切)。
//
// 点数预算(语义对齐网站 tiers.json):每段 max(ceil(GB), ceil(小时), 1),
// GB = 段体积/1073741824,小时 = 段时长/3600,向上取整。
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

#include <limits>
#include <memory>
#include <string>

#include <QSet>

#include "core/splitter.h"
#include "jobs/engine.h"
#include "jobs/ffmpegjob.h"

namespace one6s::jobs {

// 单段点数:max(ceil(GB), ceil(小时), 1)。
int segmentPoints(double size_bytes, double duration_s);

struct SplitParams {
    QString inputPath;
    QString outputDir;      // 分段最终落盘目录(空 = 与源同目录)
    // —— 调用方先 probe 好的输入数据(预览与执行共用同一份,保证所见即所跑)——
    double durationS = 0.0;
    double sizeBytes = 0.0;
    int width = 0;
    int height = 0;
    std::string codec = "unknown";   // 预检因子查表用(h264/hevc/other)
    // —— 规划参数 ——
    double tierMaxMb = 2048.0;       // 档位体积上限(free 2048 / pro 5120 / org 8192 / 自定义)
    double precheckMaxS = std::numeric_limits<double>::infinity();  // 预检时长上限(秒;inf = 不设限)
    double minSegmentS = 60.0;       // 段长下限(短输入/测试可调小)
    // 显式计划(UI 预览确认后下发;>0 时跳过 planSplit 直接采用,保证所见即所跑):
    double segmentLenS = 0.0;        // 计划段长(秒)
    int plannedParts = 0;            // 计划段数(0 = 按 ceil(时长/段长) 推算)
};

struct SplitSegment {
    QString path;          // 最终改名后的绝对路径
    double sizeBytes = 0;
    double durationS = 0;
    int points = 0;
    bool overLimit = false;  // 递归到底仍超档位上限 → true(警告)
};

class SplitRunner : public QObject {
    Q_OBJECT
public:
    SplitRunner(SplitParams params, EnginePaths engines, QObject* parent = nullptr);
    ~SplitRunner() override;

    // 状态:idle / running / done / cancelled / failed(与 FfmpegJob 同名语义)。
    QString state() const { return state_; }

    const QVector<SplitSegment>& segments() const { return segments_; }
    const QStringList& warnings() const { return warnings_; }
    int totalPoints() const;
    // 稳定后有效:每段时长上限(预检值)与计划段长,UI 结果页展示用。
    double precheckMaxS() const { return precheckMaxS_; }
    double plannedSegmentS() const { return plannedT_; }
    int plannedParts() const { return plannedParts_; }

public slots:
    void start();
    // 幂等;任何阶段(ffmpeg 中/后验中/改名前)都落 cancelled 并清理临时目录。
    void cancel();

signals:
    void progress(int percent);
    // 终态,state ∈ {done, cancelled, failed};failed 时 error 可读。
    void finished(const QString& state, const QString& error);

private:
    struct PassContext {
        QString src;          // 被切分的输入(顶层 = 源;递归 = 超限段)
        double t = 0;         // 本遍 -segment_time
        double srcDur = 0;    // src 时长(进度映射分母)
        int depth = 0;        // 0 = 顶层;递归上限 2
        QString subdir;       // 本遍输出子目录(临时目录下)
        QStringList pending;  // 待后验的分段文件(绝对路径,文件名序)
        QVector<double> sizes;      // 与 pending 对齐的实测体积
        QSet<int> overIdx;          // core::overLimit 结果(超限段下标)
        int verifyIdx = 0;
        QVector<SplitSegment> collected;  // 本遍已定段的产物(递归遍的会拼接回父遍)
    };

    void beginPass(PassContext ctx);
    void onPassFinished(const QString& state, const QString& error);
    void stepVerify();
    void passDone();          // 当前遍后验完毕:递归遍拼回父遍,顶层 → finalize
    void finalize();
    void settle(const QString& state, const QString& error);
    // deleteLater + release:禁止在作业自己的 finished 槽里 delete 发送者。
    void discardJob();
    void emitProgress(int percent);   // 单调不减
    bool preparePlan(QString& error); // 校验 + planSplit + 临时目录;失败填 error

    SplitParams params_;
    EnginePaths engines_;
    QString state_ = QStringLiteral("idle");
    bool cancelRequested_ = false;
    bool settled_ = false;

    QTemporaryDir tmp_;       // 输出目录下的工作目录;析构即清理(cancel/failed 兜底)
    std::unique_ptr<FfmpegJob> job_;

    // 计划(preparePlan 填,稳定后可读)。
    double precheckMaxS_ = 0;
    double plannedT_ = 0;
    int plannedParts_ = 0;
    double limitBytes_ = 0;   // 后验用体积上限 = tierMaxMb × 1048576(严格大于算超)
    std::string ext_;         // 分段扩展名(containerFor 结果)
    QString stem_;

    QVector<PassContext> stack_;  // 栈顶 = 当前遍
    QVector<SplitSegment> segments_;  // 最终产物(改名前 path 为临时位置,改名后更新)
    QStringList warnings_;
    double finalizedDur_ = 0;     // 已定段时长合计(进度 90..99 用)
    double totalDur_ = 1.0;
    int lastEmitted_ = -1;

    static constexpr int kMaxRecursionDepth = 2;
    static constexpr int kProgressFfmpegCap = 89;  // ffmpeg 遍的全局进度上限
};

}  // namespace one6s::jobs
