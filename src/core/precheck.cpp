// precheck.cpp — compressor.py:53-89 预检语义的 C++ 复刻。
#include "core/precheck.h"

#include <cctype>
#include <fstream>

#include "core/spec.h"  // SpecError(加载失败复用同一异常类型)

namespace one6s {

Precheck Precheck::parse(const nlohmann::json& j) {
    Precheck p;
    if (!j.is_object()) return p;
    p.enabled = j.value("enabled", false);
    p.margin = j.value("margin", 0.8);
    if (j.contains("factors") && j.at("factors").is_object()) {
        for (auto const& [fam, buckets] : j.at("factors").items()) {
            if (!buckets.is_object()) continue;
            std::map<std::string, double> row;
            for (auto const& [bucket, v] : buckets.items())
                if (v.is_number()) row[bucket] = v.get<double>();
            p.factors[fam] = std::move(row);
        }
    }
    return p;
}

Precheck Precheck::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw SpecError("cannot open precheck_factors.json: " + path);
    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        throw SpecError(std::string("precheck_factors.json parse error: ") + e.what());
    }
    return parse(j);
}

double Precheck::factor(const std::string& codec, int width, int height) const {
    // compressor.py:53-68。桶界:≤1280 p720 / ≤1920 p1080 / ≤2560 p1440 / 其余 p2160。
    const long m = std::max(width, height);
    const char* bucket = m <= 1280 ? "p720"
                       : m <= 1920 ? "p1080"
                       : m <= 2560 ? "p1440"
                                   : "p2160";
    std::string lc;
    lc.reserve(codec.size());
    for (char c : codec) lc.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    const std::string fam = (lc == "h264" || lc == "hevc") ? lc : "other";

    // Python `factors.get(fam) or factors.get("other") or {}`:存在的空表也是 falsy。
    static const std::map<std::string, double> kEmpty;
    auto fam_it = factors.find(fam);
    if (fam_it == factors.end() || fam_it->second.empty()) fam_it = factors.find("other");
    const auto& fam_factors =
        (fam_it != factors.end() && !fam_it->second.empty()) ? fam_it->second : kEmpty;

    // `fam_factors.get(bucket) or factors["other"]["p2160"] or 0.9`:0/缺失都算 falsy。
    if (auto b = fam_factors.find(bucket); b != fam_factors.end() && b->second != 0.0)
        return b->second;
    if (auto o = factors.find("other"); o != factors.end())
        if (auto p = o->second.find("p2160"); p != o->second.end() && p->second != 0.0)
            return p->second;
    return 0.9;
}

double Precheck::maxInputSeconds(int width, int height, const std::string& codec,
                                 double ffmpeg_timeout_min) const {
    if (!enabled) return kInf;
    const double f = factor(codec, width, height);
    if (f <= 0) return kInf;
    return ffmpeg_timeout_min * 60.0 * margin / f;
}

}  // namespace one6s
