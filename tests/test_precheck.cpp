// test_precheck.cpp — 预检因子表:分辨率桶 × 编码族、回退链与上限公式。
#include <QtTest/QtTest>

#include <cmath>
#include <nlohmann/json.hpp>

#include "core/precheck.h"

using one6s::Precheck;

class TestPrecheck : public QObject {
    Q_OBJECT

private slots:
    void loadRealFactors();      // 真实 precheck_factors.json 的桶/族/上限
    void bucketBoundaries();     // 1280/1281、1920/1921、2560/2561 分桶边界
    void familyNormalization();  // 大小写与未知编码族
    void fallbackChain();        // 缺族/缺桶/零因子回退
    void disabledUnlimited();    // enabled=false → ∞
    void defaultMargin();        // json 缺 margin → 0.8
};

void TestPrecheck::loadRealFactors() {
    const Precheck pc = Precheck::load(QStringLiteral(SPEC_DIR "/precheck_factors.json"));
    QVERIFY(pc.enabled);
    QVERIFY2(std::fabs(pc.margin - 0.8) < 1e-12,
             qPrintable(QStringLiteral("margin=%1").arg(pc.margin)));

    QCOMPARE(pc.factor("h264", 1280, 720), 0.096);
    QCOMPARE(pc.factor("h264", 1920, 1080), 0.118);
    QCOMPARE(pc.factor("h264", 2560, 1440), 0.25);
    QCOMPARE(pc.factor("h264", 3840, 2160), 0.385);
    QCOMPARE(pc.factor("hevc", 1920, 1080), 0.182);
    QCOMPARE(pc.factor("vp9", 640, 480), 0.23);      // 未知族 → other/p720
    QCOMPARE(pc.factor("av1", 3840, 2160), 0.9);

    // maxInputSeconds = 180×60×0.8/因子(容差避开浮点尾差)。
    QVERIFY2(std::fabs(pc.maxInputSeconds(1280, 720, "h264") - 90000.0) < 0.01,
             qPrintable(QStringLiteral("max720=%1").arg(pc.maxInputSeconds(1280, 720, "h264"))));
    QVERIFY2(std::fabs(pc.maxInputSeconds(3840, 2160, "h264") - 22441.56) < 0.01,
             qPrintable(QStringLiteral("max2160=%1").arg(pc.maxInputSeconds(3840, 2160, "h264"))));
}

void TestPrecheck::bucketBoundaries() {
    const Precheck pc = Precheck::load(QStringLiteral(SPEC_DIR "/precheck_factors.json"));
    QCOMPARE(pc.factor("h264", 1280, 720), 0.096);   // 1280 → p720
    QCOMPARE(pc.factor("h264", 1281, 1281), 0.118);  // 1281 → p1080
    QCOMPARE(pc.factor("h264", 1920, 1080), 0.118);
    QCOMPARE(pc.factor("h264", 1921, 1080), 0.25);   // 1921 → p1440
    QCOMPARE(pc.factor("h264", 2560, 1440), 0.25);
    QCOMPARE(pc.factor("h264", 2561, 1440), 0.385);  // 2561 → p2160
    QCOMPARE(pc.factor("h264", 0, 0), 0.096);        // 0×0 视为最小桶
}

void TestPrecheck::familyNormalization() {
    const Precheck pc = Precheck::load(QStringLiteral(SPEC_DIR "/precheck_factors.json"));
    QCOMPARE(pc.factor("H264", 1280, 720), 0.096);   // 大小写不敏感
    QCOMPARE(pc.factor("HEVC", 1280, 720), 0.15);
    QCOMPARE(pc.factor("h264 ", 1280, 720), 0.23);   // 带空格视为未知 → other
}

void TestPrecheck::fallbackChain() {
    using nlohmann::json;
    // 缺 hevc 族、h264 只有 p720 桶。
    const auto pc = Precheck::parse(json::parse(R"({
        "enabled": true, "margin": 0.8,
        "factors": {
            "h264": {"p720": 0.1},
            "other": {"p720": 0.23, "p1080": 0.27, "p1440": 0.57, "p2160": 0.9}
        }})"));
    QCOMPARE(pc.factor("h264", 1280, 720), 0.1);     // 命中自家桶
    QCOMPARE(pc.factor("h264", 1920, 1080), 0.9);    // 缺桶 → other/p2160
    QCOMPARE(pc.factor("hevc", 640, 480), 0.23);     // 缺族 → other

    // 族表为空表也按缺族处理(Python `{} or factors["other"]` falsy 语义)。
    const auto pc2 = Precheck::parse(json::parse(R"({
        "enabled": true, "margin": 0.8,
        "factors": {
            "h264": {},
            "other": {"p720": 0.23, "p1080": 0.27, "p1440": 0.57, "p2160": 0.9}
        }})"));
    QCOMPARE(pc2.factor("h264", 1280, 720), 0.23);

    // 因子为 0 一样走回退;全部回退失败 → 0.9 兜底(与 Python `or 0.9` 一致,
    // 故 factor 实际不可能 ≤0,maxInputSeconds 的 ≤0 分支只是防御性死代码)。
    const auto pc3 = Precheck::parse(json::parse(R"({
        "enabled": true, "margin": 0.8,
        "factors": {"h264": {"p720": 0.0}, "other": {"p2160": 0.0}}})"));
    QCOMPARE(pc3.factor("h264", 640, 480), 0.9);
    QVERIFY2(std::fabs(pc3.maxInputSeconds(640, 480, "h264") - 9600.0) < 0.01,
             qPrintable(QStringLiteral("max=%1").arg(pc3.maxInputSeconds(640, 480, "h264"))));
}

void TestPrecheck::disabledUnlimited() {
    const auto pc = Precheck::parse(nlohmann::json::parse(
        R"({"enabled": false, "margin": 0.8, "factors": {"h264": {"p720": 0.096}}})"));
    QVERIFY(qIsInf(pc.maxInputSeconds(1280, 720, "h264")));
    QVERIFY(qIsInf(pc.maxInputSeconds(3840, 2160, "hevc")));
}

void TestPrecheck::defaultMargin() {
    // json 缺 margin → 0.8;缺 enabled → false(保守:不设限交给上层判断)。
    const auto pc = Precheck::parse(nlohmann::json::parse(
        R"({"enabled": true, "factors": {"other": {"p2160": 0.9}}})"));
    QVERIFY2(std::fabs(pc.margin - 0.8) < 1e-12,
             qPrintable(QStringLiteral("margin=%1").arg(pc.margin)));
    QVERIFY2(std::fabs(pc.maxInputSeconds(3840, 2160, "vp9") - 9600.0) < 0.01,
             qPrintable(QStringLiteral("max=%1").arg(pc.maxInputSeconds(3840, 2160, "vp9"))));
}

QTEST_MAIN(TestPrecheck)
#include "test_precheck.moc"
