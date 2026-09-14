// jobmodel.h — 极简任务模型与队列(一次跑一个,FIFO;UI 无关,可 headless 测试)。
//
// 作业以 (level, overrides) 描述,入队时用 core::resolve 解析为 Eff;
// 轮到它时经 encode::buildCommands 构造命令交给 FfmpegJob 执行。
// M3 再扩展:并发槽、失败重试、持久化队列。
#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <deque>
#include <map>
#include <memory>

#include <nlohmann/json.hpp>

#include "core/spec.h"
#include "jobs/engine.h"
#include "jobs/ffmpegjob.h"

namespace one6s::jobs {

enum class JobState { Queued, Running, Done, Cancelled, Failed };

QString jobStateName(JobState state);  // "queued"/"running"/"done"/"cancelled"/"failed"

struct JobRecord {
    quint64 id = 0;
    QString inputPath;
    QString outputPath;
    QString levelId;        // 如 "L4";与 overrides 合起来即任务的有效参数来源
    nlohmann::json overrides = nlohmann::json::object();  // 如 {"target_mb":8}
    one6s::Eff eff;         // 入队时 resolve 的结果(resolve 失败则作业 Failed)
    double durationS = 0.0;
    bool hasAudio = false;
    JobState state = JobState::Queued;
    int progress = 0;       // 0..100;Running 期间来自 FfmpegJob 的进度信号
    QString error;          // Failed 时的文案(stderr 末 800 字符等)
};

class JobModel : public QObject {
    Q_OBJECT
public:
    explicit JobModel(QObject* parent = nullptr);

    // Spec 与引擎路径由上层注入(app 启动时定位/加载一次)。
    void setSpec(one6s::Spec spec) { spec_ = std::move(spec); }
    void setEngines(EnginePaths engines) { engines_ = engines; }
    const EnginePaths& engines() const { return engines_; }

    // 入队并尝试启动。duration_s / has_audio 由调用方先 probe 得到。
    // resolve 失败( SpecError,如 L7 无 target_mb)时作业直接落 Failed,
    // 不阻塞后续队列。返回作业 id。
    quint64 enqueue(const QString& input_path, const QString& output_path,
                    const QString& level_id, const nlohmann::json& overrides,
                    double duration_s, bool has_audio);
    // 取消当前正在跑的作业;排队中的作业不受影响(M3 再做全队列取消)。
    void cancelCurrent();

    bool idle() const { return activeId_ == 0 && queue_.empty(); }
    const JobRecord* record(quint64 id) const;  // 不存在 → nullptr
    QList<JobRecord> records() const;

signals:
    void jobEnqueued(quint64 id);
    void jobUpdated(quint64 id);  // 状态/进度有任何变化
    void jobProgress(quint64 id, int percent);
    void jobFinished(quint64 id, JobState state, const QString& error);

private:
    void startNext();
    void finishActive(JobState state, const QString& error);
    JobRecord& mutableRecord(quint64 id);

    one6s::Spec spec_;
    EnginePaths engines_;
    std::map<quint64, JobRecord> records_;
    std::deque<quint64> queue_;   // 等待运行的 FIFO(仅存排队中的 id)
    quint64 activeId_ = 0;        // 0 = 无
    quint64 nextId_ = 1;
    std::unique_ptr<FfmpegJob> active_;
    std::unique_ptr<PasslogDir> passlog_;  // 两遍作业的 passlogfile 目录
};

}  // namespace one6s::jobs
