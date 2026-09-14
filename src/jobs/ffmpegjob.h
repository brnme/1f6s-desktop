// ffmpegjob.h — 单个 ffmpeg 作业执行器(QObject,信号驱动,UI 无关)。
//
// 输入是 core::encode::buildCommands 产出的命令列表(1 条单遍 / 2 条两遍)。
// 与网站 worker.py 的对齐点(刻意逐条一致):
//   - 每条命令执行前在最后一个 token 前插入 -progress pipe:1 -nostats
//     (worker.py:238,即插在输出文件参数之前,token 顺序不重排);
//   - 进度解析 out_time_us / out_time_ms(实为微秒)/ out_time= 时钟文本
//     (worker.py:52-68);
//   - 百分比 = min(base + int(out/dur × (100-base)), 99),dur = max(时长,1);
//     单遍 base=0;两遍 pass1 不发百分比、pass2 base=50(worker.py:250-258、320);
//   - 失败时错误信息带 stderr 末 800 字符(worker.py:262-263)。
// 有意偏离(AGENTS.md 记录):两遍 pass1 以 -f null - 收尾,progress token
// 插入规则不变。
#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>

#include <optional>

namespace one6s::jobs {

// 从 -progress 行解析已完成输出时长(秒);非进度行/无法解析 → nullopt。
// 对齐网站 worker.py:52-68:out_time_us 与 out_time_ms 实为微秒(ffmpeg
// 历史命名如此),统一 ÷1e6;out_time= 是 HH:MM:SS.xxx 文本。
std::optional<double> parseOutTimeS(const QString& line);

// 两遍作业的 passlogfile 临时目录:构造即创建,析构时随 QTemporaryDir
// 整目录清理(作业结束即释放)。logfile() 的返回值直接传给
// encode::buildCommands 的 passlogfile 形参。
class PasslogDir {
public:
    PasslogDir() = default;
    // 不可拷贝;移动语义交给 unique_ptr 包装。
    PasslogDir(const PasslogDir&) = delete;
    PasslogDir& operator=(const PasslogDir&) = delete;

    bool isValid() const { return dir_.isValid(); }
    // <tmpdir>/pass —— ffmpeg 实际落地 <tmpdir>/pass-0.log(-.mbtree)。
    QString logfile() const { return dir_.filePath(QStringLiteral("pass")); }

private:
    QTemporaryDir dir_;
};

struct FfmpegJobConfig {
    QString ffmpegPath;             // 引擎可执行文件(首 token 的替代者)
    QList<QStringList> commands;    // buildCommands 产出,1 或 2 条;首 token 恒为 "ffmpeg"
    double durationS = 0.0;         // 输入总时长(秒);≤0 时进度恒 0
    bool twopass = false;           // 两遍作业:pass1 不发百分比,pass2 从 50 起算
    QString outputPath;             // 最终产物;取消时清理该部分文件
    bool lowerPriority = true;      // 启动后 POSIX nice 10,让出前台
};

class FfmpegJob : public QObject {
    Q_OBJECT
public:
    explicit FfmpegJob(FfmpegJobConfig config, QObject* parent = nullptr);
    ~FfmpegJob() override;

    // 状态:running / done / cancelled / failed;未 start 前 "idle"。
    QString state() const { return state_; }
    bool isRunning() const { return state_ == QStringLiteral("running"); }

public slots:
    // 逐条执行 commands_;终态只发一次 finished。重复调用无效。
    void start();
    // 幂等;进程未起时先记标志,started 后立刻 kill。终态后调用无效。
    void cancel();

signals:
    // 单调递增 0..99(成功收尾补发一次 100);两遍作业 pass1 期间不发。
    void progress(int percent);
    // 两遍作业在每遍开始时发:"pass1" / "pass2"。
    void stageChanged(const QString& stage);
    // 终态,state ∈ {done, cancelled, failed};failed 时 error 带 stderr 末 800 字符。
    void finished(const QString& state, const QString& error);

private:
    void runNextCommand();
    void onStarted();
    void onStdout();
    void onStderr();
    void onFinished(int exit_code, QProcess::ExitStatus status);
    void settle(const QString& state, const QString& error);

    FfmpegJobConfig config_;
    QProcess proc_;
    QString state_ = QStringLiteral("idle");
    QString error_;
    QByteArray errTail_;        // 保留末 16KB,足够覆盖 ffmpeg 报错尾部
    int cmdIndex_ = 0;          // 当前执行到第几条命令
    bool cancelRequested_ = false;
    bool settled_ = false;

    static constexpr int kNiceValue = 10;         // 低优先级
    static constexpr qint64 kErrTailCap = 16384;  // stderr 缓冲上限
};

}  // namespace one6s::jobs
