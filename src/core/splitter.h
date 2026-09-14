// splitter.h — 超大文件分割规划(纯规划,不跑 ffmpeg;桌面端特有,网站无对应)。
//
// 公式:bitrate = size / max(duration, 1);
//       T = min(tierMaxMb × 1024 × 1024 × 0.95 ÷ bitrate, precheckMaxSec);
//       T 下限 60s;parts = ceil(duration / T)。
// precheckMaxSec 来自 precheck::maxInputSeconds(可为无穷大)。
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace one6s::splitter {

struct SplitPlan {
    double segment_len_s = 0;  // 每段目标时长(秒);无码率信息且无预检上限时可为 inf
    int parts = 0;             // 段数,至少 1
};

// 按档位体积上限(留 5% 余量)与预检时长上限规划分割。
SplitPlan planSplit(double duration_s, double size_bytes, double tier_max_mb,
                    double precheck_max_s);

struct Container {
    std::string muxer;     // -segment_format / -f 用
    std::string keep_ext;  // 输出分段沿用的扩展名(小写,不带点)
};

// 容器映射:mp4/m4v→mp4,mkv→matroska,webm→webm,mov→mov,avi→avi,flv→flv,
// ts/m2ts→mpegts,wmv→asf。大小写不敏感、容忍前导点;未知扩展名 → nullopt。
std::optional<Container> containerFor(std::string ext);

// -f segment 切割命令(与网站风格同构,token 顺序固定):
// ["ffmpeg","-y","-i",src,"-c","copy","-map","0","-f","segment",
//  "-segment_time",T,"-reset_timestamps","1","-segment_format",muxer,outPattern]
std::vector<std::string> segmentCommand(const std::string& src,
                                        const std::string& out_pattern,
                                        const std::string& muxer,
                                        double segment_len_s);

// 分段文件名:"{stem}.part{idx:02d}of{total:02d}.{ext}"(total≥100 时自然扩为三位)。
// idx 从 1 计。
std::string partName(const std::string& stem, int idx, int total,
                     const std::string& ext);

// 后验重分割:返回 size 超过 limit_bytes(严格大于)的段下标(0 起)。
std::vector<int> overLimit(const std::vector<double>& sizes, double limit_bytes);

}  // namespace one6s::splitter
