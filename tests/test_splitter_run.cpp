// test_splitter_run.cpp — M3 分割执行 headless 集成测试。
//
// 现场合成 12s 密集关键帧视频(testsrc + sine,-g 10 = 每 1s 一个关键帧),
// 用自定义档位把计划段长压到 ≈4s(段长下限调小,否则 60s 下限会让 12s 视频
// 只有 1 段):断言恰好 3 段、part01of03..part03of03 命名、各段实测时长
// 4±0.6s、体积全部 ≤ 档位上限、每段 1 点合计 3 点。ffmpeg 缺失时整体 QSKIP。
#include <QtTest/QtTest>

#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

#include <cmath>
#include <string>
#include <vector>

#include "core/precheck.h"
#include "core/probe.h"
#include "core/splitter.h"
#include "jobs/engine.h"
#include "jobs/splitrunner.h"

using one6s::ProbeResult;
using one6s::jobs::EnginePaths;
using one6s::jobs::SplitParams;
using one6s::jobs::SplitRunner;
using one6s::jobs::SplitSegment;
using one6s::jobs::locateEngines;
using one6s::jobs::segmentPoints;

namespace {
constexpr double kMiB = 1024.0 * 1024.0;
}  // namespace

class TestSplitterRun : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();       // 引擎定位 + 合成 12s 密集关键帧视频
    void splitRunsThreeParts();  // 核心:3 段/命名/时长/体积/点数
    void pointsFormula();      // segmentPoints 纯函数边界

private:
    QTemporaryDir dir_;
    QString input_;
    double duration_ = 12.0;
    double sizeBytes_ = 0;
    double tierMaxMb_ = 0;   // 由实测体积反推,使计划段长 ≈4.02s
    double limitBytes_ = 0;
    EnginePaths engines_;
};

void TestSplitterRun::initTestCase() {
    engines_ = locateEngines();
    if (engines_.ffmpeg.isEmpty() || engines_.ffprobe.isEmpty())
        QSKIP("ffmpeg/ffprobe 不可用,跳过分割集成测试");
    QVERIFY(dir_.isValid());

    // 密集关键帧:-g 10(每 1s 一个关键帧),保证 -segment_time 能按点切。
    input_ = dir_.filePath(QStringLiteral("in.mp4"));
    QProcess gen;
    gen.start(engines_.ffmpeg,
              {QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
               QStringLiteral("-i"),
               QStringLiteral("testsrc=duration=12:size=320x240:rate=10"),
               QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
               QStringLiteral("sine=duration=12"),
               QStringLiteral("-c:v"), QStringLiteral("libx264"),
               QStringLiteral("-g"), QStringLiteral("10"),
               QStringLiteral("-c:a"), QStringLiteral("aac"),
               input_});
    QVERIFY2(gen.waitForStarted(10000), "ffmpeg 无法启动");
    QVERIFY2(gen.waitForFinished(120000),
             "合成超时: " + gen.readAllStandardError().left(300));
    QVERIFY2(gen.exitCode() == 0,
             "合成失败: " + gen.readAllStandardError().left(300));
    QVERIFY(QFileInfo::exists(input_));

    ProbeResult r;
    try {
        r = one6s::probe(engines_.ffprobe, input_);
    } catch (const std::exception& e) {
        QFAIL(e.what());
    }
    QVERIFY2(std::fabs(r.duration_s - 12.0) < 1.0,
             qPrintable(QStringLiteral("duration=%1").arg(r.duration_s)));
    duration_ = r.duration_s;
    sizeBytes_ = static_cast<double>(QFileInfo(input_).size());
    QVERIFY(sizeBytes_ > 0);

    // 计划段长必须恰为 4.0s:segment muxer 只在时间戳 ≥ T 的关键帧切,
    // 关键帧每 1s 一枚 —— T=4.0 → 恰在 4s 关键帧切,3 段;
    // T 略大于 4(哪怕 1 ulp)会切在 5s 关键帧上变成 5/5/2,略小于 4 会规划出 4 段。
    // 故走「UI 预览确认后下发显式计划」路径,不经 tierMaxMb 反推(浮点往返
    // 会引入 ulp 偏差)。档位上限给足余量(0.4×源体积 > 每段 ≈0.33×源体积)。
    limitBytes_ = sizeBytes_ * 0.4;
    tierMaxMb_ = limitBytes_ / kMiB;
}

