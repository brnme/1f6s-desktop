// test_splitter.cpp — 分割规划纯函数手算用例。
#include <QtTest/QtTest>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "core/splitter.h"

using namespace one6s::splitter;

namespace {
constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
}  // namespace

class TestSplitter : public QObject {
    Q_OBJECT

private slots:
    void planSplit_2h4GiBFreeTier();   // 任务书手算用例:2h/4GiB/2048MB 档
    void planSplit_precheckCap();
    void planSplit_floor60s();
    void planSplit_zeroSizeNoCap();    // 0 字节 + 预检无穷大 → inf 段长、至少 1 段
    void containerFor_fullTable();
    void containerFor_tolerant();
    void containerFor_unknown();
    void segmentCommand_tokens();
    void partName_numbering();
    void overLimit_indices();
};

void TestSplitter::planSplit_2h4GiBFreeTier() {
    // bitrate = 4GiB/7200s;T = 0.95×2GiB/bitrate = 3420s(恰为 0.95×7200/2);
    // 上限 90000s(h264 p720 预检)不触发;parts = ceil(7200/3420) = 3。
    const SplitPlan p = planSplit(7200.0, 4.0 * kGiB, 2048.0, 90000.0);
    QCOMPARE(p.parts, 3);
    QVERIFY2(std::fabs(p.segment_len_s - 3420.0) < 0.01,
             qPrintable(QStringLiteral("segment_len=%1").arg(p.segment_len_s)));
}

void TestSplitter::planSplit_precheckCap() {
    // 预检上限 1000s 小于体积上限允许的 3420s → T=1000,parts=ceil(7.2)=8。
    const SplitPlan p = planSplit(7200.0, 4.0 * kGiB, 2048.0, 1000.0);
    QCOMPARE(p.parts, 8);
    QVERIFY2(std::fabs(p.segment_len_s - 1000.0) < 1e-9,
             qPrintable(QStringLiteral("segment_len=%1").arg(p.segment_len_s)));
}

void TestSplitter::planSplit_floor60s() {
    // 高码率小档位:体积上限只够 30.6s → 抬到下限 60s;parts=ceil(600/60)=10。
    const SplitPlan p = planSplit(600.0, 40.0e9, 2048.0, std::numeric_limits<double>::infinity());
    QVERIFY2(std::fabs(p.segment_len_s - 60.0) < 1e-9,
             qPrintable(QStringLiteral("segment_len=%1").arg(p.segment_len_s)));
    QCOMPARE(p.parts, 10);
}

void TestSplitter::planSplit_zeroSizeNoCap() {
    // 0 字节 + 预检不设限:bitrate=0 → T=inf;ceil(dur/inf)=0 → 钳到至少 1 段。
    const SplitPlan p =
        planSplit(600.0, 0.0, 2048.0, std::numeric_limits<double>::infinity());
    QCOMPARE(p.parts, 1);
    QVERIFY(qIsInf(p.segment_len_s));
}

void TestSplitter::containerFor_fullTable() {
    struct Row {
        const char* ext;
        const char* muxer;
    };
    const std::vector<Row> table = {
        {"mp4", "mp4"},   {"m4v", "mp4"},      {"mkv", "matroska"}, {"webm", "webm"},
        {"mov", "mov"},   {"avi", "avi"},      {"flv", "flv"},      {"ts", "mpegts"},
        {"m2ts", "mpegts"}, {"wmv", "asf"},
    };
    for (const Row& r : table) {
        const std::optional<Container> c = containerFor(r.ext);
        QVERIFY2(c.has_value(), qPrintable(QStringLiteral("ext=%1").arg(r.ext)));
        QCOMPARE(c->muxer, r.muxer);
        QCOMPARE(c->keep_ext, std::string(r.ext));
    }
}

void TestSplitter::containerFor_tolerant() {
    const std::optional<Container> upper = containerFor("MP4");
    QVERIFY(upper.has_value());
    QCOMPARE(upper->muxer, "mp4");
    QCOMPARE(upper->keep_ext, std::string("mp4"));  // 规范化为小写

    const std::optional<Container> dotted = containerFor(".mkv");
    QVERIFY(dotted.has_value());
    QCOMPARE(dotted->muxer, "matroska");
    QCOMPARE(dotted->keep_ext, std::string("mkv"));
}

void TestSplitter::containerFor_unknown() {
    for (const char* ext : {"mpg", "rmvb", "3gp", ""}) {
        QVERIFY2(!containerFor(ext).has_value(),
                 qPrintable(QStringLiteral("ext=%1 should be unknown").arg(ext)));
    }
}

void TestSplitter::segmentCommand_tokens() {
    const std::vector<std::string> want = {
        "ffmpeg", "-y", "-i", "in.mkv", "-c", "copy", "-map", "0", "-f", "segment",
        "-segment_time", "1800", "-reset_timestamps", "1",
        "-segment_format", "matroska", "out.part%02dof%02d.mkv",
    };
    const std::vector<std::string> got =
        segmentCommand("in.mkv", "out.part%02dof%02d.mkv", "matroska", 1800.0);
    QVERIFY2(got == want,
             qPrintable(QStringLiteral("got %1").arg(QString::fromStdString(
                 [&] {
                     std::string s;
                     for (const std::string& t : got) s += t + " ";
                     return s;
                 }()))));

    // 整数段长给干净整数串;分数段长给浮点串。
    const std::vector<std::string> frac =
        segmentCommand("a.mp4", "b%03d.mp4", "mp4", 1800.5);
    QCOMPARE(frac[10], std::string("-segment_time"));
    QCOMPARE(frac[11], std::string("1800.5"));
}

void TestSplitter::partName_numbering() {
    QCOMPARE(partName("vid", 1, 3, "mp4"), std::string("vid.part01of03.mp4"));
    QCOMPARE(partName("vid", 3, 3, "mp4"), std::string("vid.part03of03.mp4"));
    // ≥100 的数字自然扩成三位(%02d 最小宽度),小序号仍保持两位。
    QCOMPARE(partName("vid", 1, 100, "mp4"), std::string("vid.part01of100.mp4"));
    QCOMPARE(partName("vid", 100, 120, "mkv"), std::string("vid.part100of120.mkv"));
    QCOMPARE(partName("a.b", 9, 9, "webm"), std::string("a.b.part09of09.webm"));
}

void TestSplitter::overLimit_indices() {
    const std::vector<int> hit = overLimit({100.0, 250.0, 300.0, 249.0}, 250.0);
    QCOMPARE(hit, std::vector<int>{2});  // 严格大于,恰等于上限不算超
    QVERIFY(overLimit({100.0, 50.0}, 250.0).empty());
    QVERIFY(overLimit({}, 1.0).empty());
    QCOMPARE(overLimit({300.0, 400.0}, 250.0), std::vector<int>({0, 1}));
}

QTEST_MAIN(TestSplitter)
#include "test_splitter.moc"
