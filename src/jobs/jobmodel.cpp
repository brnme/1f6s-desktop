// jobmodel.cpp — 任务队列实现(FIFO 串行;compress 与 split 两类任务)。
#include "jobs/jobmodel.h"

#include <QFileInfo>

#include "core/encode.h"

namespace one6s::jobs {

QString jobStateName(JobState state) {
    switch (state) {
        case JobState::Queued: return QStringLiteral("queued");
        case JobState::Running: return QStringLiteral("running");
        case JobState::Done: return QStringLiteral("done");
        case JobState::Cancelled: return QStringLiteral("cancelled");
        case JobState::Failed: return QStringLiteral("failed");
    }
    return QStringLiteral("unknown");
}

JobModel::JobModel(QObject* parent) : QObject(parent) {}

quint64 JobModel::enqueue(const QString& input_path, const QString& output_path,
                          const QString& level_id, const nlohmann::json& overrides,
                          double duration_s, bool has_audio) {
    JobRecord rec;
    rec.id = nextId_++;
    rec.kind = JobKind::Compress;
    rec.inputPath = input_path;
    rec.outputPath = output_path;
    rec.levelId = level_id;
    rec.overrides = overrides;
    rec.durationS = duration_s;
    rec.hasAudio = has_audio;

    // 预检 + 引擎检查:能提前失败的都提前失败,给出可读文案。
    try {
        rec.eff = resolve(spec_, level_id.toStdString(), overrides);
    } catch (const std::exception& e) {
        return failImmediate(std::move(rec), QString::fromUtf8(e.what()));
    }
    if (!engines_.ok())
        return failImmediate(std::move(rec),
                             QStringLiteral("未找到 ffmpeg/ffprobe,请重新安装本程序。"));

    const quint64 id = rec.id;
    records_.emplace(id, std::move(rec));
    emit jobEnqueued(id);
    emit taskAdded(id);
    emit taskUpdated(id);
    queue_.push_back(id);
    startNext();
    return id;
}

quint64 JobModel::enqueueSplit(const SplitParams& params) {
    JobRecord rec;
    rec.id = nextId_++;
    rec.kind = JobKind::Split;
    rec.inputPath = params.inputPath;
    rec.outputPath = params.outputDir.isEmpty()
                         ? QFileInfo(params.inputPath).absolutePath()
                         : params.outputDir;
    rec.durationS = params.durationS;
    rec.split = params;
    rec.levelId = QStringLiteral("split");

    if (!engines_.ok())
        return failImmediate(std::move(rec),
                             QStringLiteral("未找到 ffmpeg/ffprobe,请重新安装本程序。"));

    const quint64 id = rec.id;
    records_.emplace(id, std::move(rec));
    emit jobEnqueued(id);
    emit taskAdded(id);
    emit taskUpdated(id);
    queue_.push_back(id);
    startNext();
    return id;
}

quint64 JobModel::failImmediate(JobRecord&& rec, const QString& error) {
    rec.state = JobState::Failed;
    rec.error = error;
    const quint64 id = rec.id;
    records_.emplace(id, std::move(rec));
    emit jobEnqueued(id);
    emit taskAdded(id);
    emit taskUpdated(id);
    emit jobFinished(id, JobState::Failed, records_.at(id).error);
    return id;
}

bool JobModel::removeQueued(quint64 id) {
    auto it = std::find(queue_.begin(), queue_.end(), id);
    if (it == queue_.end()) return false;
    queue_.erase(it);
    auto& rec = mutableRecord(id);
    rec.state = JobState::Cancelled;
    emit taskRemoved(id);
    emit taskUpdated(id);
    emit jobFinished(id, JobState::Cancelled, {});
    return true;
}

void JobModel::cancelTask(quint64 id) {
    if (id == activeId_) {
        cancelCurrent();
        return;
    }
    removeQueued(id);
}

void JobModel::cancelCurrent() {
    if (active_) active_->cancel();
    if (activeSplit_) activeSplit_->cancel();
}

const JobRecord* JobModel::record(quint64 id) const {
    const auto it = records_.find(id);
    return it == records_.end() ? nullptr : &it->second;
}

QList<JobRecord> JobModel::records() const {
    QList<JobRecord> out;
    out.reserve(static_cast<qsizetype>(records_.size()));
    for (const auto& [id, rec] : records_) out.append(rec);
    return out;
}

void JobModel::startNext() {
    if (activeId_ != 0 || queue_.empty()) return;
    if (!engines_.ok()) {  // 队列里残留的作业一并落失败
        while (!queue_.empty()) {
            const quint64 id = queue_.front();
            queue_.pop_front();
            auto& rec = mutableRecord(id);
            rec.state = JobState::Failed;
            rec.error = QStringLiteral("未找到 ffmpeg/ffprobe,请重新安装本程序。");
            emit taskUpdated(id);
            emit jobUpdated(id);
            emit jobFinished(id, JobState::Failed, rec.error);
        }
        return;
    }

    const quint64 id = queue_.front();
    queue_.pop_front();
    JobRecord& rec = mutableRecord(id);

    if (rec.kind == JobKind::Split) {
        auto runner = std::make_unique<SplitRunner>(rec.split, engines_, this);
        connect(runner.get(), &SplitRunner::progress, this,
                [this, id](int percent) {
                    if (activeId_ != id) return;
                    auto& r = mutableRecord(id);
                    if (percent > r.progress) r.progress = percent;
                    emit jobProgress(id, r.progress);
                    emit taskUpdated(id);
                    emit jobUpdated(id);
                });
        connect(runner.get(), &SplitRunner::finished, this,
                [this, id](const QString& state, const QString& error) {
                    if (activeId_ != id) return;
                    JobState st = JobState::Failed;
                    if (state == QLatin1String("done")) st = JobState::Done;
                    else if (state == QLatin1String("cancelled"))
                        st = JobState::Cancelled;
                    // 成功时把分割结果拷进记录,UI 结果页直接读模型。
                    if (st == JobState::Done && activeSplit_) {
                        auto& r = mutableRecord(id);
                        r.splitSegments = activeSplit_->segments();
                        r.splitWarnings = activeSplit_->warnings();
                        r.splitPoints = activeSplit_->totalPoints();
                    }
                    finishActive(st, error);
                });
        activeId_ = id;
        rec.state = JobState::Running;
        rec.progress = 0;
        emit taskUpdated(id);
        emit jobUpdated(id);
        activeSplit_ = std::move(runner);
        activeSplit_->start();
        return;
    }

    // —— compress 任务 ——
    // 两遍作业:passlogfile 放进作业专属临时目录,作业结束(PasslogDir 析构)清理。
    QString passlogfile;
    if (rec.eff.twopass) {
        passlog_ = std::make_unique<PasslogDir>();
        passlogfile = passlog_->logfile();
    }
    const auto token_lists = encode::buildCommands(
        spec_, rec.inputPath.toStdString(), rec.outputPath.toStdString(),
        rec.eff, rec.durationS, rec.hasAudio, passlogfile.toStdString());
    FfmpegJobConfig cfg;
    cfg.ffmpegPath = engines_.ffmpeg;
    for (const auto& tokens : token_lists) {
        QStringList l;
        l.reserve(static_cast<qsizetype>(tokens.size()));
        for (const auto& t : tokens) l.append(QString::fromStdString(t));
        cfg.commands.append(l);
    }
    cfg.durationS = rec.durationS;
    cfg.twopass = rec.eff.twopass;
    cfg.outputPath = rec.outputPath;

    activeId_ = id;
    rec.state = JobState::Running;
    rec.progress = 0;
    emit taskUpdated(id);
    emit jobUpdated(id);

    active_ = std::make_unique<FfmpegJob>(std::move(cfg), this);
    connect(active_.get(), &FfmpegJob::progress, this,
            [this, id](int percent) {
                if (activeId_ != id) return;
                auto& r = mutableRecord(id);
                if (percent > r.progress) r.progress = percent;
                emit jobProgress(id, r.progress);
                emit taskUpdated(id);
                emit jobUpdated(id);
            });
    connect(active_.get(), &FfmpegJob::finished, this,
            [this, id](const QString& state, const QString& error) {
                if (activeId_ != id) return;
                JobState st = JobState::Failed;
                if (state == QLatin1String("done")) st = JobState::Done;
                else if (state == QLatin1String("cancelled")) st = JobState::Cancelled;
                finishActive(st, error);
            });
    active_->start();
}

void JobModel::finishActive(JobState state, const QString& error) {
    const quint64 id = activeId_;
    auto& rec = mutableRecord(id);
    rec.state = state;
    rec.error = error;
    if (state == JobState::Done) rec.progress = 100;
    activeId_ = 0;
    discardActive();     // 执行器延迟销毁(不在其 finished 槽里 delete 发送者)
    passlog_.reset();    // passlogfile 临时目录在此清理
    emit taskUpdated(id);
    emit jobUpdated(id);
    emit jobFinished(id, state, error);
    startNext();
    emit queueAdvanced();
}

void JobModel::discardActive() {
    if (active_) {
        active_->deleteLater();
        active_.release();  // 所有权交还 Qt(deleteLater/父子析构兜底)
    }
    if (activeSplit_) {
        activeSplit_->deleteLater();
        activeSplit_.release();
    }
}

}  // namespace one6s::jobs
