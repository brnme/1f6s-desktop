// splitrunner.cpp — 分割执行器状态机(见 splitrunner.h 顶部说明)。
#include "jobs/splitrunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cmath>

#include "core/probe.h"

namespace one6s::jobs {

namespace {
constexpr double kGiB = 1073741824.0;
constexpr double kMiB = 1048576.0;
}  // namespace

int segmentPoints(double size_bytes, double duration_s) {
    // tiers.json 公式:单产物点数 = max(ceil(文件GB), ceil(时长小时), 1)。
    const int gb = static_cast<int>(std::ceil(size_bytes / kGiB));
    const int hours = static_cast<int>(std::ceil(duration_s / 3600.0));
    return std::max(std::max(gb, hours), 1);
}

int SplitRunner::totalPoints() const {
    int sum = 0;
    for (const SplitSegment& s : segments_) sum += s.points;
    return sum;
}

SplitRunner::SplitRunner(SplitParams params, EnginePaths engines, QObject* parent)
    : QObject(parent), params_(std::move(params)), engines_(std::move(engines)) {}

SplitRunner::~SplitRunner() {
    if (job_) job_->cancel();  // 析构即取消;QTemporaryDir 随成员析构清理工作目录
}

void SplitRunner::start() {
    if (state_ != QStringLiteral("idle")) return;
    if (cancelRequested_) {  // 未起先取消
        settle(QStringLiteral("cancelled"), {});
        return;
    }
    state_ = QStringLiteral("running");
    QString err;
    if (!preparePlan(err)) {
        settle(QStringLiteral("failed"), err);
        return;
    }
    PassContext top;
    top.src = params_.inputPath;
    top.t = plannedT_;
    top.srcDur = params_.durationS;
    top.depth = 0;
    top.subdir = tmp_.filePath(QStringLiteral("p0"));
    beginPass(std::move(top));
}

void SplitRunner::cancel() {
    if (settled_) return;
    cancelRequested_ = true;
    if (job_ && job_->isRunning()) {
        job_->cancel();  // finished 回调里 settle
    } else if (state_ == QStringLiteral("running")) {
        settle(QStringLiteral("cancelled"), {});
    }
}

bool SplitRunner::preparePlan(QString& error) {
    if (!engines_.ok()) {
        error = QStringLiteral("未找到 ffmpeg/ffprobe,请重新安装本程序。");
        return false;
    }
    const QFileInfo in(params_.inputPath);
    if (!in.exists() || !in.isFile()) {
        error = QStringLiteral("输入文件不存在:%1").arg(params_.inputPath);
        return false;
    }
    if (params_.durationS <= 0.0) {
        error = QStringLiteral("无法读取输入时长,无法规划分割。");
        return false;
    }
    const QString out_dir = params_.outputDir.isEmpty()
                                ? in.absolutePath()
                                : params_.outputDir;
    params_.outputDir = out_dir;

    const std::optional<splitter::Container> container =
        splitter::containerFor(in.suffix().toStdString());
    if (!container.has_value()) {
        error = QStringLiteral("不支持的容器格式「%1」,无法流复制分割。")
                    .arg(in.suffix());
        return false;
    }
    ext_ = container->keep_ext;
    stem_ = in.completeBaseName();

    precheckMaxS_ = params_.precheckMaxS;
    if (params_.segmentLenS > 0.0) {
        // 显式计划:直接采用 UI 预览确认的段长/段数(所见即所跑)。
        plannedT_ = params_.segmentLenS;
        plannedParts_ = params_.plannedParts > 0
                            ? params_.plannedParts
                            : static_cast<int>(std::ceil(params_.durationS / plannedT_));
    } else {
        const splitter::SplitPlan plan = splitter::planSplit(
            params_.durationS, params_.sizeBytes, params_.tierMaxMb, precheckMaxS_,
            params_.minSegmentS);
        plannedT_ = plan.segment_len_s;
        plannedParts_ = plan.parts;
    }
    // T=inf(planSplit 退化路径)没有可执行的分段时长:钳为整段一段。
    if (!std::isfinite(plannedT_) || plannedT_ <= 0.0)
        plannedT_ = params_.durationS;
    limitBytes_ = params_.tierMaxMb * kMiB;  // 后验上限:档位体积,严格大于算超
    totalDur_ = std::max(params_.durationS, 1e-9);

    // 工作目录放输出目录下:保证与最终落盘同文件系统,改名不跨盘。
    tmp_.setAutoRemove(true);
    if (!tmp_.isValid()) {
        error = QStringLiteral("无法创建分割临时目录(输出目录不可写?)。");
        return false;
    }
    return true;
}

void SplitRunner::beginPass(PassContext ctx) {
    QDir().mkpath(ctx.subdir);
    // pattern 先用临时名 seg%05d.<ext>;最终名在 finalize 阶段统一改。
    const QString pattern =
        ctx.subdir + QStringLiteral("/seg%05d.%1").arg(QString::fromStdString(ext_));
    const std::vector<std::string> tokens = splitter::segmentCommand(
        ctx.src.toStdString(), pattern.toStdString(),
        splitter::containerFor(ext_)->muxer, ctx.t);

    FfmpegJobConfig cfg;
    cfg.ffmpegPath = engines_.ffmpeg;
    QStringList l;
    l.reserve(static_cast<qsizetype>(tokens.size()));
    for (const std::string& t : tokens) l.append(QString::fromStdString(t));
    cfg.commands.append(l);
    // 红 线:segmentCommand token 原样,FfmpegJob 只插 -progress 三 token、
    // 替换首 token 为引擎路径 —— 即本任务允许的全部编排自由度。
    cfg.durationS = ctx.srcDur > 0 ? ctx.srcDur : params_.durationS;
    cfg.twopass = false;
    // 分段模式无单一产物,outputPath 留空;取消清理由临时目录兜底。
    cfg.lowerPriority = true;

    const int depth = ctx.depth;
    job_ = std::make_unique<FfmpegJob>(std::move(cfg), this);
    connect(job_.get(), &FfmpegJob::progress, this, [this](int pct) {
        // ffmpeg 遍映射到全局 0..kProgressFfmpegCap;后验/改名阶段再推进 90..99。
        emitProgress(std::min(static_cast<int>(pct * 0.9), kProgressFfmpegCap));
    });
    connect(job_.get(), &FfmpegJob::finished, this,
            &SplitRunner::onPassFinished);
    stack_.push_back(std::move(ctx));
    job_->start();
}

void SplitRunner::onPassFinished(const QString& state, const QString& error) {
    // 不在发送者的信号槽内直接 delete:release + deleteLater(见 discardJob)。
    discardJob();
    if (cancelRequested_) {
        settle(QStringLiteral("cancelled"), {});
        return;
    }
    if (state != QStringLiteral("done")) {
        settle(QStringLiteral("failed"),
               error.isEmpty() ? QStringLiteral("ffmpeg 分割失败。") : error);
        return;
    }
    // 收割本遍产物:seg*.ext,文件名序(%05d 零填充 → 字典序 = 数值序)。
    PassContext& ctx = stack_.last();
    const QDir dir(ctx.subdir);
    const QStringList names = dir.entryList(
        QStringList{QStringLiteral("seg*.%1").arg(QString::fromStdString(ext_))},
        QDir::Files, QDir::Name);
    if (names.isEmpty()) {
        settle(QStringLiteral("failed"),
               QStringLiteral("分割未产生任何分段(容器或流异常)。"));
        return;
    }
    for (const QString& name : names)
        ctx.pending.append(dir.filePath(name));

    // 后验用 core::overLimit 先找出超限段下标。
    ctx.sizes.reserve(static_cast<qsizetype>(names.size()));
    for (const QString& f : ctx.pending)
        ctx.sizes.append(static_cast<double>(QFileInfo(f).size()));
    const std::vector<double> raw(ctx.sizes.cbegin(), ctx.sizes.cend());
    for (const int idx : splitter::overLimit(raw, limitBytes_))
        ctx.overIdx.insert(idx);
    stepVerify();
}

void SplitRunner::stepVerify() {
    while (true) {
        if (cancelRequested_) {
            settle(QStringLiteral("cancelled"), {});
            return;
        }
        PassContext& ctx = stack_.last();
        if (ctx.verifyIdx >= ctx.pending.size()) {
            passDone();  // 可能弹栈或已 finalize
            return;
        }
        const QString path = ctx.pending.at(ctx.verifyIdx);
        const double size = ctx.sizes.at(ctx.verifyIdx);

        // 逐段 ffprobe 实测时长。
        double dur = 0;
        try {
            dur = one6s::probe(engines_.ffprobe, path).duration_s;
        } catch (const std::exception& e) {
            settle(QStringLiteral("failed"),
                   QStringLiteral("分段探测失败:%1").arg(QString::fromUtf8(e.what())));
            return;
        }

        const bool over = ctx.overIdx.contains(ctx.verifyIdx);
        if (over && ctx.depth < kMaxRecursionDepth) {
            // 递归再分割:T = T × 超限比 × 0.9,下限钳 1s 防病态。
            const double new_t =
                std::max(ctx.t * (limitBytes_ / std::max(size, 1.0)) * 0.9, 1.0);
            ctx.verifyIdx++;  // 该段被递归遍替换,消费掉
            PassContext child;
            child.src = path;
            child.t = new_t;
            child.srcDur = dur;
            child.depth = ctx.depth + 1;
            child.subdir = ctx.subdir + QStringLiteral("/r%1").arg(child.depth);
            beginPass(std::move(child));
            return;  // 子遍异步,回调后从子遍继续
        }

        SplitSegment seg;
        seg.path = path;
        seg.sizeBytes = size;
        seg.durationS = dur;
        seg.points = segmentPoints(size, dur);
        seg.overLimit = over;  // 递归到底仍超限 → 警告
        ctx.collected.append(seg);
        finalizedDur_ += dur;
        emitProgress(90 + static_cast<int>(9.0 * finalizedDur_ / totalDur_));
        ctx.verifyIdx++;
    }
}

void SplitRunner::passDone() {
    const PassContext ctx = stack_.takeLast();
    if (stack_.isEmpty()) {
        segments_ = ctx.collected;
        finalize();
        return;
    }
    // 递归遍结束:产物按序拼回父遍(替换被再分割的那一段)。
    stack_.last().collected += ctx.collected;
}

void SplitRunner::finalize() {
    // 关键帧稀疏检测:段数少于计划,或任一段时长 > 1.5×计划段长。
    const QString kf_warn = QStringLiteral(
        "源视频关键帧间隔过大,分段不均;分段边界可能偏离预估,建议先重编码"
        "再分割,或接受现有分段。");
    if (segments_.size() < plannedParts_) {
        if (!warnings_.contains(kf_warn)) warnings_.append(kf_warn);
    }
    for (const SplitSegment& s : segments_) {
        if (s.durationS > 1.5 * plannedT_) {
            if (!warnings_.contains(kf_warn)) warnings_.append(kf_warn);
            break;
        }
    }

    // 全部完成后按最终顺序改名 <stem>.partNNofMM.<ext>(覆盖同名旧产物)。
    const int total = static_cast<int>(segments_.size());
    for (int i = 0; i < total; ++i) {
        const std::string name =
            splitter::partName(stem_.toStdString(), i + 1, total, ext_);
        const QString dest = QDir(params_.outputDir).filePath(
            QString::fromStdString(name));
        QFile::remove(dest);  // rename 不会覆盖已存在目标,先移除(与 -y 同语义)
        if (!QFile::rename(segments_[i].path, dest)) {
            settle(QStringLiteral("failed"),
                   QStringLiteral("分段改名失败:%1 → %2")
                       .arg(segments_[i].path, dest));
            return;
        }
        segments_[i].path = dest;
    }
    emitProgress(100);
    settle(QStringLiteral("done"), {});
}

void SplitRunner::emitProgress(int percent) {
    percent = std::clamp(percent, 0, 100);
    if (percent <= lastEmitted_) return;
    lastEmitted_ = percent;
    emit progress(percent);
}

void SplitRunner::settle(const QString& state, const QString& error) {
    if (settled_) return;
    settled_ = true;
    state_ = state;
    discardJob();  // 终态后作业对象延迟销毁(deleteLater;析构 kill 由 Qt 兜底)
    emit finished(state_, error);
}

void SplitRunner::discardJob() {
    if (!job_) return;
    job_->deleteLater();
    // 所有权交还 Qt:deleteLater 销毁,或随本对象(QObject 父子)析构兜底。
    job_.release();
}

}  // namespace one6s::jobs
