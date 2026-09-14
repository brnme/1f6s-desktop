#include "jobs/ffmpegjob.h"

#include <QFile>

#include <algorithm>

#ifdef Q_OS_UNIX
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace one6s::jobs {

std::optional<double> parseOutTimeS(const QString& line) {
    // 语义对齐 worker.py:52-68:key=val;us/ms 实为微秒。
    const QString trimmed = line.trimmed();
    const int eq = trimmed.indexOf(QLatin1Char('='));
    if (eq <= 0) return std::nullopt;
    const QString key = trimmed.left(eq);
    bool ok = false;
    if (key == QLatin1String("out_time_us") || key == QLatin1String("out_time_ms")) {
        const qint64 us = trimmed.mid(eq + 1).toLongLong(&ok);
        if (!ok) return std::nullopt;
        return static_cast<double>(us) / 1e6;
    }
    if (key == QLatin1String("out_time")) {
        const QStringList parts = trimmed.mid(eq + 1).split(QLatin1Char(':'));
        if (parts.size() != 3) return std::nullopt;
        bool h_ok = false, m_ok = false, s_ok = false;
        const int h = parts[0].toInt(&h_ok);
        const int m = parts[1].toInt(&m_ok);
        const double s = parts[2].toDouble(&s_ok);
        if (!h_ok || !m_ok || !s_ok) return std::nullopt;
        return h * 3600.0 + m * 60.0 + s;
    }
    return std::nullopt;
}

FfmpegJob::FfmpegJob(FfmpegJobConfig config, QObject* parent)
    : QObject(parent), config_(std::move(config)) {
    proc_.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&proc_, &QProcess::started, this, &FfmpegJob::onStarted);
    connect(&proc_, &QProcess::readyReadStandardOutput, this, &FfmpegJob::onStdout);
    connect(&proc_, &QProcess::readyReadStandardError, this, &FfmpegJob::onStderr);
    connect(&proc_, &QProcess::finished, this, &FfmpegJob::onFinished);
    // start 失败(如引擎被删)也要落终态,不能悬死。
    connect(&proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart && !settled_)
            settle(QStringLiteral("failed"),
                   QStringLiteral("无法启动 ffmpeg:%1")
                       .arg(proc_.errorString()));
    });
}

FfmpegJob::~FfmpegJob() {
    if (proc_.state() != QProcess::NotRunning) {
        proc_.kill();
        proc_.waitForFinished(3000);
    }
}

void FfmpegJob::start() {
    if (settled_ || state_ == QStringLiteral("running")) return;
    if (config_.commands.isEmpty()) {
        settle(QStringLiteral("failed"), QStringLiteral("命令列表为空"));
        return;
    }
    state_ = QStringLiteral("running");
    cmdIndex_ = 0;
    runNextCommand();
}

void FfmpegJob::cancel() {
    if (settled_) return;
    cancelRequested_ = true;
    if (state_ == QStringLiteral("running") &&
        proc_.state() != QProcess::NotRunning)
        proc_.kill();          // worker.py 取消路径同为 SIGKILL
    else if (state_ != QStringLiteral("running"))
        settle(QStringLiteral("cancelled"), {});
    // running 且 Starting:started 槽里见到 cancelRequested_ 再 kill。
}

void FfmpegJob::runNextCommand() {
    const QStringList cmd = config_.commands.at(cmdIndex_);
    // worker.py:238:cmd[:-1] + ["-progress","pipe:1","-nostats", cmd[-1]]。
    // 首 token 是字面 "ffmpeg",以定位到的引擎路径作为程序名启动,其余原样。
    QStringList args = cmd.mid(1);
    if (args.isEmpty()) {
        settle(QStringLiteral("failed"), QStringLiteral("命令缺少输出参数"));
        return;
    }
    const int last = args.size() - 1;
    args.insert(last, QStringLiteral("-progress"));
    args.insert(last + 1, QStringLiteral("pipe:1"));
    args.insert(last + 2, QStringLiteral("-nostats"));

    if (config_.twopass)
        emit stageChanged(cmdIndex_ == 0 ? QStringLiteral("pass1")
                                         : QStringLiteral("pass2"));

    errTail_.clear();
    proc_.start(config_.ffmpegPath, args);
}

void FfmpegJob::onStarted() {
#ifdef Q_OS_UNIX
    if (config_.lowerPriority) {
        // 非特权进程只能调低自身优先级,失败可忽略(值超出 RLIMIT_NICE 等)。
        ::setpriority(PRIO_PROCESS, static_cast<id_t>(proc_.processId()),
                      kNiceValue);
    }
#endif
    if (cancelRequested_) proc_.kill();
}

void FfmpegJob::onStdout() {
    while (proc_.canReadLine()) {
        const QString line = QString::fromUtf8(proc_.readLine());
        const std::optional<double> out = parseOutTimeS(line);
        if (!out) continue;

        // worker.py:320 pct = min(base + int(out/dur × (100-base)), 99);
        // dur = max(时长, 1)(worker.py:302)。
        const double dur = config_.durationS > 0.0 ? config_.durationS : 1.0;
        const int base = config_.twopass && cmdIndex_ > 0 ? 50 : 0;
        // pass1(update=False)不刷新百分比(worker.py:252-258)。
        if (config_.twopass && cmdIndex_ == 0) continue;
        int pct = base + static_cast<int>(*out / dur * (100 - base));
        pct = std::min(pct, 99);
        emit progress(pct);
    }
}

void FfmpegJob::onStderr() {
    errTail_ += proc_.readAllStandardError();
    if (errTail_.size() > kErrTailCap)
        errTail_ = errTail_.right(kErrTailCap);
}

void FfmpegJob::onFinished(int exit_code, QProcess::ExitStatus status) {
    if (cancelRequested_) {
        // 取消:清理部分输出文件(passlogfile 由 PasslogDir 随作业析构清理)。
        if (!config_.outputPath.isEmpty()) QFile::remove(config_.outputPath);
        settle(QStringLiteral("cancelled"), {});
        return;
    }
    if (exit_code != 0 || status == QProcess::CrashExit) {
        // worker.py:262-263:错误信息带 stderr 末 800 字符。
        const QString tail = QString::fromUtf8(errTail_.right(800));
        settle(QStringLiteral("failed"),
               QStringLiteral("ffmpeg exit %1: %2").arg(exit_code).arg(tail));
        return;
    }
    if (++cmdIndex_ < config_.commands.size()) {
        runNextCommand();
        return;
    }
    emit progress(100);  // 与网站终态 progress=100 一致
    settle(QStringLiteral("done"), {});
}

void FfmpegJob::settle(const QString& state, const QString& error) {
    if (settled_) return;
    settled_ = true;
    state_ = state;
    error_ = error;
    emit finished(state_, error_);
}

}  // namespace one6s::jobs
