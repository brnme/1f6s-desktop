#include "jobs/engine.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace one6s::jobs {
namespace {

// 引擎随包分发的文件名(Linux/macOS 同名,Windows 带 .exe 后缀——
// findBesideApp 的 QFileInfo::exists 不认无后缀文件;打包布局见 packaging/README.md)。
#ifdef Q_OS_WIN
constexpr const char* kFfmpegName = "ffmpeg.exe";
constexpr const char* kFfprobeName = "ffprobe.exe";
#else
constexpr const char* kFfmpegName = "ffmpeg";
constexpr const char* kFfprobeName = "ffprobe";
#endif

QString findBesideApp(const char* name) {
    const QString dir = QCoreApplication::applicationDirPath();
    if (dir.isEmpty()) return {};
    const QString candidate = dir + QLatin1Char('/') + QLatin1String(name);
    const QFileInfo info(candidate);
    // 须是真实可执行文件,排除 PATH 解析与同名占位脚本误判。
    return (info.exists() && info.isFile() && info.isExecutable())
               ? candidate
               : QString();
}

}  // namespace

EnginePaths locateEngines() {
    EnginePaths e;
    e.ffmpeg = findBesideApp(kFfmpegName);
    e.ffprobe = findBesideApp(kFfprobeName);
    if (e.ffmpeg.isEmpty()) e.ffmpeg = QStandardPaths::findExecutable(kFfmpegName);
    if (e.ffprobe.isEmpty()) e.ffprobe = QStandardPaths::findExecutable(kFfprobeName);
    return e;
}

QString verifyEncoders(const QString& ffmpeg_path) {
    if (ffmpeg_path.isEmpty())
        return QStringLiteral("未找到 ffmpeg,请重新安装本程序。");

    QProcess proc;
    proc.start(ffmpeg_path, {QStringLiteral("-hide_banner"),
                             QStringLiteral("-encoders")});
    if (!proc.waitForStarted(10000))
        return QStringLiteral("无法启动 ffmpeg(%1)。").arg(ffmpeg_path);
    if (!proc.waitForFinished(30000))
        return QStringLiteral("ffmpeg -encoders 查询超时(%1)。").arg(ffmpeg_path);

    const QByteArray out = proc.readAllStandardOutput();
    // 逐词匹配,避免描述文本里偶然出现的子串造成误判。
    const std::pair<const char*, const char*> required[] = {
        {"libx264", "H.264(libx264)"},
        {"libx265", "H.265(libx265)"},
        {"aac", "AAC"},
    };
    QStringList missing;
    for (const auto& [name, label] : required) {
        const QRegularExpression re(
            QStringLiteral("\\b%1\\b").arg(QString::fromLatin1(name)));
        if (!re.match(QString::fromUtf8(out)).hasMatch())
            missing << QString::fromUtf8(label);
    }
    if (!missing.isEmpty())
        return QStringLiteral("当前 ffmpeg 缺少必需编码器:%1。请更换内置引擎或升级 ffmpeg。")
            .arg(missing.join(QStringLiteral("、")));
    return {};
}

}  // namespace one6s::jobs
