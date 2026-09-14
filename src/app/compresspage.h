// compresspage.h — 「压缩」页:多选文件入队,任务列表卡片(文件名/等级/进度/
// 取消/打开文件夹),队列 FIFO 串行跑。
// M4:三参数模式(等级/场景/微调,对齐网站 compress.html 的 mode-radio)+
// 全部文案走 i18n。三模式统一产出 (level, overrides) 走既有 resolve→
// buildCommands 流水线;微调空选项 = 继承基线等级(白名单语义)。
#pragma once

#include <QMap>
#include <QWidget>

#include <nlohmann/json.hpp>

#include "core/spec.h"
#include "jobs/engine.h"
#include "jobs/jobmodel.h"

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;

class CompressPage : public QWidget {
    Q_OBJECT

public:
    CompressPage(const one6s::Spec& spec, one6s::jobs::EnginePaths engines,
                 one6s::jobs::JobModel* model, QWidget* parent = nullptr);

private slots:
    void addVideos();          // 多选 → 逐个 probe → 入队
    void refreshLevelOptions();  // 等级模式:L7 时目标体积可见
    void refreshModeHint();      // 模式切换 → 提示行换文案
    void onTaskAdded(quint64 id);
    void onTaskRemoved(quint64 id);
    void onTaskUpdated(quint64 id);
    void onProgress(quint64 id, int percent);
    void onFinished(quint64 id, one6s::jobs::JobState state, const QString& error);

private:
    int rowOf(quint64 id) const;
    void insertRow(quint64 id);
    void refreshRow(quint64 id);
    QWidget* makeActions(quint64 id);
    // 等级显示名:i18n level.{id}.name 优先,缺失回退 levels.json 内嵌名。
    QString levelName(const QString& level_id) const;
    QString levelDisplay(const QString& level_id) const;
    // 按当前模式算出 (level, overrides):等级模式(L7 带目标体积)/
    // 场景模式(kScenarios 映射)/ 微调模式(空选项不写 overrides = 继承)。
    void resolveCurrentSelection(QString* level, nlohmann::json* overrides) const;
    void populateLevelCombo(QComboBox* combo, bool with_scene_tooltip) const;

    one6s::Spec spec_;
    one6s::jobs::EnginePaths engines_;
    one6s::jobs::JobModel* model_;  // 主窗口持有,页面只引用

    QTabWidget* modeTabs_ = nullptr;
    // 等级模式
    QComboBox* levelCombo_ = nullptr;
    QLabel* targetLabel_ = nullptr;
    QSpinBox* targetSpin_ = nullptr;
    // 场景模式
    QComboBox* scenarioCombo_ = nullptr;
    // 微调模式
    QComboBox* finetuneBase_ = nullptr;
    QComboBox* finetuneResolution_ = nullptr;
    QComboBox* finetuneInterval_ = nullptr;
    QComboBox* finetuneGray_ = nullptr;
    QComboBox* finetuneAudio_ = nullptr;
    QLabel* hintLabel_ = nullptr;   // 当前模式的提示行(upload.mode.*.hint)
    QPushButton* addBtn_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    QMap<quint64, int> rows_;   // 任务 id → 表行号(随删除重排)
};
