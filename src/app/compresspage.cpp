// compresspage.cpp — 「压缩」页实现(M2 单文件流升级为队列形态)。
#include "app/compresspage.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "core/encode.h"
#include "core/probe.h"

namespace {

constexpr const char kVideoFilter[] =
    "视频文件 (*.mp4 *.mkv *.mov *.avi *.webm *.m4v *.flv *.ts *.wmv);;"
    "所有文件 (*)";

// 红线警示:与网站端口径一致(讲解/幻灯片类适用;体育/艺术/软件操作演示不适用)。
constexpr const char kRedlineWarning[] =
    "注意:1f6s 抽帧压缩面向讲解、培训、幻灯片类内容;"
    "体育、艺术视频,以及光标操作很重要的软件演示不适用,压缩后无法正常观看。";

QString stateText(one6s::jobs::JobState state) {
    using one6s::jobs::JobState;
    switch (state) {
        case JobState::Queued: return QStringLiteral("排队中");
        case JobState::Running: return QStringLiteral("压缩中");
        case JobState::Done: return QStringLiteral("完成");
        case JobState::Cancelled: return QStringLiteral("已取消");
        case JobState::Failed: return QStringLiteral("失败");
    }
    return QStringLiteral("未知");
}

}  // namespace

CompressPage::CompressPage(const one6s::Spec& spec, one6s::jobs::EnginePaths engines,
                           one6s::jobs::JobModel* model, QWidget* parent)
    : QWidget(parent), spec_(spec), engines_(engines), model_(model) {
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);
    layout->setContentsMargins(16, 16, 16, 16);

    // --- 顶行:添加视频 + 等级选择(L7 出目标体积) ---
    auto* topRow = new QHBoxLayout;
    addBtn_ = new QPushButton(QStringLiteral("添加视频…"), this);
    connect(addBtn_, &QPushButton::clicked, this, &CompressPage::addVideos);
    topRow->addWidget(addBtn_);

    topRow->addWidget(new QLabel(QStringLiteral("压缩等级:"), this));
    levelCombo_ = new QComboBox(this);
    connect(levelCombo_, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshLevelOptions(); });
    topRow->addWidget(levelCombo_, /*stretch=*/1);

    targetLabel_ = new QLabel(QStringLiteral("目标体积 (MB):"), this);
    targetSpin_ = new QSpinBox(this);
    targetSpin_->setRange(1, 500);   // spec 白名单边界 kTargetMbMin/Max
    targetSpin_->setValue(8);
    targetSpin_->setSuffix(QStringLiteral(" MB"));
    connect(targetSpin_, &QSpinBox::valueChanged, this,
            [this](int) { refreshLevelOptions(); });
    topRow->addWidget(targetLabel_);
    topRow->addWidget(targetSpin_);
    layout->addLayout(topRow);

    // --- 任务列表卡片 ---
    table_ = new QTableWidget(this);
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({QStringLiteral("文件"), QStringLiteral("等级"),
                                       QStringLiteral("状态"), QStringLiteral("进度"),
                                       QStringLiteral("操作")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(true);
    layout->addWidget(table_, /*stretch=*/1);

    // --- 状态行 + 红线警示 ---
    statusLabel_ = new QLabel(QStringLiteral("就绪。选择视频加入队列,任务按顺序串行执行。"), this);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    auto* warn = new QLabel(QString::fromUtf8(kRedlineWarning), this);
    warn->setWordWrap(true);
    warn->setStyleSheet(QStringLiteral("color: #e5c07b;"));
    layout->addWidget(warn);

    // --- 队列信号接线 ---
    connect(model_, &one6s::jobs::JobModel::taskAdded, this,
            &CompressPage::onTaskAdded);
    connect(model_, &one6s::jobs::JobModel::taskRemoved, this,
            &CompressPage::onTaskRemoved);
    connect(model_, &one6s::jobs::JobModel::taskUpdated, this,
            &CompressPage::onTaskUpdated);
    connect(model_, &one6s::jobs::JobModel::jobProgress, this,
            &CompressPage::onProgress);
    connect(model_, &one6s::jobs::JobModel::jobFinished, this,
            &CompressPage::onFinished);

    refreshLevelOptions();
}

void CompressPage::refreshLevelOptions() {
    if (levelCombo_->count() == 0) {
        // std::map 按 id 字典序 → L1..L8 天然有序;显示 "L4 · 智能调优级" 形态。
        for (const auto& [id, level] : spec_.levels) {
            const QString text = level.name.empty()
                                     ? QString::fromStdString(id)
                                     : QStringLiteral("%1 · %2")
                                           .arg(QString::fromStdString(id),
                                                QString::fromStdString(level.name));
            levelCombo_->addItem(text, QString::fromStdString(id));
        }
        const int idx = levelCombo_->findData(
            QString::fromStdString(spec_.default_level));
        if (idx >= 0) levelCombo_->setCurrentIndex(idx);
    }
    const bool is_l7 = levelCombo_->currentData().toString() == QLatin1String("L7");
    targetLabel_->setVisible(is_l7);
    targetSpin_->setVisible(is_l7);
}

QString CompressPage::levelDisplay(const QString& level_id) const {
    const auto it = spec_.levels.find(level_id.toStdString());
    if (it == spec_.levels.end() || it->second.name.empty()) return level_id;
    return QStringLiteral("%1 · %2")
        .arg(level_id, QString::fromStdString(it->second.name));
}

void CompressPage::addVideos() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("选择视频(可多选)"), QString(),
        QString::fromUtf8(kVideoFilter));
    if (files.isEmpty()) return;

    if (!engines_.ok() || engines_.ffprobe.isEmpty()) {
        statusLabel_->setStyleSheet(QStringLiteral("color: #e06c75;"));
        statusLabel_->setText(QStringLiteral("未找到 ffmpeg/ffprobe,无法读取视频信息。"));
        return;
    }

    QStringList failed;
    int added = 0;
    for (const QString& path : files) {
        one6s::ProbeResult r;
        try {
            r = one6s::probe(engines_.ffprobe, path);
        } catch (const std::exception& e) {
            failed.append(QStringLiteral("%1(%2)")
                              .arg(QFileInfo(path).fileName(),
                                   QString::fromUtf8(e.what())));
            continue;
        }
        // 输出与源同目录:<stem>_1f6s.mp4,已存在则续号,永不覆盖(core 的 outputPath)。
        const QString out = QString::fromStdString(one6s::encode::outputPath(
            spec_, QFileInfo(path).absolutePath().toStdString(),
            path.toStdString()));
        const QString level = levelCombo_->currentData().toString();
        nlohmann::json overrides = nlohmann::json::object();
        if (level == QLatin1String("L7")) overrides["target_mb"] = targetSpin_->value();
        model_->enqueue(path, out, level, overrides, r.duration_s, r.has_audio);
        ++added;
    }

    statusLabel_->setStyleSheet({});
    if (failed.isEmpty()) {
        statusLabel_->setText(
            QStringLiteral("已加入 %1 个任务,队列按顺序串行执行。").arg(added));
    } else {
        statusLabel_->setStyleSheet(QStringLiteral("color: #e06c75;"));
        statusLabel_->setText(QStringLiteral("已加入 %1 个;读取失败:%2")
                                  .arg(added)
                                  .arg(failed.join(QStringLiteral("; "))));
    }
}

