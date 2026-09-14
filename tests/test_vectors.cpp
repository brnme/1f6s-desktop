// test_vectors.cpp — 黄金向量对拍(核心验收)。
// oracle:vectors/vectors.json(由网站仓真实代码 gen_vectors.py 生成)。
// commands case:resolve → eff 逐字段断言;buildCommands 输出与向量逐 token 断言,
//   其中 pass1 尾部三 token("-f","mp4","DEVNULL")按 AGENTS.md 等价规则归一为
//   ("-f","null","-")后比较,其余 token 必须逐一相等。
// error case:resolve 必须抛 SpecError 且 message 与 expected_error 逐字相等。
// estimate case:estimateMb 与期望值按 double 精确相等(非 fuzzy)。
#include <QtTest/QtTest>

#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/encode.h"
#include "core/spec.h"

using nlohmann::json;
using one6s::Eff;
using one6s::Spec;
using one6s::SpecError;
using Tokens = std::vector<std::string>;

namespace {

// AGENTS.md 等价归一:把向量命令尾部的 ("-f","mp4","DEVNULL") 换成 ("-f","null","-")。
void normalizeDevnull(Tokens& cmd) {
    for (size_t i = 0; i + 3 <= cmd.size(); ++i) {
        if (cmd[i] == "-f" && cmd[i + 1] == "mp4" && cmd[i + 2] == "DEVNULL") {
            cmd[i] = "-f";
            cmd[i + 1] = "null";
            cmd[i + 2] = "-";
            return;
        }
    }
}

std::string quote(const std::string& s) { return "\"" + s + "\""; }

std::string dumpTokens(const Tokens& t) {
    std::string s = "[";
    for (size_t i = 0; i < t.size(); ++i) {
        if (i) s += ", ";
        s += quote(t[i]);
    }
    return s + "]";
}

// tokens 首个差异点的可读描述。
std::string firstDiff(const Tokens& a, const Tokens& b) {
    std::string msg = "got=" + dumpTokens(a) + " want=" + dumpTokens(b);
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        if (a[i] != b[i]) return "token[" + std::to_string(i) + "] got " + quote(a[i]) +
                                    " want " + quote(b[i]) + "; " + msg;
    }
    return "size got " + std::to_string(a.size()) + " want " +
           std::to_string(b.size()) + "; " + msg;
}

}  // namespace

class TestVectors : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void oracle_data();
    void oracle();
    void resolveExtraChecks();

private:
    void compareEff(const Eff& e, const json& want, const std::string& id) const;

    json m_doc;
    Spec m_spec;
};

void TestVectors::initTestCase() {
    std::ifstream in(VECTORS_JSON);
    QVERIFY2(in.good(), "cannot open " VECTORS_JSON);
    in >> m_doc;

    m_spec = Spec::load(QStringLiteral(SPEC_DIR "/levels.json"));

    QCOMPARE(m_doc["case_count"].get<int>(), static_cast<int>(m_doc["cases"].size()));
    // M1 契约:黄金向量 261 case(189 commands + 24 error + 48 estimate)。
    QCOMPARE(static_cast<int>(m_doc["cases"].size()), 261);
}

void TestVectors::oracle_data() {
    QTest::addColumn<int>("index");
    const auto& cases = m_doc["cases"];
    for (size_t i = 0; i < cases.size(); ++i)
        QTest::newRow(cases[i]["id"].get<std::string>().c_str()) << static_cast<int>(i);
}

