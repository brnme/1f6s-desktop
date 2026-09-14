// 主窗口(M2):选视频 → 选等级 → 压缩(进度/取消)→ 打开产物所在文件夹。
// 本阶段文案中文硬编码,M4 统一换 i18n key(assets/i18n/{en,zh}.json)。
#pragma once

#include <QMainWindow>
#include <QString>

#include <optional>

#include "core/probe.h"
#include "core/spec.h"
#include "jobs/engine.h"
#include "jobs/jobmodel.h"

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const one6s::Spec& spec, one6s::jobs::EnginePaths engines,
                        QWidget* parent = nullptr);

private slots:
    void pickVideo();
    void refreshLevelOptions();
    void startCompression();
    void cancelRunning();
    void onJobProgress(quint64 id, int percent);
    void onJobFinished(quint64 id, one6s::jobs::JobState state, const QString& error);
    void openOutputFolder();

private:
    QWidget* buildCentral();
    void setRunningUi(bool running);
    QString currentLevelId() const;
    void showFatal(const QString& message);  // 引擎/规范问题:置红并禁用开始

    one6s::Spec spec_;
    one6s::jobs::EnginePaths engines_;
    one6s::jobs::JobModel model_;

    QString inputPath_;
    QString lastOutput_;
    std::optional<one6s::ProbeResult> probe_;
    quint64 jobId_ = 0;  // 当前/最近一次入队的作业;0 = 无

    QLabel* fileLabel_ = nullptr;
    QLabel* infoLabel_ = nullptr;
    QComboBox* levelCombo_ = nullptr;
    QLabel* targetLabel_ = nullptr;
    QSpinBox* targetSpin_ = nullptr;
    QLabel* estimateLabel_ = nullptr;
    QLabel* warnLabel_ = nullptr;
    QPushButton* pickBtn_ = nullptr;
    QPushButton* startBtn_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;
    QPushButton* openBtn_ = nullptr;
    QProgressBar* bar_ = nullptr;
    QLabel* statusLabel_ = nullptr;
};
