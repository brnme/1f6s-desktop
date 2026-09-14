// test_jobs.cpp — M2 jobs 层 headless 集成测试。
//
// 真实 ffmpeg 现场合成 3s 测试视频(lavfi testsrc + sine,参考 test_probe.cpp),
// 跑通:L4 单遍作业(产物/进度/状态)、启动即取消(状态/无残留)、JobModel FIFO
// 队列,外加进度行解析与编码器自检的单元断言。ffmpeg 缺失时整体 QSKIP。
#include <QtTest/QtTest>

#include <QEventLoop>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <cmath>
#include <string>
#include <vector>

#include "core/encode.h"
#include "core/probe.h"
#include "core/spec.h"
#include "jobs/engine.h"
#include "jobs/ffmpegjob.h"
#include "jobs/jobmodel.h"

using one6s::Eff;
using one6s::ProbeResult;
using one6s::Spec;
using one6s::encode::buildCommands;
using one6s::encode::outputPath;
using one6s::jobs::EnginePaths;
using one6s::jobs::FfmpegJob;
using one6s::jobs::FfmpegJobConfig;
using one6s::jobs::JobModel;
using one6s::jobs::JobState;
using one6s::jobs::locateEngines;
using one6s::jobs::PasslogDir;
using one6s::jobs::parseOutTimeS;
using one6s::jobs::verifyEncoders;

#ifndef SPEC_DIR
#define SPEC_DIR "."
#endif

class TestJobs : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();          // 引擎定位 + 合成 3s 输入视频
    void parseOutTimeLines();     // 进度行解析(us/ms/时钟/非法行)
    void encodersCheck();         // verifyEncoders 正反两路
    void l4JobRunsToDone();       // 核心:L4 跑完 → 产物可解/进度≥95/状态 done
    void cancelRightAfterStart(); // 启动即取消 → cancelled 且无残留输出
    void jobModelQueueFifo();     // JobModel:两作业排队,FIFO 顺序完成

private:
    static QString qstr(const std::string& s) { return QString::fromStdString(s); }
    // 把 buildCommands 结果转成 FfmpegJobConfig(与 JobModel 内部同样的转换)。
    FfmpegJobConfig makeConfig(const QString& dst, const Eff& eff, bool twopass,
                               const QString& passlogfile) const;

    QTemporaryDir dir_;
    QString input_;      // 合成的 3s 测试视频(含音轨)
    double duration_ = 3.0;
    bool has_audio_ = true;
    Spec spec_;
    EnginePaths engines_;
};

void TestJobs::initTestCase() {
    engines_ = locateEngines();
    if (engines_.ffmpeg.isEmpty() || engines_.ffprobe.isEmpty())
        QSKIP("ffmpeg/ffprobe 不可用,跳过 jobs 集成测试");
    QVERIFY(dir_.isValid());
    spec_ = Spec::load(SPEC_DIR "/levels.json");

    // 合成 3s 测试视频:testsrc + sine,libx264 + aac。
    input_ = dir_.filePath(QStringLiteral("in.mp4"));
    QProcess gen;
    gen.start(engines_.ffmpeg,
              {QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
               QStringLiteral("-i"),
               QStringLiteral("testsrc=duration=3:size=320x240:rate=10"),
               QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
               QStringLiteral("sine=frequency=440:duration=3"),
               QStringLiteral("-c:v"), QStringLiteral("libx264"),
               QStringLiteral("-c:a"), QStringLiteral("aac"),
               QStringLiteral("-shortest"), input_});
    QVERIFY2(gen.waitForStarted(10000), "ffmpeg 无法启动");
    QVERIFY2(gen.waitForFinished(60000),
             "合成超时: " + gen.readAllStandardError().left(300));
    QVERIFY2(gen.exitCode() == 0,
             "合成失败: " + gen.readAllStandardError().left(300));
    QVERIFY(QFileInfo::exists(input_));
}

FfmpegJobConfig TestJobs::makeConfig(const QString& dst, const Eff& eff,
                                     bool twopass,
                                     const QString& passlogfile) const {
    const auto cmds = buildCommands(spec_, input_.toStdString(), dst.toStdString(),
                                    eff, duration_, has_audio_,
                                    passlogfile.toStdString());
    FfmpegJobConfig cfg;
    cfg.ffmpegPath = engines_.ffmpeg;
    for (const auto& tokens : cmds) {
        QStringList l;
        l.reserve(static_cast<qsizetype>(tokens.size()));
        for (const auto& t : tokens) l.append(QString::fromStdString(t));
        cfg.commands.append(l);
    }
    cfg.durationS = duration_;
    cfg.twopass = twopass;
    cfg.outputPath = dst;
    return cfg;
}

void TestJobs::parseOutTimeLines() {
    // out_time_us / out_time_ms 实为微秒(ffmpeg 历史命名),÷1e6。
    QCOMPARE(*parseOutTimeS(QStringLiteral("out_time_us=1234567")), 1.234567);
    QCOMPARE(*parseOutTimeS(QStringLiteral("out_time_ms=1000000")), 1.0);
    QVERIFY(std::fabs(*parseOutTimeS(QStringLiteral("out_time=00:00:02.972154")) -
                      2.972154) < 1e-9);
    QCOMPARE(*parseOutTimeS(QStringLiteral("out_time=01:00:00.000000")), 3600.0);
    // 带换行/空白与 progress 行、非法值。
    QCOMPARE(*parseOutTimeS(QStringLiteral("  out_time_us=42\n")), 42e-6);
    QVERIFY(!parseOutTimeS(QStringLiteral("progress=continue")).has_value());
    QVERIFY(!parseOutTimeS(QStringLiteral("frame=12")).has_value());
    QVERIFY(!parseOutTimeS(QStringLiteral("out_time=xx")).has_value());
    QVERIFY(!parseOutTimeS(QStringLiteral("garbage")).has_value());
}