int CompressPage::rowOf(quint64 id) const {
    const auto it = rows_.constFind(id);
    return it == rows_.cend() ? -1 : it.value();
}

void CompressPage::insertRow(quint64 id) {
    const one6s::jobs::JobRecord* rec = model_->record(id);
    if (!rec || rec->kind != one6s::jobs::JobKind::Compress) return;  // 分割任务归分割页
    const int row = table_->rowCount();
    table_->insertRow(row);

    auto* file_item = new QTableWidgetItem(QFileInfo(rec->inputPath).fileName());
    file_item->setToolTip(rec->inputPath);
    table_->setItem(row, 0, file_item);
    table_->setItem(row, 1, new QTableWidgetItem(levelDisplay(rec->levelId)));
    table_->setItem(row, 2, new QTableWidgetItem());

    auto* bar = new QProgressBar(this);
    bar->setRange(0, 100);
    bar->setValue(rec->progress);
    bar->setTextVisible(false);
    table_->setCellWidget(row, 3, bar);

    table_->setCellWidget(row, 4, makeActions(id));
    rows_[id] = row;
}

QWidget* CompressPage::makeActions(quint64 id) {
    auto* w = new QWidget(this);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(2, 0, 2, 0);
    auto* cancel = new QPushButton(QStringLiteral("取消"), w);
    cancel->setToolTip(QStringLiteral("从队列移除或取消当前任务"));
    connect(cancel, &QPushButton::clicked, this, [this, id] {
        model_->cancelTask(id);
    });
    auto* open = new QPushButton(QStringLiteral("打开文件夹"), w);
    connect(open, &QPushButton::clicked, this, [this, id] {
        const one6s::jobs::JobRecord* rec = model_->record(id);
        if (rec && !rec->outputPath.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(
                QFileInfo(rec->outputPath).absolutePath()));
    });
    h->addWidget(cancel);
    h->addWidget(open);
    return w;
}

