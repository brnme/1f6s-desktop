// aboutpage.h — 「关于」页(M4)。
//
// 展示:应用版本(CMake 项目版本,APP_VERSION 编译定义)、ffmpeg 版本串
// (-version 首行)、规范版本(levels.json 顶层 version)。
// 启动即异步 GET https://1f6s.com/api/levels(3s 超时):成功且远程 version
// 与本地不同 → 页内提示条 + specUpdateFound 信号(主窗口同步状态栏);
// 失败静默——网站尚未上线,必须优雅降级。
#pragma once

#include <QString>
#include <QWidget>

#include "core/spec.h"
#include "jobs/engine.h"

class QLabel;
class QNetworkAccessManager;

class AboutPage : public QWidget {
    Q_OBJECT

public:
    AboutPage(const one6s::Spec& spec, one6s::jobs::EnginePaths engines,
              QWidget* parent = nullptr);

signals:
    // 远程规范版本与本地不同(只在能解析出非空远程版本时发)。
    void specUpdateFound(const QString& remote_version, const QString& local_version);

private:
    QString ffmpegVersion() const;  // `ffmpeg -version` 首行;失败 → "未知"
    void checkRemoteSpec();         // 异步;网络/解析失败一律静默

    QString appVersion_;
    QString localSpecVersion_;
    QString ffmpegPath_;
    QNetworkAccessManager* nam_ = nullptr;
    QLabel* updateLabel_ = nullptr;
};
