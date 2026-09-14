// probe.cpp — compressor.py:29-48 的 C++ 复刻(QProcess 同步版)。
#include "core/probe.h"

#include <QProcess>
#include <algorithm>
#include <cstdlib>
#include <nlohmann/json.hpp>

namespace one6s {
namespace {

using nlohmann::json;

double parseDuration(const json& fmt) {
    // Python: float(fmt.get("duration") or 0) — 缺失/空 → 0。
    // ffprobe 对个别容器会给出 "N/A" 之类字符串(Python 端会直接抛 ValueError),
    // 桌面端取 0 更稳(宁可当 0 让上层兜底,不在探测层崩)。
    if (!fmt.contains("duration")) return 0;
    const json& d = fmt.at("duration");
    if (d.is_number()) return d.get<double>();
    if (d.is_string()) {
        const std::string s = d.get<std::string>();
        if (s.empty()) return 0;
        try {
            size_t pos = 0;
            const double v = std::stod(s, &pos);
            if (pos != s.size()) return 0;  // "N/A" 等非纯数字
            return v;
        } catch (const std::exception&) {
            return 0;
        }
    }
    return 0;
}

}  // namespace

ProbeResult probe(const QString& ffprobe_path, const QString& file_path) {
    QProcess p;
    p.start(ffprobe_path, QStringList{"-v", "quiet", "-print_format", "json",
                                      "-show_format", "-show_streams", file_path});
    if (!p.waitForStarted(10000))
        throw std::runtime_error("ffprobe failed: cannot start " +
                                 ffprobe_path.toStdString());
    if (!p.waitForFinished(120000)) {  // 与网站 subprocess timeout=120 对齐
        p.kill();
        p.waitForFinished(5000);
        throw std::runtime_error("ffprobe failed: timeout after 120s");
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        QByteArray err = p.readAllStandardError().right(500);  // Python stderr[-500:]
        throw std::runtime_error("ffprobe failed: " + err.toStdString());
    }

    json info = json::parse(p.readAllStandardOutput().constData(), nullptr, false);
    if (info.is_discarded() || !info.is_object()) info = json::object();  // 空输出 → {}

    ProbeResult r;
    r.duration_s = parseDuration(info.value("format", json::object()));

    const json streams =
        info.contains("streams") && info.at("streams").is_array()
            ? info.at("streams")
            : json::array();
    const json* vstream = nullptr;
    for (const json& s : streams) {
        const std::string type = s.value("codec_type", "");
        if (!vstream && type == "video") vstream = &s;
        if (type == "audio") r.has_audio = true;
    }
    if (vstream) {
        // Python: int(vstream.get("width") or 0) — 缺失/null/0 → 0。
        if (vstream->contains("width") && vstream->at("width").is_number())
            r.width = vstream->at("width").get<int>();
        if (vstream->contains("height") && vstream->at("height").is_number())
            r.height = vstream->at("height").get<int>();
        if (vstream->contains("codec_name") && vstream->at("codec_name").is_string())
            r.codec = vstream->at("codec_name").get<std::string>();
    }
    if (r.codec.empty()) r.codec = "unknown";  // Python `or "unknown"`
    return r;
}

}  // namespace one6s
