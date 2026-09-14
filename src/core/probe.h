// probe.h — ffprobe 元数据探测(语义对齐网站 compressor.py:18-48)。
#pragma once

#include <QString>
#include <stdexcept>
#include <string>

namespace one6s {

struct ProbeResult {
    double duration_s = 0;
    int width = 0;
    int height = 0;
    bool has_audio = false;
    // ffprobe codec_name(h264/hevc/vp9/av1/…);缺失时 "unknown"。
    std::string codec = "unknown";
};

// 同步执行 `ffprobe -v quiet -print_format json -show_format -show_streams <file>`,
// 120s 超时。启动失败、非零退出或超时抛 std::runtime_error("ffprobe failed: …"),
// 文案前缀与 compressor.py 一致。
ProbeResult probe(const QString& ffprobe_path, const QString& file_path);

}  // namespace one6s