void TestVectors::compareEff(const Eff& e, const json& want, const std::string& id) const {
    auto str = [&want](const char* k) { return want.at(k).get<std::string>(); };
    auto num = [&want](const char* k) { return want.at(k).get<int>(); };

    QVERIFY2(e.level == str("level"), (id + ": level").c_str());
    QVERIFY2(e.interval == num("interval"),
             (id + ": interval got " + std::to_string(e.interval)).c_str());
    QVERIFY2(e.scale == str("scale"), (id + ": scale got " + e.scale).c_str());
    QVERIFY2(e.gray == want.at("gray").get<bool>(),
             (id + ": gray").c_str());
    QVERIFY2(e.codec == str("codec"), (id + ": codec got " + e.codec).c_str());
    QVERIFY2(e.audio_bitrate == str("audio_bitrate"),
             (id + ": audio_bitrate got " + e.audio_bitrate).c_str());
    QVERIFY2(e.audio_rate == num("audio_rate"),
             (id + ": audio_rate got " + std::to_string(e.audio_rate)).c_str());
    QVERIFY2(e.twopass == want.at("twopass").get<bool>(), (id + ": twopass").c_str());

    // JSON null ↔ nullopt
    QVERIFY2(e.crf.has_value() == !want.at("crf").is_null(),
             (id + ": crf presence").c_str());
    if (e.crf.has_value())
        QVERIFY2(*e.crf == num("crf"),
                 (id + ": crf got " + std::to_string(*e.crf)).c_str());
    QVERIFY2(e.tune.has_value() == !want.at("tune").is_null(),
             (id + ": tune presence").c_str());
    if (e.tune.has_value())
        QVERIFY2(*e.tune == str("tune"), (id + ": tune got " + *e.tune).c_str());
    QVERIFY2(e.extra.has_value() == !want.at("extra").is_null(),
             (id + ": extra presence").c_str());
    if (e.extra.has_value())
        QVERIFY2(*e.extra == str("extra"), (id + ": extra got " + *e.extra).c_str());
    QVERIFY2(e.target_mb.has_value() == !want.at("target_mb").is_null(),
             (id + ": target_mb presence").c_str());
    if (e.target_mb.has_value())
        QVERIFY2(*e.target_mb == num("target_mb"),
                 (id + ": target_mb got " + std::to_string(*e.target_mb)).c_str());
}

void TestVectors::oracle() {
    QFETCH(int, index);
    const json& c = m_doc["cases"][static_cast<size_t>(index)];
    const std::string id = c.at("id").get<std::string>();
    const std::string level = c.at("level").get<std::string>();
    const json& overrides = c.at("overrides");
    const double duration = c.at("duration_s").get<double>();
    const std::string kind = c.at("kind").get<std::string>();

    if (kind == "error") {
        bool thrown = false;
        std::string msg;
        try {
            static_cast<void>(one6s::resolve(m_spec, level, overrides));
        } catch (const SpecError& e) {
            thrown = true;
            msg = e.what();
        }
        const std::string want = c.at("expected_error").get<std::string>();
        QVERIFY2(thrown, (id + ": expected SpecError \"" + want + "\", got none").c_str());
        QVERIFY2(msg == want,
                 (id + ": error message got \"" + msg + "\" want \"" + want + "\"").c_str());
        return;
    }

    if (kind == "commands") {
        const Eff eff = one6s::resolve(m_spec, level, overrides);
        compareEff(eff, c.at("eff"), id);

        std::vector<Tokens> got = one6s::encode::buildCommands(
            m_spec, "INPUT", "OUTPUT", eff, duration, c.at("has_audio").get<bool>(),
            "PASSLOG");
        std::vector<Tokens> want;
        for (const auto& cmd : c.at("commands")) {
            Tokens t = cmd.get<Tokens>();
            normalizeDevnull(t);
            want.push_back(std::move(t));
        }
        QVERIFY2(got.size() == want.size(),
                 (id + ": command count got " + std::to_string(got.size()) +
                  " want " + std::to_string(want.size()))
                     .c_str());
        for (size_t i = 0; i < want.size(); ++i)
            QVERIFY2(got[i] == want[i],
                     (id + ": command[" + std::to_string(i) + "] " + firstDiff(got[i], want[i]))
                         .c_str());
        return;
    }

    if (kind == "estimate") {
        std::optional<int> target_mb;
        if (overrides.contains("target_mb") && !overrides.at("target_mb").is_null())
            target_mb = overrides.at("target_mb").get<int>();
        const double got = one6s::encode::estimateMb(level, duration, target_mb);
        const double want = c.at("estimate_mb").get<double>();
        // 精确相等(round1 与 Python round 同为正确舍入),不做 fuzzy 比较。
        QVERIFY2(got == want,
                 (id + ": estimate got " + std::to_string(got) + " want " +
                  std::to_string(want))
                     .c_str());
        return;
    }

    QFAIL((id + ": unknown kind " + kind).c_str());
}

