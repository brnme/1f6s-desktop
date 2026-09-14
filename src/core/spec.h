// spec.h — levels.json 规范解析与微调参数合成(语义对齐网站 app/services/params.py)。
//
// 等级参数一律从 levels.json 读入(路径由调用方传入),不硬编码;
// 仅微调白名单(ALLOWED_*)与错误文案是写死的——它们与 params.py 同源同构。
// Python truthiness 语义(null/""/int 0 → 静默丢弃继承基础等级;"0" 字符串
// 是 truthy → 进入校验并报越界)在 py_truthy()/py_to_int() 中逐条复刻,
// 黄金向量 vectors/vectors.json 是唯一验收 oracle。
#pragma once

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace one6s {

// 与网站 ValueError 对应;message 必须与 params.py 逐字一致(对拍依赖)。
struct SpecError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// levels.json 中单个等级的字段(与 vectors.json eff 字段一一对应)。
struct Level {
    std::string id;
    std::string name;           // 显示名,如 "智能调优级"(不参与参数对拍,M2 起供 UI 用)
    std::string interval;       // 原始字段,如 "1/6"(spec 值,非白名单 token)
    std::string scale;          // 如 "-2:480"
    std::string color;          // "color" | "gray"
    std::string codec;          // "libx264" | "libx265"
    std::optional<int> crf;     // null → nullopt(码率模式)
    std::string audio_bitrate;  // "20k" | "8k"
    int audio_rate = 0;
    std::optional<std::string> tune;
    std::optional<std::string> extra;
    bool twopass = false;
};

// levels.json 整体:等级表 + encode 所需的 spec 级常量(preset/pix_fmt/音频)。
struct Spec {
    std::map<std::string, Level> levels;
    std::string preset = "slow";
    std::string pixel_format = "yuv420p";
    std::string audio_codec = "aac";
    int audio_channels = 1;
    std::string output_suffix = "_1f6s.mp4";
    std::string default_level;

    bool has_level(const std::string& id) const { return levels.count(id) != 0; }
    // 未知等级抛 SpecError("unknown level {id}")。
    const Level& level(const std::string& id) const;

    static Spec parse(const nlohmann::json& j);
    static Spec load(const std::string& path);  // 路径由调用方传入
};

// 微调白名单(params.py ALLOWED_*;纯枚举允许写死,levels.json 无此表)。
extern const std::map<std::string, std::string> kAllowedScales;   // "720p" → "-2:720"
extern const std::map<int, std::string> kAllowedIntervals;        // 4 → "1/4"
extern const std::map<std::string, std::pair<std::string, int>> kAllowedAudio;  // "20k" → ("20k", 22050)
inline constexpr int kTargetMbMin = 1;
inline constexpr int kTargetMbMax = 500;

// params.py parse_interval:"1/6" → 6;不合式 → 6。
int parseInterval(const std::string& raw);
// 白名单 interval → vf token:4 → "1/4";不在白名单抛 std::out_of_range。
const std::string& intervalToken(int interval);

// resolve 输出的有效参数集(字段名与 vectors.json eff 对齐)。
struct Eff {
    std::string level;
    int interval = 6;
    std::string scale;
    bool gray = false;
    std::string codec;
    std::optional<int> crf;
    std::string audio_bitrate;
    int audio_rate = 0;
    std::optional<std::string> tune;
    std::optional<std::string> extra;
    bool twopass = false;
    std::optional<int> target_mb;
};

// params.py resolve 的逐字复刻:基础等级 + 校验过的 overrides → 有效参数集。
// overrides 用 nlohmann::json 传入以保留类型(int 0 falsy 与 "0" truthy 不同)。
Eff resolve(const Spec& spec, const std::string& level_id,
            const nlohmann::json& overrides = nlohmann::json::object());

// Python truthiness(用于 JSON 值):null/false/0/0.0/""/空容器 → false。
bool py_truthy(const nlohmann::json& v);
// Python int() 的受控复刻:bool/整数/浮点(向零截断)/整数字符串可转换;
// 其余(含溢出、非整数字符串)返回 false → 由调用方落到相应 "… out of range"。
bool py_to_int(const nlohmann::json& v, int& out);

}  // namespace one6s
