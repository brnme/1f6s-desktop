// encode.h — ffmpeg 命令构造与体积估算(语义对齐网站 app/services/compressor.py)。
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/spec.h"

namespace one6s::encode {

// compressor.py:126-137 build_video_args。total_bitrate_kbps>0 时走码率模式
// (-b:v = 总码率 − 音频码率,下限 1k),否则 crf 模式(crf 为 null 且无码率时
// 两项都不输出,与 Python 行为一致)。
std::vector<std::string> videoArgs(const Spec& spec, const Eff& eff,
                                   double total_bitrate_kbps = 0.0);

// compressor.py:140-148 build_audio_args。无音轨 → ["-an"]。
std::vector<std::string> audioArgs(const Spec& spec, const Eff& eff, bool has_audio);

// compressor.py:151-153 twopass_total_bitrate = target_mb × 8192 / max(duration, 1)。
double twopassTotalBitrate(const Eff& eff, double duration_s);

// compressor.py:167-182 build_commands。两遍时返回 [pass1, pass2]:
// pass1 尾部是 ["-f","null","-"] —— 本项目唯一有意偏离(网站端为
// ["-f","mp4",os.devnull],两遍统计等价、不落盘更稳,AGENTS.md 已记录;
// 对拍时把向量里的三 token 等价归一后再比)。passlogfile 由调用方给全路径。
std::vector<std::vector<std::string>> buildCommands(const Spec& spec,
                                                    const std::string& src,
                                                    const std::string& dst,
                                                    const Eff& eff,
                                                    double duration_s,
                                                    bool has_audio,
                                                    const std::string& passlogfile);

// compressor.py:104-108 estimate_mb。target_mb 生效(非空非 0)时直接返回;
// 否则按每级总码率估算:round1(duration × kbps / 8192)。
// round1 复刻 Python round(x, 1) 的 half-even 语义(%.[n]f 正确舍入 +
// 转回 double),不是 std::round 的 half-away-from-zero。
double estimateMb(const std::string& level_id, double duration_s,
                  std::optional<int> target_mb = std::nullopt);

// compressor.py:156-164 output_path:<dir>/<stem>_1f6s.mp4;已存在则
// <stem>_1f6s.{i}.mp4(i 从 2 递增),永不覆盖。stem 与 Python Path.stem 同义
// ("a.tar.gz" → "a.tar")。
std::string outputPath(const Spec& spec, const std::string& dir,
                       const std::string& input_path);

}  // namespace one6s::encode