// 向量矩阵未覆盖的错误文案与 Python 边界语义,逐条补测。
void TestVectors::resolveExtraChecks() {
    auto expectError = [&](const std::string& level, const json& ov,
                           const std::string& want) {
        const QString where = QString::fromStdString(level + ": want \"" + want + "\"");
        try {
            static_cast<void>(one6s::resolve(m_spec, level, ov));
        } catch (const SpecError& e) {
            QVERIFY2(std::string(e.what()) == want,
                     qPrintable(QStringLiteral("got \"%1\"; %2").arg(e.what(), where)));
            return;
        }
        QFAIL(qPrintable(QStringLiteral("expected SpecError, got none; %1").arg(where)));
    };

    expectError("L9", json::object(), "unknown level L9");
    expectError("L4", json("nope"), "overrides must be an object");
    expectError("L4", json::array({1, 2}), "overrides must be an object");
    // falsy 的 overrides 按 Python `overrides or {}` 视为空,不报错。
    static_cast<void>(one6s::resolve(m_spec, "L4", json(nullptr)));
    static_cast<void>(one6s::resolve(m_spec, "L4", json::array()));

    expectError("L4", json{{"resolution", 720}}, "resolution out of range");
    expectError("L4", json{{"interval", "abc"}}, "interval out of range");
    expectError("L4", json{{"interval", 5}}, "interval out of range");
    expectError("L4", json{{"grayscale", 1}}, "grayscale must be boolean");
    expectError("L4", json{{"grayscale", "yes"}}, "grayscale must be boolean");
    expectError("L4", json{{"audio", "64k"}}, "audio out of range");
    expectError("L4", json{{"target_mb", "abc"}}, "target_mb out of range");
    // 字符串 "0" 是 truthy → 进入校验并报越界(int 0 是 falsy,静默丢弃,见下)。
    expectError("L4", json{{"target_mb", "0"}}, "target_mb out of range");
    // interval 为 null 时被 truthiness 过滤 → 静默继承基础等级,不报错。
    const Eff nullIv = one6s::resolve(m_spec, "L4", json{{"interval", nullptr}});
    QCOMPARE(nullIv.interval, 6);

    // Python 语义补充:int 0 是 falsy → 静默丢弃;字符串 "6" 可转 int;True → 1。
    const Eff dropped = one6s::resolve(m_spec, "L4", json{{"target_mb", 0}});
    QCOMPARE(dropped.target_mb.has_value(), false);
    QCOMPARE(dropped.twopass, false);
    QCOMPARE(dropped.crf.has_value(), true);

    const Eff strInt = one6s::resolve(m_spec, "L4", json{{"interval", "6"}});
    QCOMPARE(strInt.interval, 6);

    const Eff boolInt = one6s::resolve(m_spec, "L4", json{{"target_mb", true}});
    QCOMPARE(boolInt.target_mb.has_value(), true);
    QCOMPARE(*boolInt.target_mb, 1);  // int(True) == 1 → 落在下界内

    // grayscale 的字符串大小写不敏感;显式 false 也生效。
    QCOMPARE(one6s::resolve(m_spec, "L1", json{{"grayscale", "TRUE"}}).gray, true);
    QCOMPARE(one6s::resolve(m_spec, "L2", json{{"grayscale", "False"}}).gray, false);
}

QTEST_MAIN(TestVectors)
#include "test_vectors.moc"