void CompressPage::refreshRow(quint64 id) {
    const int row = rowOf(id);
    if (row < 0) return;
    const one6s::jobs::JobRecord* rec = model_->record(id);
    if (!rec) return;

    auto* state_item = table_->item(row, 2);
    state_item->setText(stateText(rec->state));
    if (rec->state == one6s::jobs::JobState::Failed) {
        state_item->setToolTip(rec->error);
        state_item->setForeground(QColor(0xe0, 0x6c, 0x75));
    } else {
        state_item->setToolTip({});
        state_item->setForeground({});
    }

    auto actions = table_->cellWidget(row, 4);
    if (auto* h = actions ? actions->findChild<QHBoxLayout*>() : nullptr) {
        // 取消按钮:queued/running 可点;打开文件夹:done 才可点。
        const auto items = h->findChildren<QPushButton*>();
        if (items.size() == 2) {
            const bool busy = rec->state == one6s::jobs::JobState::Queued ||
                              rec->state == one6s::jobs::JobState::Running;
            items[0]->setEnabled(busy);
            items[1]->setEnabled(rec->state == one6s::jobs::JobState::Done);
        }
    }
}

void CompressPage::onTaskAdded(quint64 id) { insertRow(id); }

void CompressPage::onTaskRemoved(quint64 id) {
    const int row = rowOf(id);
    if (row < 0) return;
    table_->removeRow(row);
    rows_.remove(id);
    for (auto& r : rows_)          // 行号重排
        if (r > row) --r;
}

void CompressPage::onTaskUpdated(quint64 id) { refreshRow(id); }

void CompressPage::onProgress(quint64 id, int percent) {
    const int row = rowOf(id);
    if (row < 0) return;
    if (auto* bar = qobject_cast<QProgressBar*>(table_->cellWidget(row, 3)))
        bar->setValue(percent);
}

void CompressPage::onFinished(quint64 id, one6s::jobs::JobState state,
                              const QString& error) {
    refreshRow(id);
    if (rowOf(id) < 0) return;   // 非本页任务(分割)
    if (state == one6s::jobs::JobState::Done) {
        statusLabel_->setStyleSheet({});
        statusLabel_->setText(QStringLiteral("完成:%1")
                                  .arg(model_->record(id)->outputPath));
    } else if (state == one6s::jobs::JobState::Failed) {
        statusLabel_->setStyleSheet(QStringLiteral("color: #e06c75;"));
        statusLabel_->setText(QStringLiteral("失败:%1").arg(error));
    }
}