void TestJobs::encodersCheck() {
    QVERIFY2(verifyEncoders(engines_.ffmpeg).isEmpty(),
             qPrintable(verifyEncoders(engines_.ffmpeg)));
    QVERIFY(!verifyEncoders(QString()).isEmpty());
    QVERIFY(!verifyEncoders(QStringLiteral("/nonexistent/ffmpeg")).isEmpty());
}

void TestJobs::l4JobRunsToDone() {
    const Eff eff = one6s::resolve(spec_, "L4");
    QVERIFY(!eff.twopass);
    const QString dst = qstr(outputPath(spec_, dir_.path().toStdString(),
                                        input_.toStdString()));
    FfmpegJob job(makeConfig(dst, eff, /*twopass=*/false, /*passlogfile=*/{}));

    int max_pct = -1;
    int last_pct = -1;
    QString fin_state, fin_err;
    QEventLoop loop;
    QTimer::singleShot(120000, &loop, &QEventLoop::quit);  // 兜底防悬死
    QObject::connect(&job, &FfmpegJob::progress, this,
                     [&](int p) { max_pct = qMax(max_pct, p); last_pct = p; });
    QObject::connect(&job, &FfmpegJob::finished, this,
                     [&](const QString& s, const QString& e) {
                         fin_state = s;
                         fin_err = e;
                         loop.quit();
                     });
    job.start();
    loop.exec();

    QCOMPARE(fin_state, QStringLiteral("done"));
    QVERIFY2(fin_err.isEmpty(), qPrintable(fin_err));
    QCOMPARE(job.state(), QStringLiteral("done"));
    QVERIFY2(max_pct >= 95,
             qPrintable(QStringLiteral("max progress=%1,期望 ≥95").arg(max_pct)));
    QCOMPARE(last_pct, 100);
    QVERIFY2(QFileInfo::exists(dst), "输出文件应存在");

    // 产物可被 ffprobe 解,且经 L4 的 scale=-2:480(320x240 → 640x480)。
    ProbeResult pr;
    try {
        pr = one6s::probe(engines_.ffprobe, dst);
    } catch (const std::exception& e) {
        QFAIL(e.what());
    }
    QCOMPARE(pr.width, 640);
    QCOMPARE(pr.height, 480);
    QCOMPARE(QString::fromStdString(pr.codec), QStringLiteral("h264"));
}

void TestJobs::cancelRightAfterStart() {
    const Eff eff = one6s::resolve(spec_, "L4");
    const QString dst = qstr(outputPath(spec_, dir_.path().toStdString(),
                                        input_.toStdString()));
    FfmpegJob job(makeConfig(dst, eff, /*twopass=*/false, /*passlogfile=*/{}));

    QString fin_state;
    QEventLoop loop;
    QTimer::singleShot(60000, &loop, &QEventLoop::quit);
    QObject::connect(&job, &FfmpegJob::finished, this,
                     [&](const QString& s, const QString&) {
                         fin_state = s;
                         loop.quit();
                     });
    job.start();
    // 同步紧跟取消:QProcess 还在 Starting,started 槽见到标志位后 kill,
    // 与转码快慢无关,结果确定。
    job.cancel();
    loop.exec();

    QCOMPARE(fin_state, QStringLiteral("cancelled"));
    QCOMPARE(job.state(), QStringLiteral("cancelled"));
    QVERIFY2(!QFileInfo::exists(dst), "取消后不得残留输出文件");
}

void TestJobs::jobModelQueueFifo() {
    JobModel model;
    model.setSpec(spec_);
    model.setEngines(engines_);

    QVector<quint64> finished_order;
    QVector<JobState> finished_states;
    QEventLoop loop;
    QTimer::singleShot(240000, &loop, &QEventLoop::quit);
    int done_count = 0;
    QObject::connect(&model, &JobModel::jobFinished, this,
                     [&](quint64 id, JobState st, const QString& err) {
                         finished_order.append(id);
                         finished_states.append(st);
                         if (st == JobState::Failed)
                             qWarning("job %llu failed: %s",
                                      static_cast<unsigned long long>(id),
                                      qPrintable(err));
                         if (++done_count == 2) loop.quit();
                     });

    const QString out1 = qstr(outputPath(spec_, dir_.path().toStdString(),
                                         input_.toStdString()));
    const QString out2 = qstr(outputPath(spec_, dir_.path().toStdString(),
                                         input_.toStdString()));
    const quint64 id1 = model.enqueue(input_, out1, QStringLiteral("L4"),
                                      nlohmann::json::object(), duration_,
                                      has_audio_);
    const quint64 id2 = model.enqueue(input_, out2, QStringLiteral("L4"),
                                      nlohmann::json::object(), duration_,
                                      has_audio_);
    QVERIFY(id1 != 0 && id2 != 0 && id1 != id2);
    QVERIFY(!model.idle());  // 第二单在排队
    QCOMPARE(model.record(id2)->state, JobState::Queued);

    loop.exec();
    QCOMPARE(finished_order.size(), 2);
    QCOMPARE(finished_states[0], JobState::Done);
    QCOMPARE(finished_states[1], JobState::Done);
    // FIFO:先入队者先完成。
    QCOMPARE(finished_order[0], id1);
    QCOMPARE(finished_order[1], id2);
    QVERIFY(model.idle());
    QVERIFY(QFileInfo::exists(out1));
    QVERIFY(QFileInfo::exists(out2));
    QCOMPARE(model.record(id1)->progress, 100);
}

QTEST_MAIN(TestJobs)
#include "test_jobs.moc"
