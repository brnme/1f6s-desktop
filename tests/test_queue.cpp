// test_queue.cpp — M3 任务队列语义 headless 集成测试。
//
// 队列契约(与 jobmodel.h 文档一致):
//   1. FIFO 串行:同一时刻至多 1 个任务 running;先入队者先完成;
//   2. 取消 running 任务:它落 cancelled,后续任务照常执行;
//   3. removeQueued 只作用于 queued 任务:出队 + 标记 cancelled + taskRemoved,
//      永不执行;对 running/终态任务返回 false 且无副作用;
//   4. 状态变迁 queued → running → done/cancelled/failed,每次变迁发 taskUpdated。
//
// 现场合成 2 个 1s 视频;ffmpeg 缺失时整体 QSKIP。
#include <QtTest/QtTest>

#include <QEventLoop>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include "core/encode.h"
#include "core/spec.h"
#include "jobs/engine.h"
#include "jobs/jobmodel.h"

using one6s::Spec;
using one6s::encode::outputPath;
using one6s::jobs::JobModel;
using one6s::jobs::JobRecord;
using one6s::jobs::JobState;

#ifndef SPEC_DIR
#define SPEC_DIR "."
#endif

class TestQueue : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();            // 引擎定位 + 合成 2 个 1s 视频
    void fifoSerialCompletion();    // 契约 1:串行 + FIFO 顺序 + 状态变迁链
    void cancelRunningNextRuns();   // 契约 2:取消第一个,第二个仍执行
    void removeQueuedSemantics();   // 契约 3:移除 queued 任务 + 非 queued 拒绝

private:
    static QString qstr(const std::string& s) { return QString::fromStdString(s); }

    QTemporaryDir dir_;
    QString input1_;
    QString input2_;
    Spec spec_;
    one6s::jobs::EnginePaths engines_;
};

void TestQueue::initTestCase() {
    engines_ = one6s::jobs::locateEngines();
    if (engines_.ffmpeg.isEmpty() || engines_.ffprobe.isEmpty())
        QSKIP("ffmpeg/ffprobe 不可用,跳过队列集成测试");
    QVERIFY(dir_.isValid());
    spec_ = Spec::load(QStringLiteral(SPEC_DIR "/levels.json"));

    input1_ = dir_.filePath(QStringLiteral("in1.mp4"));
    input2_ = dir_.filePath(QStringLiteral("in2.mp4"));
    for (const QString* out : {&input1_, &input2_}) {
        QProcess gen;
        gen.start(engines_.ffmpeg,
                  {QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
                   QStringLiteral("-i"),
                   QStringLiteral("testsrc=duration=1:size=320x240:rate=10"),
                   QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                   QStringLiteral("sine=frequency=440:duration=1"),
                   QStringLiteral("-c:v"), QStringLiteral("libx264"),
                   QStringLiteral("-c:a"), QStringLiteral("aac"),
                   QStringLiteral("-shortest"), *out});
        QVERIFY2(gen.waitForStarted(10000), "ffmpeg 无法启动");
        QVERIFY2(gen.waitForFinished(60000),
                 "合成超时: " + gen.readAllStandardError().left(300));
        QVERIFY2(gen.exitCode() == 0,
                 "合成失败: " + gen.readAllStandardError().left(300));
        QVERIFY(QFileInfo::exists(*out));
    }
}

void TestQueue::fifoSerialCompletion() {
    JobModel model;
    model.setSpec(spec_);
    model.setEngines(engines_);

    // 契约 4:每个任务的状态变迁链恰为 queued → running → done。
    // (只在状态真正变化时记链;running 期间的进度更新不重复计数。)
    QMap<quint64, QVector<JobState>> transitions;
    int running_now = 0;
    int max_concurrent = 0;
    QVector<quint64> finished_order;
    QEventLoop loop;
    QTimer::singleShot(240000, &loop, &QEventLoop::quit);

    QObject::connect(&model, &JobModel::taskUpdated, this, [&](quint64 id) {
        const JobRecord* r = model.record(id);
        QVERIFY(r != nullptr);
        QVector<JobState>& chain = transitions[id];
        if (!chain.isEmpty() && chain.last() == r->state) return;  // 进度更新
        if (r->state == JobState::Running) ++running_now;
        else if (!chain.isEmpty() && chain.last() == JobState::Running)
            --running_now;
        chain.append(r->state);
        max_concurrent = qMax(max_concurrent, running_now);
    });
    QObject::connect(&model, &JobModel::jobFinished, this,
                     [&](quint64 id, JobState st, const QString& err) {
                         if (st == JobState::Failed)
                             qWarning("job %llu failed: %s",
                                      static_cast<unsigned long long>(id),
                                      qPrintable(err));
                         finished_order.append(id);
                         if (finished_order.size() == 2) loop.quit();
                     });

    const QString out1 = qstr(outputPath(spec_, dir_.path().toStdString(),
                                         input1_.toStdString()));
    const QString out2 = qstr(outputPath(spec_, dir_.path().toStdString(),
                                         input2_.toStdString()));
    const quint64 id1 = model.enqueue(input1_, out1, QStringLiteral("L4"),
                                      nlohmann::json::object(), 1.0, true);
    const quint64 id2 = model.enqueue(input2_, out2, QStringLiteral("L4"),
                                      nlohmann::json::object(), 1.0, true);
    QVERIFY(id1 != 0 && id2 != 0 && id1 != id2);
    QVERIFY(!model.idle());
    QCOMPARE(model.record(id2)->state, JobState::Queued);  // 第二单排队

    loop.exec();

    QCOMPARE(finished_order.size(), 2);
    QCOMPARE(max_concurrent, 1);                 // 严格串行:从未并发
    QCOMPARE(finished_order[0], id1);            // FIFO:先入队先完成
    QCOMPARE(finished_order[1], id2);
    QCOMPARE(model.record(id1)->state, JobState::Done);
    QCOMPARE(model.record(id2)->state, JobState::Done);
    QCOMPARE(model.record(id1)->progress, 100);
    QCOMPARE(model.record(id2)->progress, 100);
    // 契约 4:变迁链精确匹配(queued → running → done)。
    for (const quint64 id : {id1, id2}) {
        const QVector<JobState>& chain = transitions.value(id);
        QCOMPARE(chain.size(), 3);
        QCOMPARE(chain[0], JobState::Queued);
        QCOMPARE(chain[1], JobState::Running);
        QCOMPARE(chain[2], JobState::Done);
    }
    QVERIFY(QFileInfo::exists(out1));
    QVERIFY(QFileInfo::exists(out2));
    QVERIFY(model.idle());
}

