// engine.h — ffmpeg/ffprobe 引擎定位与编码器自检(UI 无关,可 headless 测试)。
//
// 定位顺序:优先主程序可执行文件同目录(QApplication::applicationDirPath(),
// 打包形态 —— 静态 ffmpeg 引擎随包分发,见 packaging/);找不到再退回系统
// PATH(QStandardPaths::findExecutable,开发形态)。
#pragma once

#include <QString>

namespace one6s::jobs {

struct EnginePaths {
    QString ffmpeg;    // 空串 = 未找到
    QString ffprobe;

    bool ok() const { return !ffmpeg.isEmpty() && !ffprobe.isEmpty(); }
};

// 按上述顺序定位;两件都找不到时对应字段为空串,调用方自行决定报错文案。
EnginePaths locateEngines();

// 跑 `ffmpeg -hide_banner -encoders`,确认 libx264/libx265/aac 都在列。
// 返回空串 = 全部可用;否则返回面向用户的中文错误文案(无法启动 / 退出码非零 /
// 缺少哪些编码器),启动时自检用。
QString verifyEncoders(const QString& ffmpeg_path);

}  // namespace one6s::jobs