void TestSplitterRun::splitRunsThreeParts() {
    SplitParams p;
    p.inputPath = input_;
    p.outputDir = dir_.path();
    p.durationS = duration_;
    p.sizeBytes = sizeBytes_;
    p.width = 320;
    p.height = 240;
    p.codec = "h264";
    p.tierMaxMb = tierMaxMb_;
    p.precheckMaxS = one6s::Precheck::kInf;
    p.minSegmentS = 1.0;
    // UI 预览确认的显式计划:12s → 3 段 × 4s。
    p.segmentLenS = 4.0;
    p.plannedParts = 3;

    SplitRunner runner(p, engines_, this);
    int max_pct = -1;
    QString fin_state, fin_err;
    QEventLoop loop;
    QTimer::singleShot(120000, &loop, &QEventLoop::quit);  // 兜底防悬死
    QObject::connect(&runner, &SplitRunner::progress, this,
                     [&](int pct) { max_pct = qMax(max_pct, pct); });
    QObject::connect(&runner, &SplitRunner::finished, this,
                     [&](const QString& s, const QString& e) {
                         fin_state = s;
                         fin_err = e;
                         loop.quit();
                     });
    runner.start();
    loop.exec();

    QCOMPARE(fin_state, QStringLiteral("done"));
    QVERIFY2(fin_err.isEmpty(), qPrintable(fin_err));
    QVERIFY2(max_pct >= 100,
             qPrintable(QStringLiteral("max progress=%1").arg(max_pct)));

    // —— 断言:恰好 3 段 ——
    const QVector<SplitSegment>& segs = runner.segments();
    QCOMPARE(segs.size(), 3);

    // —— 断言:命名 part01of03..part03of03,落在输出目录 ——
    const QString stem = QFileInfo(input_).completeBaseName();
    for (int i = 0; i < 3; ++i) {
        const QString want = QStringLiteral("%1/%2.part%3of03.mp4")
                                 .arg(dir_.path(), stem)
                                 .arg(i + 1, 2, 10, QLatin1Char('0'));
        QVERIFY2(QFileInfo::exists(segs[i].path),
                 qPrintable(QStringLiteral("缺分段:%1").arg(segs[i].path)));
        QCOMPARE(segs[i].path, want);
    }

    // —— 断言:各段实测时长 4±0.6s(ffprobe 复核文件本体) ——
    double dur_sum = 0;
    for (int i = 0; i < 3; ++i) {
        ProbeResult pr;
        try {
            pr = one6s::probe(engines_.ffprobe, segs[i].path);
        } catch (const std::exception& e) {
            QFAIL(e.what());
        }
        QVERIFY2(std::fabs(pr.duration_s - 4.0) < 0.6,
                 qPrintable(QStringLiteral("段%1 时长=%2").arg(i + 1)
                                .arg(pr.duration_s)));
        dur_sum += pr.duration_s;
        // —— 断言:体积全部 ≤ 档位上限 ——
        QVERIFY2(segs[i].sizeBytes <= limitBytes_,
                 qPrintable(QStringLiteral("段%1 体积 %2 > 上限 %3")
                                .arg(i + 1)
                                .arg(segs[i].sizeBytes)
                                .arg(limitBytes_)));
        QVERIFY(!segs[i].overLimit);
        // 后验记录的时长与 ffprobe 实测一致(±10ms)。
        QVERIFY2(std::fabs(segs[i].durationS - pr.duration_s) < 0.01,
                 qPrintable(QStringLiteral("段%1 后验时长 %2 vs 实测 %3")
                                .arg(i + 1)
                                .arg(segs[i].durationS)
                                .arg(pr.duration_s)));
    }
    // 三段拼起来还是完整的 12s。
    QVERIFY2(std::fabs(dur_sum - duration_) < 1.0,
             qPrintable(QStringLiteral("总时长=%1").arg(dur_sum)));

    // —— 断言:点数 —— 每段 ≈4s 且远小于 1GiB → max(0,0,1)=1,合计 3。
    for (int i = 0; i < 3; ++i) QCOMPARE(segs[i].points, 1);
    QCOMPARE(runner.totalPoints(), 3);

    // 密集关键帧(-g 10)不得触发稀疏警告。
    QVERIFY2(runner.warnings().isEmpty(),
             qPrintable(runner.warnings().join(QLatin1Char(';'))));

    // 临时工作目录已清理,输出目录只剩 3 个分段与源文件。
    const QFileInfoList entries =
        QDir(dir_.path()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    QCOMPARE(entries.size(), 0);
}

void TestSplitterRun::pointsFormula() {
    // max(ceil(GB), ceil(小时), 1):1GiB/2h → 2 点(小时主导)。
    QCOMPARE(segmentPoints(1.0 * 1073741824.0, 7200.0), 2);
    // 2.5GiB/0.5h → 3 点(GB 主导)。
    QCOMPARE(segmentPoints(2.5 * 1073741824.0, 1800.0), 3);
    // 极小段 → 保底 1 点。
    QCOMPARE(segmentPoints(1.0, 1.0), 1);
    // 恰好整 GB/整小时:ceil 不放大。
    QCOMPARE(segmentPoints(2.0 * 1073741824.0, 3600.0), 2);
}

QTEST_MAIN(TestSplitterRun)
#include "test_splitter_run.moc"
