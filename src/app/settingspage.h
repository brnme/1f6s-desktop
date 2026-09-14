// settingspage.h — 「设置」页(M4)。
//
// 通用:语言(默认 en;可选 system/en/zh,写 QSettings,重启生效)。
// 转码:低进程优先级 / 线程上限 / 预设速度 —— 写 QSettings 并发
// runtimeOptionsChanged,主窗口把它们灌进 JobModel(对之后启动的任务即时生效)。
// 线程上限与预设速度是对网站命令的用户主动偏离(AGENTS.md「有意偏离」2/3 条),
// 默认配置(0 / slow)下命令与黄金向量逐 token 一致。
// 高级:自定义 ffmpeg/ffprobe 路径(默认空 = 引擎定位器行为不变,重启生效)。
#pragma once

#include <QWidget>

#include "jobs/jobmodel.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

namespace settings {

// QSettings → 运行时选项(main.cpp 启动与设置页变更共用同一读取逻辑)。
one6s::jobs::RuntimeOptions loadRuntimeOptions();

// 自定义引擎路径(默认空串 = 走内置定位;主程序负责校验可执行后覆盖)。
struct EnginePathOverrides {
    QString ffmpeg;
    QString ffprobe;
};
EnginePathOverrides loadEnginePathOverrides();

}  // namespace settings

class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent = nullptr);

signals:
    // 优先级/线程/预设任一变化后发出;主窗口读取 settings::loadRuntimeOptions()
    // 灌进 JobModel(进行中的任务不受影响)。
    void runtimeOptionsChanged();

private slots:
    void saveRuntimeOptions();   // 写 QSettings + 发 runtimeOptionsChanged

private:
    void saveEnginePaths();

    QComboBox* languageCombo_ = nullptr;
    QCheckBox* priorityCheck_ = nullptr;
    QSpinBox* threadsSpin_ = nullptr;
    QComboBox* presetCombo_ = nullptr;
    QLineEdit* ffmpegEdit_ = nullptr;
    QLineEdit* ffprobeEdit_ = nullptr;
};