void TestQueue::cancelRunningNextRuns() {
    JobModel model;
    model.setSpec(spec_);
    model.setEngines(engines_);

    QVector<QPair<quint64, JobState>> finished;
    QEventLoop loop;
    QTimer::singleShot(240000, &loop, &QEventLoop::quit);
    QObject::connect(&model, &JobModel::jobFinished, this,
                     [&](quint64 id, JobState st, const QString&) {
                         finished.append({id, st});
                         if (finished.size() == 2) loop.quit();
                     });

    const QString out1 = qstr(outputPath(spec_, dir_.path().toStdString(),
                                         input1_.toStdString()));
    const QString out2 = qstr(outputPath(spec_, dir_.path().toStdString(),
                                         input2_.toStdString()));
    const quint64 id1 = model.enqueue(input1_, out1, QStringLiteral("L4"),
                                      nlohmann::json::object(), 1.0, true);
    const quint64 id2 = model.enqueue(input2_, out2, QStringLiteral("L4"),
                                      nlohmann::json::object(), 1.0, true);
    // id1 已被串行队列拉起(running),同步紧跟取消 —— 结果确定,不依赖转码快慢。
    QCOMPARE(model.record(id1)->state, JobState::Running);
    model.cancelTask(id1);

    loop.exec();

    QCOMPARE(finished.size(), 2);
    QCOMPARE(finished[0].first, id1);
    QCOMPARE(finished[0].second, JobState::Cancelled);   // 第一个被取消
    QCOMPARE(finished[1].first, id2);
    QCOMPARE(finished[1].second, JobState::Done);        // 第二个照常执行
    QVERIFY(!QFileInfo::exists(out1));                   // 取消不残留产物
    QVERIFY(QFileInfo::exists(out2));
    QVERIFY(model.idle());
}

void TestQueue::removeQueuedSemantics() {
    JobModel model;
    model.setSpec(spec_);
    model.setEngines(engines_);

    QVector<QPair<quint64, JobState>> finished;
    int removed_signals = 0;
    QEventLoop loop;
    QTimer::singleShot(240000, &loop, &QEventLoop::quit);
    QObject::connect(&model, &JobModel::taskRemoved, this,
                     [&]() { ++removed_signals; });
    QObject::connect(&model, &JobModel::jobFinished, this,
                     [&](quint64 id, JobState st, const QString&) {
                         finished.append({id, st});
                         if (finished.size() == 2) loop.quit();
                     });

    const QString out1 = qstr(outputPath(spec_, dir_.path().toStdString(),
                                         input1_.toStdString()));
    const QString out2 = qstr(outputPath(spec_, dir_.path().toStdString(),
                                         input2_.toStdString()));
    const quint64 id1 = model.enqueue(input1_, out1, QStringLiteral("L4"),
                                      nlohmann::json::object(), 1.0, true);
    const quint64 id2 = model.enqueue(input2_, out2, QStringLiteral("L4"),
                                      nlohmann::json::object(), 1.0, true);
    QCOMPARE(model.record(id1)->state, JobState::Running);

    // 契约 3:移除排队中的 id2 → 出队 + cancelled + taskRemoved。
    QVERIFY(model.removeQueued(id2));
    QCOMPARE(model.record(id2)->state, JobState::Cancelled);
    // 非 queued 任务拒绝移除:running(id1)与终态(id2)都返回 false。
    QVERIFY(!model.removeQueued(id1));
    QVERIFY(!model.removeQueued(id2));

    loop.exec();

    QCOMPARE(removed_signals, 1);
    QCOMPARE(finished.size(), 2);
    // id2 的 cancelled 通知在 removeQueued 时同步到达,先于 id1 完成。
    QCOMPARE(finished[0].first, id2);
    QCOMPARE(finished[0].second, JobState::Cancelled);
    QCOMPARE(finished[1].first, id1);
    QCOMPARE(finished[1].second, JobState::Done);
    QVERIFY(!QFileInfo::exists(out2));
    QVERIFY(QFileInfo::exists(out1));
    QVERIFY(model.idle());  // 移除后队列排空,id1 完成即整体空闲
}

QTEST_MAIN(TestQueue)
#include "test_queue.moc"
