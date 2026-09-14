// spec.cpp — params.py:1-115 的 C++ 复刻。错误文案与校验顺序与 Python 一致。
#include "core/spec.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace one6s {

const std::map<std::string, std::string> kAllowedScales = {
    {"720p", "-2:720"}, {"480p", "-2:480"}, {"360p", "-2:360"},
};
const std::map<int, std::string> kAllowedIntervals = {
    {4, "1/4"}, {6, "1/6"}, {10, "1/10"},
};
const std::map<std::string, std::pair<std::string, int>> kAllowedAudio = {
    {"20k", {"20k", 22050}}, {"8k", {"8k", 8000}},
};

const Level& Spec::level(const std::string& id) const {
    auto it = levels.find(id);
    if (it == levels.end()) throw SpecError("unknown level " + id);
    return it->second;
}

Spec Spec::parse(const nlohmann::json& j) {
    if (!j.is_object()) throw SpecError("levels.json: root must be an object");
    Spec s;
    s.version = j.value("version", "");  // 关于页展示/官网 /api/levels 对版本用
    const nlohmann::json defs = j.value("defaults", nlohmann::json::object());
    s.default_level = defs.value("level", "L4");
    s.output_suffix = defs.value("output_suffix", "_1f6s.mp4");
    s.audio_codec = j.value("audio_codec", "aac");
    s.audio_channels = j.value("audio_channels", 1);
    s.pixel_format = j.value("pixel_format", "yuv420p");
    s.preset = j.value("preset", "slow");
    for (const auto& lv : j.value("levels", nlohmann::json::array())) {
        Level l;
        l.id = lv.at("id").get<std::string>();
        l.name = lv.value("name", "");  // UI 显示用;缺失容错,不影响参数对拍
        l.interval = lv.value("interval", "1/6");
        l.scale = lv.value("scale", "");
        l.color = lv.value("color", "color");
        l.codec = lv.value("codec", "");
        l.audio_bitrate = lv.value("audio_bitrate", "20k");
        l.audio_rate = lv.value("audio_rate", 22050);
        l.twopass = lv.value("twopass", false);
        if (lv.contains("crf") && !lv.at("crf").is_null()) l.crf = lv.at("crf").get<int>();
        if (lv.contains("tune") && !lv.at("tune").is_null())
            l.tune = lv.at("tune").get<std::string>();
        if (lv.contains("extra") && !lv.at("extra").is_null())
            l.extra = lv.at("extra").get<std::string>();
        s.levels[l.id] = std::move(l);
    }
    return s;
}

Spec Spec::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw SpecError("cannot open levels.json: " + path);
    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        throw SpecError(std::string("levels.json parse error: ") + e.what());
    }
    return parse(j);
}

bool py_truthy(const nlohmann::json& v) {
    using nlohmann::json;
    switch (v.type()) {
        case json::value_t::boolean: return v.get<bool>();
        case json::value_t::number_integer:
        case json::value_t::number_unsigned:
        case json::value_t::number_float: return v.get<double>() != 0.0;
        case json::value_t::string: return !v.get<std::string>().empty();
        case json::value_t::array:
        case json::value_t::object: return !v.empty();
        default: return false;  // null 等
    }
}

bool py_to_int(const nlohmann::json& v, int& out) {
    using nlohmann::json;
    switch (v.type()) {
        case json::value_t::boolean:
            out = v.get<bool>() ? 1 : 0;  // Python: bool 是 int 子类,int(True)==1
            return true;
        case json::value_t::number_integer:
        case json::value_t::number_unsigned: {
            if (!v.is_number_integer()) return false;
            const long long n = v.get<long long>();
            if (n < -2147483648LL || n > 2147483647LL) return false;  // 溢出 → 落越界错误
            out = static_cast<int>(n);
            return true;
        }
        case json::value_t::number_float: {
            const double d = v.get<double>();
            if (!std::isfinite(d)) return false;  // Python int(nan/inf) 抛异常 → 落越界
            // Python int() 对浮点向零截断
            if (d >= 2147483648.0 || d < -2147483648.0) return false;
            out = static_cast<int>(d);
            return true;
        }
        case json::value_t::string: {
            // Python int(s):首尾空白可忽略,可选正负号,余下必须全为十进制数字。
            const std::string s = v.get<std::string>();
            size_t b = s.find_first_not_of(" \t\n\r\f\v");
            if (b == std::string::npos) return false;
            size_t e = s.find_last_not_of(" \t\n\r\f\v");
            size_t i = b;
            if (s[i] == '+' || s[i] == '-') ++i;
            if (i > e) return false;
            long long n = 0;
            for (size_t k = i; k <= e; ++k) {
                if (!std::isdigit(static_cast<unsigned char>(s[k]))) return false;
                n = n * 10 + (s[k] - '0');
                if (n > 2147483648LL) return false;  // 提前截断溢出
            }
            out = static_cast<int>(s[b] == '-' ? -n : n);
            return true;
        }
        default:
            return false;  // null/容器:Python int() 抛 TypeError → 落越界错误
    }
}

