// compresspage.h — 「压缩」页:多选文件入队,任务列表卡片(文件名/等级/进度/
// 取消/打开文件夹),队列 FIFO 串行跑。文案中文硬编码,M4 统一换 i18n key。
#pragma once

#include <QMap>
#include <QWidget>

#include "core/spec.h"
#include "jobs/engine.h"
#include "jobs/jobmodel.h"

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;

class CompressPage : public QWidget {
    Q_OBJECT

public:
    CompressPage(const one6s::Spec& spec, one6s::jobs::EnginePaths engines,
                 one6s::jobs::JobModel* model, QWidget* parent = nullptr);

private slots:
    void addVideos();          // 多选 → 逐个 probe → 入队
    void refreshLevelOptions();
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
    QString levelDisplay(const QString& level_id) const;

    one6s::Spec spec_;
    one6s::jobs::EnginePaths engines_;
    one6s::jobs::JobModel* model_;  // 主窗口持有,页面只引用

    QComboBox* levelCombo_ = nullptr;
    QLabel* targetLabel_ = nullptr;
    QSpinBox* targetSpin_ = nullptr;
    QPushButton* addBtn_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    QMap<quint64, int> rows_;   // 任务 id → 表行号(随删除重排)
};
