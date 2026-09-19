// splitpage.h — 「分割」页:向导式流复制分割(选文件 → 档位 → 预览 → 执行
// → 结果)。档位上限来自 assets/spec/tiers_limits.json(sync_spec.sh 同步副本,
// 不硬编码);预检因子来自 assets/spec/precheck_factors.json。
// 文案走 assets/i18n(M4 起,one6s::i18n::t 查询)。
// M5:压缩任务占用队列时本页入口灰化(refreshGate);开始按钮额外要求
// 无未终结的分割任务,杜绝连点重复提交。
#pragma once

#include <QMap>
#include <QWidget>

#include <optional>

#include "core/precheck.h"
#include "core/probe.h"
#include "core/splitter.h"
#include "jobs/engine.h"
#include "jobs/jobmodel.h"

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;

class SplitPage : public QWidget {
    Q_OBJECT

public:
    SplitPage(one6s::jobs::EnginePaths engines, one6s::jobs::JobModel* model,
              QWidget* parent = nullptr);

private slots:
    void pickVideo();
    void refreshPreview();   // 文件/档位任一变化 → 重算预览
    void startSplit();
    void onTaskAdded(quint64 id);
    void onTaskRemoved(quint64 id);
    void onTaskUpdated(quint64 id);
    void onProgress(quint64 id, int percent);
    void onFinished(quint64 id, one6s::jobs::JobState state, const QString& error);
    void openFolder();
    void openUploadPage();

private:
    bool loadTierConfig(QString& error);   // tiers_limits.json + precheck_factors.json
    double currentTierMaxMb() const;
    void setStartEnabled(bool on);
    void refreshGate();    // 压缩任务占用队列 → 灰化本页入口并提示
    void resetResultUi();

    one6s::jobs::EnginePaths engines_;
    one6s::jobs::JobModel* model_;  // 主窗口持有,页面只引用
    one6s::Precheck precheck_;
    QMap<QString, double> tierMb_;  // "free"/"pro"/"org" → 上限 MB
    bool configOk_ = false;

    std::optional<one6s::ProbeResult> probe_;
    QString inputPath_;
    one6s::splitter::SplitPlan plan_;   // 当前预览的规划
    quint64 lastSplitId_ = 0;           // 最近一次入队的分割任务

    QPushButton* pickBtn_ = nullptr;
    QLabel* fileLabel_ = nullptr;
    QComboBox* tierCombo_ = nullptr;
    QSpinBox* customSpin_ = nullptr;
    QLabel* capLabel_ = nullptr;        // 预检时长上限
    QTableWidget* previewTable_ = nullptr;
    QLabel* estimateLabel_ = nullptr;   // 预计点数合计
    QLabel* gateLabel_ = nullptr;       // 压缩占用时的一行灰化原因提示
    QPushButton* startBtn_ = nullptr;
    QProgressBar* bar_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QTableWidget* resultTable_ = nullptr;
    QLabel* pointsLabel_ = nullptr;
    QLabel* warnLabel_ = nullptr;
    QPushButton* openDirBtn_ = nullptr;
    QPushButton* uploadBtn_ = nullptr;
};