int parseInterval(const std::string& raw) {
    // params.py:23-26 — ^1/(\d+)$ → int(分组);不匹配 → 6。
    if (raw.size() > 2 && raw[0] == '1' && raw[1] == '/') {
        bool ok = true;
        for (size_t i = 2; i < raw.size(); ++i)
            if (!std::isdigit(static_cast<unsigned char>(raw[i]))) { ok = false; break; }
        if (ok) return std::stoi(raw.substr(2));
    }
    return 6;
}

const std::string& intervalToken(int interval) { return kAllowedIntervals.at(interval); }

namespace {

// Python: `"resolution" in overrides and overrides["resolution"]` 之类的
// 「键存在且 truthy 才校验,否则静默继承基础等级」。
bool present_and_truthy(const nlohmann::json& ov, const char* key) {
    return ov.contains(key) && py_truthy(ov.at(key));
}

}  // namespace

Eff resolve(const Spec& spec, const std::string& level_id, const nlohmann::json& overrides) {
    // params.py resolve:先查等级,再校验 overrides。
    if (!spec.has_level(level_id)) throw SpecError("unknown level " + level_id);
    const Level& lv = spec.level(level_id);
    // Python `overrides or {}`:falsy 的 overrides(None/false/0/""/空容器)当作空;
    // truthy 且非 object 才报 "overrides must be an object"。
    static const nlohmann::json kEmpty = nlohmann::json::object();
    const nlohmann::json& ov = py_truthy(overrides) ? overrides : kEmpty;
    if (!ov.is_object()) throw SpecError("overrides must be an object");

    // --- validate_overrides(params.py:36-78),校验顺序与 Python 一致 ---
    std::optional<std::string> resolution;
    if (present_and_truthy(ov, "resolution")) {
        const auto& r = ov.at("resolution");
        const bool ok = r.is_string() && kAllowedScales.count(r.get<std::string>()) != 0;
        if (!ok) throw SpecError("resolution out of range");
        resolution = r.get<std::string>();
    }
    std::optional<int> interval;
    if (present_and_truthy(ov, "interval")) {
        int iv = 0;
        if (!py_to_int(ov.at("interval"), iv) || kAllowedIntervals.count(iv) == 0)
            throw SpecError("interval out of range");
        interval = iv;
    }
    std::optional<bool> gray;
    if (ov.contains("grayscale")) {
        // Python: `overrides["grayscale"] not in (None, "")` 才校验——注意 int 0 会进到这里。
        const auto& g = ov.at("grayscale");
        const bool proceed = !(g.is_null() || (g.is_string() && g.get<std::string>().empty()));
        if (proceed) {
            std::optional<bool> b;
            if (g.is_string()) {
                std::string s = g.get<std::string>();
                std::transform(s.begin(), s.end(), s.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (s == "true") b = true;
                else if (s == "false") b = false;
                else throw SpecError("grayscale must be boolean");
            }
            if (!b) {
                if (g.is_boolean()) b = g.get<bool>();
                else throw SpecError("grayscale must be boolean");
            }
            gray = b;
        }
    }
    std::optional<std::string> audio;
    if (present_and_truthy(ov, "audio")) {
        const auto& a = ov.at("audio");
        const bool ok = a.is_string() && kAllowedAudio.count(a.get<std::string>()) != 0;
        if (!ok) throw SpecError("audio out of range");
        audio = a.get<std::string>();
    }
    std::optional<int> target_mb;
    if (present_and_truthy(ov, "target_mb")) {
        int tmb = 0;
        // 字符串 "0" 是 truthy → 走到这里并报越界;int 0 是 falsy → 静默丢弃(见下方分支)。
        if (!py_to_int(ov.at("target_mb"), tmb)) throw SpecError("target_mb out of range");
        if (tmb < kTargetMbMin || tmb > kTargetMbMax) throw SpecError("target_mb out of range");
        target_mb = tmb;
    }

    // --- 合成 eff(params.py:87-115)---
    Eff e;
    e.level = level_id;
    e.interval = parseInterval(lv.interval);
    e.scale = lv.scale;
    e.gray = (lv.color == "gray");
    e.codec = lv.codec;
    e.crf = lv.crf;
    e.audio_bitrate = lv.audio_bitrate;
    e.audio_rate = lv.audio_rate;
    e.tune = lv.tune;
    e.extra = lv.extra;
    e.twopass = lv.twopass;
    if (resolution) e.scale = kAllowedScales.at(*resolution);
    if (interval) e.interval = *interval;
    if (gray) e.gray = *gray;
    if (audio) {
        const auto& entry = kAllowedAudio.at(*audio);
        e.audio_bitrate = entry.first;
        e.audio_rate = entry.second;
    }
    if (target_mb) {
        e.target_mb = target_mb;
        e.twopass = true;
        e.crf = std::nullopt;  // 码率模式
    }
    if (lv.twopass && !lv.crf.has_value() && !e.target_mb.has_value())
        throw SpecError("L7 requires target_mb");
    return e;
}

}  // namespace one6s
