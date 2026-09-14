#include "jobs/jobmodel.h"

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
        rec.state = JobState::Failed;
        rec.error = QString::fromUtf8(e.what());
        const quint64 id = rec.id;
        records_.emplace(id, std::move(rec));
        emit jobEnqueued(id);
        emit jobUpdated(id);
        emit jobFinished(id, JobState::Failed, records_.at(id).error);
        return id;
    }
    if (!engines_.ok()) {
        rec.state = JobState::Failed;
        rec.error = QStringLiteral("未找到 ffmpeg/ffprobe,请重新安装本程序。");
    }

    const quint64 id = rec.id;
    records_.emplace(id, std::move(rec));
    emit jobEnqueued(id);
    if (records_.at(id).state == JobState::Failed) {
        emit jobUpdated(id);
        emit jobFinished(id, JobState::Failed, records_.at(id).error);
        return id;
    }
    queue_.push_back(id);
    emit jobUpdated(id);
    startNext();
    return id;
}

void JobModel::cancelCurrent() {
    if (active_) active_->cancel();
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
    if (active_ || queue_.empty()) return;
    if (!engines_.ok()) {  // 队列里残留的作业一并落失败
        while (!queue_.empty()) {
            const quint64 id = queue_.front();
            queue_.pop_front();
            auto& rec = mutableRecord(id);
            rec.state = JobState::Failed;
            rec.error = QStringLiteral("未找到 ffmpeg/ffprobe,请重新安装本程序。");
            emit jobUpdated(id);
            emit jobFinished(id, JobState::Failed, rec.error);
        }
        return;
    }

    const quint64 id = queue_.front();
    queue_.pop_front();
    JobRecord& rec = mutableRecord(id);

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
    emit jobUpdated(id);

    active_ = std::make_unique<FfmpegJob>(std::move(cfg), this);
    connect(active_.get(), &FfmpegJob::progress, this,
            [this, id](int percent) {
                if (activeId_ != id) return;
                auto& r = mutableRecord(id);
                if (percent > r.progress) r.progress = percent;
                emit jobProgress(id, r.progress);
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
    active_.reset();     // 作业对象随之析构
    passlog_.reset();    // passlogfile 临时目录在此清理
    emit jobUpdated(id);
    emit jobFinished(id, state, error);
    startNext();
}

JobRecord& JobModel::mutableRecord(quint64 id) { return records_.at(id); }

}  // namespace one6s::jobs
