// precheck.h — 超长输入超时预检(语义对齐网站 compressor.py:53-89,
// 因子表 assets/spec/precheck_factors.json 由 sync_spec.sh 从 tiers.json 抽取)。
#pragma once

#include <limits>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

class QString;  // 前置声明:load(QString) 重载用,实现在 .cpp(本头保持不引 Qt)

namespace one6s {

struct Precheck {
    bool enabled = true;
    double margin = 0.8;  // 预留安全余量;json 缺该键时的默认值
    // factors[编码族][分辨率桶] = 机器秒/输入秒。
    std::map<std::string, std::map<std::string, double>> factors;

    static Precheck parse(const nlohmann::json& j);
    // QFile 读取(Windows 非 ASCII 路径安全),语义同 Spec::load。
    static Precheck load(const QString& path);

    // 机器秒/输入秒:分辨率桶(按 max(w,h))× 编码族(h264/hevc/other)。
    // 回退链与 compressor.py:53-68 一致:缺族 → factors["other"];
    // 缺桶或因子为 0 → factors["other"]["p2160"];再缺 → 0.9(宁可保守)。
    double factor(const std::string& codec, int width, int height) const;

    // 该输入可接受的最大输入时长(秒)= timeout_min × 60 × margin ÷ 因子。
    // 预检关闭或因子 ≤ 0 → 无穷大(不设限)。timeout_min 默认 180(3 小时)。
    double maxInputSeconds(int width, int height, const std::string& codec,
                           double ffmpeg_timeout_min = 180.0) const;

    static constexpr double kInf = std::numeric_limits<double>::infinity();
};

}  // namespace one6s
