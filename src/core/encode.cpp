// encode.cpp — compressor.py 命令构造/估算的 C++ 复刻。
// token 顺序逐条对照 compressor.py:111-182,不得重排。
#include "core/encode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>

namespace one6s::encode {
namespace {

using Tokens = std::vector<std::string>;

// compressor.py:111-115 _vf:fps=1/{interval} → format=gray|yuv420p → scale={scale},
// 顺序不可变。白名单值恰为 "1/"+N,与 ALLOWED_INTERVALS 查表等价。
std::string vfChain(const Eff& eff) {
    std::string vf = "fps=" + intervalToken(eff.interval);
    vf += eff.gray ? ",format=gray" : ",format=yuv420p";
    vf += ",scale=" + eff.scale;
    return vf;
}

// "20k" → 20(Python int(eff["audio_bitrate"].rstrip("k")))。
int audioBitrateKbps(const std::string& audio_bitrate) {
    std::string s = audio_bitrate;
    while (!s.empty() && s.back() == 'k') s.pop_back();
    return std::stoi(s);
}

// Python round(x, 1):按二进制精确十进制展开做 correct rounding(half-even)。
// glibc/libstdc++ 的 %.[n]f 正是这种正确舍入,再 strtod 转回即与 Python 逐位一致;
// std::round 是 half-away-from-zero,边缘值会错,禁止用。
double round1(double v) {
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::fixed << std::setprecision(1) << v;
    return std::strtod(oss.str().c_str(), nullptr);
}

// 段长转 -segment_time 的字符串:整数值给干净整数串,分数值走默认浮点格式。
std::string formatSeconds(double s) {
    if (std::isfinite(s) && s == std::floor(s) && std::fabs(s) < 1e15)
        return std::to_string(static_cast<long long>(s));
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << s;
    return oss.str();
}

// compressor.py:118-123 _common_args
Tokens commonArgs(const Spec& spec, const Eff& eff) {
    Tokens args{"-preset", spec.preset};
    if (eff.tune && !eff.tune->empty()) {  // Python truthiness:空串视为无 tune
        args.push_back("-tune");
        args.push_back(*eff.tune);
    }
    args.push_back("-pix_fmt");
    args.push_back(spec.pixel_format);
    return args;
}

}  // namespace

Tokens videoArgs(const Spec& spec, const Eff& eff, double total_bitrate_kbps) {
    Tokens args{"-c:v", eff.codec};
    if (eff.extra && !eff.extra->empty()) {  // Python truthiness:空串视为无 extra
        args.push_back(eff.codec == "libx265" ? "-x265-params" : "-x264-params");
        args.push_back(*eff.extra);
    }
    if (eff.crf.has_value()) {
        args.push_back("-crf");
        args.push_back(std::to_string(*eff.crf));
    } else if (total_bitrate_kbps != 0.0) {
        const int audio_kbps = audioBitrateKbps(eff.audio_bitrate);
        long long bv = static_cast<long long>(total_bitrate_kbps) - audio_kbps;
        if (bv < 1) bv = 1;  // Python max(int(total)-audio_kbps, 1)
        args.push_back("-b:v");
        args.push_back(std::to_string(bv) + "k");
    }
    Tokens c = commonArgs(spec, eff);
    args.insert(args.end(), c.begin(), c.end());
    args.push_back("-vf");
    args.push_back(vfChain(eff));
    return args;
}

Tokens audioArgs(const Spec& spec, const Eff& eff, bool has_audio) {
    if (!has_audio) return {"-an"};
    return {"-c:a", spec.audio_codec,
            "-b:a", eff.audio_bitrate,
            "-ar", std::to_string(eff.audio_rate),
            "-ac", std::to_string(spec.audio_channels)};
}

double twopassTotalBitrate(const Eff& eff, double duration_s) {
    const double tmb = eff.target_mb.has_value() ? static_cast<double>(*eff.target_mb) : 0.0;
    return tmb * 8192.0 / std::max(duration_s, 1.0);
}

std::vector<Tokens> buildCommands(const Spec& spec, const std::string& src,
                                  const std::string& dst, const Eff& eff,
                                  double duration_s, bool has_audio,
                                  const std::string& passlogfile) {
    const Tokens base{"ffmpeg", "-y", "-i", src};
    if (eff.twopass) {
        const double total = twopassTotalBitrate(eff, duration_s);
        const Tokens video = videoArgs(spec, eff, total);
        const Tokens audio = audioArgs(spec, eff, has_audio);
        Tokens cmd1 = base;
        cmd1.insert(cmd1.end(), {"-pass", "1", "-passlogfile", passlogfile});
        cmd1.insert(cmd1.end(), video.begin(), video.end());
        cmd1.insert(cmd1.end(), audio.begin(), audio.end());
        // 有意偏离:网站端为 ["-f","mp4",os.devnull];桌面端 -f null - 不落盘更稳。
        cmd1.insert(cmd1.end(), {"-f", "null", "-"});
        Tokens cmd2 = base;
        cmd2.insert(cmd2.end(), {"-pass", "2", "-passlogfile", passlogfile});
        cmd2.insert(cmd2.end(), video.begin(), video.end());
        cmd2.insert(cmd2.end(), audio.begin(), audio.end());
        cmd2.push_back(dst);
        return {cmd1, cmd2};
    }
    Tokens cmd = base;
    const Tokens video = videoArgs(spec, eff);
    const Tokens audio = audioArgs(spec, eff, has_audio);
    cmd.insert(cmd.end(), video.begin(), video.end());
    cmd.insert(cmd.end(), audio.begin(), audio.end());
    cmd.push_back(dst);
    return {cmd};
}

// 与网站 compressor.py:14-16 _EST_KBPS 同源同构(levels.json 无该字段,允许写死)。
double estimateMb(const std::string& level_id, double duration_s,
                  std::optional<int> target_mb) {
    if (target_mb.has_value() && *target_mb != 0)  // Python `if target_mb:` truthiness
        return static_cast<double>(*target_mb);
    static const std::map<std::string, int> kEstKbps = {
        {"L1", 47}, {"L2", 38}, {"L3", 29}, {"L4", 25},
        {"L5", 22}, {"L6", 16}, {"L8", 10},
    };
    int kbps = 30;  // 缺省(L7 及未知等级)
    if (auto it = kEstKbps.find(level_id); it != kEstKbps.end()) kbps = it->second;
    return round1(duration_s * kbps / 8192.0);
}

std::string outputPath(const Spec& spec, const std::string& dir,
                       const std::string& input_path) {
    namespace fs = std::filesystem;
    // Python Path(...).stem:"a.tar.gz" → "a.tar";隐藏文件名保留整段。
    const std::string stem = fs::path(input_path).stem().string();
    const std::string suffix = spec.output_suffix;
    // SUFFIX[:-4] 去掉 ".mp4" 四字符,得 "_1f6s"(Python output_path 的重名续号前缀)。
    const std::string stem_part =
        suffix.size() >= 4 ? suffix.substr(0, suffix.size() - 4) : suffix;
    fs::path out = fs::path(dir) / (stem + suffix);
    int i = 2;
    while (fs::exists(out)) {
        out = fs::path(dir) / (stem + stem_part + "." + std::to_string(i) + ".mp4");
        ++i;
    }
    return out.string();
}

}  // namespace one6s::encode
