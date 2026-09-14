// splitter.cpp — 分割规划纯函数实现。
#include "core/splitter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <locale>
#include <map>
#include <sstream>

namespace one6s::splitter {

SplitPlan planSplit(double duration_s, double size_bytes, double tier_max_mb,
                    double precheck_max_s) {
    const double bitrate = size_bytes / std::max(duration_s, 1.0);
    double t = std::min(tier_max_mb * 1024.0 * 1024.0 * 0.95 / bitrate, precheck_max_s);
    t = std::max(t, 60.0);  // 段长下限 60s
    int parts = static_cast<int>(std::ceil(duration_s / t));
    if (parts < 1) parts = 1;  // t=inf(0 字节且预检不设限)时 ceil→0,至少给 1 段
    return {t, parts};
}

std::optional<Container> containerFor(std::string ext) {
    static const std::map<std::string, std::string> kMuxers = {
        {"mp4", "mp4"},      {"m4v", "mp4"},  {"mkv", "matroska"},
        {"webm", "webm"},    {"mov", "mov"},  {"avi", "avi"},
        {"flv", "flv"},      {"ts", "mpegts"}, {"m2ts", "mpegts"},
        {"wmv", "asf"},
    };
    // 容忍 ".MP4" 之类写法:去前导点 + 小写化。
    if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    auto it = kMuxers.find(ext);
    if (it == kMuxers.end()) return std::nullopt;
    return Container{it->second, ext};
}

std::vector<std::string> segmentCommand(const std::string& src,
                                        const std::string& out_pattern,
                                        const std::string& muxer,
                                        double segment_len_s) {
    std::string t_str;
    if (std::isfinite(segment_len_s) && segment_len_s == std::floor(segment_len_s) &&
        std::fabs(segment_len_s) < 1e15) {
        t_str = std::to_string(static_cast<long long>(segment_len_s));
    } else {
        std::ostringstream oss;
        oss.imbue(std::locale::classic());
        oss << segment_len_s;
        t_str = oss.str();
    }
    return {"ffmpeg", "-y", "-i", src, "-c", "copy", "-map", "0", "-f", "segment",
            "-segment_time", t_str, "-reset_timestamps", "1",
            "-segment_format", muxer, out_pattern};
}

std::string partName(const std::string& stem, int idx, int total,
                     const std::string& ext) {
    char idx_buf[16];
    char total_buf[16];
    std::snprintf(idx_buf, sizeof idx_buf, "%02d", idx);
    std::snprintf(total_buf, sizeof total_buf, "%02d", total);
    return stem + ".part" + idx_buf + "of" + total_buf + "." + ext;
}

std::vector<int> overLimit(const std::vector<double>& sizes, double limit_bytes) {
    std::vector<int> out;
    for (size_t i = 0; i < sizes.size(); ++i)
        if (sizes[i] > limit_bytes) out.push_back(static_cast<int>(i));
    return out;
}

}  // namespace one6s::splitter
