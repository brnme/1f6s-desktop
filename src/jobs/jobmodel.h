// jobmodel.h — 任务队列模型(FIFO 串行;UI 无关,可 headless 测试)。
//
// M3 起支持两类任务:compress(单文件压缩,level+overrides)与 split(流复制
// 分割,SplitParams)。同一时刻至多一个任务在跑;一个跑完自动启动下一个。
//
// 队列语义(测试与 UI 共同契约):
//   - queued:排队中;removeQueued(id) 可将其移出队列并标记 cancelled;
//   - running:cancelTask(id) 取消;跑完按状态落 done/failed/cancelled;
//   - 任务记录保留到模型析构(UI 靠 taskRemoved 决定是否从列表撤下卡片)。
//
// 信号契约:UI 列表刷新用 taskAdded/taskRemoved/taskUpdated/queueAdvanced;
// jobEnqueued/jobUpdated/jobProgress/jobFinished 为 M2 兼容信号,语义不变
// (jobUpdated 与 taskUpdated 同步发出)。
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <deque>
#include <map>
#include <memory>

#include <nlohmann/json.hpp>

#include "core/spec.h"
#include "jobs/engine.h"
#include "jobs/ffmpegjob.h"
#include "jobs/splitrunner.h"

namespace one6s::jobs {

enum class JobState { Queued, Running, Done, Cancelled, Failed };
enum class JobKind { Compress, Split };

QString jobStateName(JobState state);  // "queued"/"running"/"done"/"cancelled"/"failed"

// 压缩任务的运行时选项(设置页 M4)。默认值全部等于「忠实网站命令」:
// threadLimit=0 不插 token、presetOverride 空 不改 -preset、lowerPriority=true
// 是 M2 起的既有行为。前两项是用户主动偏离(AGENTS.md 有意偏离节 2/3 条),
// 只影响新入队的任务;黄金向量对拍走 core::encode::buildCommands,不经过这里。
struct RuntimeOptions {
    bool lowerPriority = true;   // 启动后 POSIX nice 10(FfmpegJobConfig 同名项)
    int threadLimit = 0;         // 0=不限;>0 时每条命令最后一个 token 前插 "-threads N"
    QString presetOverride;      // 空=忠实 spec;非空(veryfast/faster/medium)替换 "-preset" 的值
};

struct JobRecord {
    quint64 id = 0;
    JobKind kind = JobKind::Compress;
    QString inputPath;
    QString outputPath;     // compress:最终产物;split:输出目录
    QString levelId;        // compress:如 "L4";与 overrides 合起来即有效参数来源
    nlohmann::json overrides = nlohmann::json::object();
    one6s::Eff eff;         // compress:入队时 resolve 的结果(失败则作业 Failed)
    double durationS = 0.0;
    bool hasAudio = false;
    SplitParams split;      // split:入队参数(kind==Split 时有效)
    JobState state = JobState::Queued;
    int progress = 0;       // 0..100;Running 期间来自执行器的进度信号
    QString error;          // Failed 时的文案(stderr 末 800 字符等)
    // split 结果(完成后有效):
    QVector<SplitSegment> splitSegments;
    QStringList splitWarnings;
    int splitPoints = 0;
};

class JobModel : public QObject {
    Q_OBJECT
public:
    explicit JobModel(QObject* parent = nullptr);

    // Spec 与引擎路径由上层注入(app 启动时定位/加载一次)。
    void setSpec(one6s::Spec spec) { spec_ = std::move(spec); }
    void setEngines(EnginePaths engines) { engines_ = engines; }
    const EnginePaths& engines() const { return engines_; }
    // 运行时选项(设置页可随时更新;对之后启动的任务生效,进行中的不动)。
    void setRuntimeOptions(RuntimeOptions opts) { opts_ = std::move(opts); }
    const RuntimeOptions& runtimeOptions() const { return opts_; }

    // —— 入队 ——
    // compress 任务:duration_s / has_audio 由调用方先 probe 得到。
    // resolve 失败(SpecError,如 L7 无 target_mb)时作业直接落 Failed,
    // 不阻塞后续队列。返回作业 id。
    quint64 enqueue(const QString& input_path, const QString& output_path,
                    const QString& level_id, const nlohmann::json& overrides,
                    double duration_s, bool has_audio);
    // split 任务:plan 数据由调用方先 probe/预览好(SplitParams 全量自带)。
    quint64 enqueueSplit(const SplitParams& params);

    // —— 队列操作 ——
    // 移除排队中的任务:出队 + 标记 cancelled + taskRemoved。非 queued → false。
    bool removeQueued(quint64 id);
    // queued 任务 → 等价 removeQueued;running 任务 → 取消;其余无效。
    void cancelTask(quint64 id);
    // 取消当前正在跑的作业(M2 兼容入口)。
    void cancelCurrent();

    bool idle() const { return activeId_ == 0 && queue_.empty(); }
    // 该类任务是否有排队/运行中的(未终结的)记录 —— 压缩/分割互斥灰化依据;
    // failImmediate 直接落 Failed 的记录不算。
    bool hasOpenTasks(JobKind kind) const;
    const JobRecord* record(quint64 id) const;  // 不存在 → nullptr
    QList<JobRecord> records() const;

signals:
    // M3 UI 列表契约:
    void taskAdded(quint64 id);
    void taskRemoved(quint64 id);   // removeQueued 成功后发出
    void taskUpdated(quint64 id);   // 状态/进度有任何变化
    void queueAdvanced();           // 一个任务终结、队列头部推进后(含队列排空)

    // M2 兼容信号(语义不变):
    void jobEnqueued(quint64 id);
    void jobUpdated(quint64 id);    // 与 taskUpdated 同步发出
    void jobProgress(quint64 id, int percent);
    void jobFinished(quint64 id, JobState state, const QString& error);

private:
    void startNext();
    // 用户主动偏离(AGENTS.md 有意偏离 2/3 条)统一在此落地:对单条命令
    // 替换 "-preset" 取值、在最后一个 token 前插 "-threads N";默认配置下两者皆空转。
    void applyRuntimeOptions(QStringList& cmd) const;
    void finishActive(JobState state, const QString& error);
    // deleteLater + release:禁止在执行器自己的 finished 槽里 delete 发送者。
    void discardActive();
    // 入队即失败(resolve 失败/引擎缺失):记录落 Failed、信号照发、不入队。
    quint64 failImmediate(JobRecord&& rec, const QString& error);
    JobRecord& mutableRecord(quint64 id) { return records_.at(id); }

    one6s::Spec spec_;
    EnginePaths engines_;
    RuntimeOptions opts_;
    std::map<quint64, JobRecord> records_;
    std::deque<quint64> queue_;   // 等待运行的 FIFO(仅存排队中的 id)
    quint64 activeId_ = 0;        // 0 = 无
    quint64 nextId_ = 1;
    std::unique_ptr<FfmpegJob> active_;       // compress 任务的执行器
    std::unique_ptr<SplitRunner> activeSplit_;  // split 任务的执行器
    std::unique_ptr<PasslogDir> passlog_;  // 两遍作业的 passlogfile 目录
};

}  // namespace one6s::jobs
