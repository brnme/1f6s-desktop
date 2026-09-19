// compresspage.cpp — 「压缩」页实现(M4:三参数模式 + i18n)。
//
// 三模式对齐网站 app/templates/compress.html 的 mode-radio:
//   level    → 等级下拉(L7 出目标体积输入)
//   scenario → 场景下拉(9 场景,名称/提示取网站 locales;映射见 kScenarios)
//   finetune → 基线等级 + 分辨率/间隔/灰度/音频 受限下拉(空 = 继承,
//              overrides 键名与网站 params.py ALLOWED_* 一致:
//              resolution / interval / grayscale / audio)
#include "app/compresspage.h"

#include <algorithm>

#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "app/i18n.h"
#include "core/encode.h"
#include "core/probe.h"

namespace {

// 场景 → 推荐等级映射:逐项对齐网站仓 app/services/presets.py 的 SCENARIOS
// (compress.html「按使用场景」下拉同源,顺序即 Python dict 插入序)。
// email 额外锁 target_mb=8(对应 scenario_preset 的 overrides);0 = 不带。
struct ScenarioTarget {
    const char* id;
    const char* level;
    int target_mb;
};
constexpr ScenarioTarget kScenarios[] = {
    {"email", "L7", 8},       // 邮件附件:锁定 8 MB → L7
    {"im", "L6", 0},          // 即时通讯 → L6
    {"training", "L4", 0},    // 企业培训 → L4
    {"mooc", "L3", 0},        // 在线课程/慕课 → L3
    {"demo", "L1", 0},        // 售前演示 → L1
    {"compliance", "L5", 0},  // 合规备案 → L5
    {"offline", "L6", 0},     // 移动离线观看 → L6
    {"platform", "L2", 0},    // 视频平台上传 → L2
    {"archive", "L8", 0},     // 批量归档 → L8
};
constexpr int kScenarioCount = sizeof(kScenarios) / sizeof(kScenarios[0]);

QString stateText(one6s::jobs::JobState state) {
    using one6s::jobs::JobState;
    switch (state) {
        case JobState::Queued: return one6s::i18n::t(QStringLiteral("compress.state.queued"));
        case JobState::Running: return one6s::i18n::t(QStringLiteral("compress.state.running"));
        case JobState::Done: return one6s::i18n::t(QStringLiteral("compress.state.done"));
        case JobState::Cancelled: return one6s::i18n::t(QStringLiteral("compress.state.cancelled"));
        case JobState::Failed: return one6s::i18n::t(QStringLiteral("compress.state.failed"));
    }
    return one6s::i18n::t(QStringLiteral("compress.state.unknown"));
}

QString videoFilter() {
    return one6s::i18n::t(QStringLiteral("compress.filter.video")) +
           QStringLiteral(" (*.mp4 *.mkv *.mov *.avi *.webm *.m4v *.flv *.ts *.wmv);;") +
           one6s::i18n::t(QStringLiteral("compress.filter.all"));
}

// 暂存列表行的时长显示:mm:ss(小时并入分钟,如 125:30)。
QString durationText(double seconds) {
    const int total = qMax(0, static_cast<int>(seconds + 0.5));
    return QStringLiteral("%1:%2")
        .arg(total / 60, 2, 10, QChar('0'))
        .arg(total % 60, 2, 10, QChar('0'));
}

}  // namespace

CompressPage::CompressPage(const one6s::Spec& spec, one6s::jobs::EnginePaths engines,
                           one6s::jobs::JobModel* model, QWidget* parent)
    : QWidget(parent), spec_(spec), engines_(engines), model_(model) {
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);
    layout->setContentsMargins(16, 16, 16, 16);

    // --- 顶行:添加视频 ---
    auto* topRow = new QHBoxLayout;
    addBtn_ = new QPushButton(
        one6s::i18n::t(QStringLiteral("compress.add_videos")), this);
    connect(addBtn_, &QPushButton::clicked, this, &CompressPage::addVideos);
    topRow->addWidget(addBtn_);
    topRow->addStretch(1);
    layout->addLayout(topRow);

    // --- 参数模式三选项卡(等级/场景/微调) ---
    modeTabs_ = new QTabWidget(this);
    connect(modeTabs_, &QTabWidget::currentChanged, this,
            &CompressPage::refreshModeHint);

    // 1) 等级模式
    auto* levelPane = new QWidget(modeTabs_);
    auto* levelLayout = new QVBoxLayout(levelPane);
    levelLayout->setContentsMargins(8, 8, 8, 8);
    levelLayout->setSpacing(6);
    levelCombo_ = new QComboBox(levelPane);
    connect(levelCombo_, &QComboBox::currentIndexChanged, this,
            &CompressPage::refreshLevelOptions);
    levelLayout->addWidget(levelCombo_);
    auto* targetRow = new QHBoxLayout;
    targetLabel_ = new QLabel(
        one6s::i18n::t(QStringLiteral("compress.target_label")), levelPane);
    targetSpin_ = new QSpinBox(levelPane);
    targetSpin_->setRange(1, 500);   // spec 白名单边界 kTargetMbMin/Max
    targetSpin_->setValue(8);
    targetSpin_->setSuffix(QStringLiteral(" MB"));
    targetRow->addWidget(targetLabel_);
    targetRow->addWidget(targetSpin_);
    targetRow->addStretch(1);
    levelLayout->addLayout(targetRow);
    modeTabs_->addTab(levelPane,
                      one6s::i18n::t(QStringLiteral("upload.mode.level")));

    // 2) 场景模式
    auto* scenarioPane = new QWidget(modeTabs_);
    auto* scenarioLayout = new QVBoxLayout(scenarioPane);
    scenarioLayout->setContentsMargins(8, 8, 8, 8);
    scenarioCombo_ = new QComboBox(scenarioPane);
    for (const auto& sc : kScenarios) {
        const QString id = QString::fromLatin1(sc.id);
        scenarioCombo_->addItem(
            one6s::i18n::t(QStringLiteral("scenario.") + id) +
                QStringLiteral(" — ") +
                one6s::i18n::t(QStringLiteral("scenario.") + id + ".hint"),
            id);
    }
    scenarioLayout->addWidget(scenarioCombo_);
    modeTabs_->addTab(scenarioPane,
                      one6s::i18n::t(QStringLiteral("upload.mode.scenario")));

    // 3) 微调模式(受限下拉,空选项 = 继承基线)
    auto* tunePane = new QWidget(modeTabs_);
    auto* tuneGrid = new QGridLayout(tunePane);
    tuneGrid->setContentsMargins(8, 8, 8, 8);
    tuneGrid->setHorizontalSpacing(12);
    tuneGrid->setVerticalSpacing(6);
    const QString inherit = one6s::i18n::t(QStringLiteral("compress.inherit"));
    auto makeTuneCombo = [this, inherit](std::initializer_list<QPair<QString, QString>> items) {
        auto* combo = new QComboBox(this);
        combo->addItem(inherit, QString());
        for (const auto& [label, data] : items) combo->addItem(label, data);
        return combo;
    };
    finetuneBase_ = new QComboBox(tunePane);
    populateLevelCombo(finetuneBase_, /*with_scene_tooltip=*/false);
    finetuneResolution_ = makeTuneCombo({
        {QStringLiteral("720p"), QStringLiteral("720p")},
        {QStringLiteral("480p"), QStringLiteral("480p")},
        {QStringLiteral("360p"), QStringLiteral("360p")},
    });
    finetuneInterval_ = makeTuneCombo({
        {QStringLiteral("4"), QStringLiteral("4")},
        {QStringLiteral("6"), QStringLiteral("6")},
        {QStringLiteral("10"), QStringLiteral("10")},
    });
    finetuneGray_ = makeTuneCombo({
        {one6s::i18n::t(QStringLiteral("upload.finetune.grayscale.on")),
         QStringLiteral("true")},
        {one6s::i18n::t(QStringLiteral("upload.finetune.grayscale.off")),
         QStringLiteral("false")},
    });
    finetuneAudio_ = makeTuneCombo({
        {QStringLiteral("20 kbps"), QStringLiteral("20k")},
        {QStringLiteral("8 kbps"), QStringLiteral("8k")},
    });
    int grid_row = 0, grid_col = 0;
    auto addTuneRow = [&](const char* key, QComboBox* combo) {
        tuneGrid->addWidget(new QLabel(one6s::i18n::t(QString::fromLatin1(key)),
                                       tunePane),
                            grid_row, grid_col);
        tuneGrid->addWidget(combo, grid_row, grid_col + 1);
        if (++grid_row > 2) { grid_row = 0; grid_col += 2; }
    };
    addTuneRow("compress.finetune.base", finetuneBase_);
    addTuneRow("compress.finetune.resolution", finetuneResolution_);
    addTuneRow("compress.finetune.interval", finetuneInterval_);
    addTuneRow("compress.finetune.gray", finetuneGray_);
    addTuneRow("compress.finetune.audio", finetuneAudio_);
    tuneGrid->setColumnStretch(1, 1);
    tuneGrid->setColumnStretch(3, 1);
    modeTabs_->addTab(tunePane,
                      one6s::i18n::t(QStringLiteral("upload.mode.finetune")));

    // 模式提示行(对齐网站 mode-hint 段落)。
    hintLabel_ = new QLabel(this);
    hintLabel_->setWordWrap(true);
    hintLabel_->setStyleSheet(QStringLiteral("color: #8a9099;"));
    layout->addWidget(modeTabs_);
    layout->addWidget(hintLabel_);
    refreshModeHint();

    // 分割任务占用队列时的一行原因提示(灰化期间可见)。
    gateLabel_ = new QLabel(this);
    gateLabel_->setWordWrap(true);
    gateLabel_->setStyleSheet(QStringLiteral("color: #e5c07b;"));
    gateLabel_->hide();
    layout->addWidget(gateLabel_);

    // --- 待压缩暂存区(选择与执行分离:选完只入列表,点「开始压缩」才入队) ---
    stageBox_ = new QWidget(this);
    auto* stageLayout = new QVBoxLayout(stageBox_);
    stageLayout->setContentsMargins(0, 0, 0, 0);
    stageLayout->setSpacing(6);
    stageList_ = new QListWidget(stageBox_);
    stageList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    stageList_->setMaximumHeight(150);
    stageLayout->addWidget(stageList_);
    auto* stageRow = new QHBoxLayout;
    removeBtn_ = new QPushButton(
        one6s::i18n::t(QStringLiteral("compress.stage.remove")), stageBox_);
    connect(removeBtn_, &QPushButton::clicked, this, &CompressPage::removeStaged);
    clearBtn_ = new QPushButton(
        one6s::i18n::t(QStringLiteral("compress.stage.clear")), stageBox_);
    connect(clearBtn_, &QPushButton::clicked, this, &CompressPage::clearStaged);
    startBtn_ = new QPushButton(
        one6s::i18n::t(QStringLiteral("compress.start")), stageBox_);
    connect(startBtn_, &QPushButton::clicked, this, &CompressPage::startCompression);
    startBtn_->setEnabled(false);
    stageRow->addWidget(removeBtn_);
    stageRow->addWidget(clearBtn_);
    stageRow->addStretch(1);
    stageRow->addWidget(startBtn_);
    stageLayout->addLayout(stageRow);
    layout->addWidget(stageBox_);
    stageBox_->hide();

    // --- 任务列表卡片 ---
    table_ = new QTableWidget(this);
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels(
        {one6s::i18n::t(QStringLiteral("compress.col.file")),
         one6s::i18n::t(QStringLiteral("compress.col.level")),
         one6s::i18n::t(QStringLiteral("compress.col.state")),
         one6s::i18n::t(QStringLiteral("compress.col.progress")),
         one6s::i18n::t(QStringLiteral("compress.col.action"))});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(true);
    layout->addWidget(table_, /*stretch=*/1);

    // --- 状态行 + 红线警示 ---
    statusLabel_ = new QLabel(
        one6s::i18n::t(QStringLiteral("compress.status.ready")), this);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    auto* warn = new QLabel(
        one6s::i18n::t(QStringLiteral("upload.redline.title")) +
            QStringLiteral(": ") +
            one6s::i18n::t(QStringLiteral("upload.redline.body")),
        this);
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

    populateLevelCombo(levelCombo_, /*with_scene_tooltip=*/true);
    refreshLevelOptions();
    refreshStageUi();
}

void CompressPage::populateLevelCombo(QComboBox* combo,
                                      bool with_scene_tooltip) const {
    // std::map 按 id 字典序 → L1..L8 天然有序;显示名取 i18n level.{id}.name
    // (缺失回退 levels.json 内嵌名),与网站等级下拉同文案。
    for (const auto& [id, level] : spec_.levels) {
        const QString qid = QString::fromStdString(id);
        const int idx = combo->count();
        combo->addItem(levelName(qid), qid);
        if (with_scene_tooltip) {
            const QString scene =
                one6s::i18n::t(QStringLiteral("level.") + qid + ".scene");
            const QString expected =
                one6s::i18n::t(QStringLiteral("level.") + qid + ".expected");
            QString tip = scene;
            if (expected != QStringLiteral("level.") + qid + ".expected")
                tip += QStringLiteral("\n") + expected;
            if (scene != QStringLiteral("level.") + qid + ".scene")
                combo->setItemData(idx, tip, Qt::ToolTipRole);
        }
    }
    const int idx = combo->findData(QString::fromStdString(spec_.default_level));
    if (idx >= 0) combo->setCurrentIndex(idx);
}

QString CompressPage::levelName(const QString& level_id) const {
    const QString key = QStringLiteral("level.") + level_id + ".name";
    const QString translated = one6s::i18n::t(key);
    if (translated != key) return translated;  // 词条命中
    const auto it = spec_.levels.find(level_id.toStdString());
    if (it != spec_.levels.end() && !it->second.name.empty())
        return QStringLiteral("%1 · %2").arg(level_id,
                                             QString::fromStdString(it->second.name));
    return level_id;
}

QString CompressPage::levelDisplay(const QString& level_id) const {
    return levelName(level_id);
}

void CompressPage::refreshModeHint() {
    // 构造期间 insertTab 就会触发 currentChanged,此时 hintLabel_ 可能尚未创建。
    if (!hintLabel_) return;
    static const char* kHints[] = {"upload.mode.level.hint",
                                   "upload.mode.scenario.hint",
                                   "upload.mode.finetune.hint"};
    const int tab = modeTabs_->currentIndex();
    if (tab < 0 || tab > 2) return;
    hintLabel_->setText(one6s::i18n::t(QString::fromLatin1(kHints[tab])));
}

void CompressPage::refreshLevelOptions() {
    const bool is_l7 = levelCombo_->currentData().toString() == QLatin1String("L7");
    targetLabel_->setVisible(is_l7);
    targetSpin_->setVisible(is_l7);
}

void CompressPage::resolveCurrentSelection(QString* level,
                                           nlohmann::json* overrides) const {
    *overrides = nlohmann::json::object();
    switch (modeTabs_->currentIndex()) {
        case 1: {  // 场景模式:kScenarios 映射(与网站 presets.py 同源)
            const QString id = scenarioCombo_->currentData().toString();
            for (const auto& sc : kScenarios) {
                if (id != QLatin1String(sc.id)) continue;
                *level = QString::fromLatin1(sc.level);
                if (sc.target_mb > 0) (*overrides)["target_mb"] = sc.target_mb;
                return;
            }
            *level = QString::fromStdString(spec_.default_level);
            return;
        }
        case 2: {  // 微调模式:空选项 = 继承(白名单语义,key 名同网站)
            *level = finetuneBase_->currentData().toString();
            const QString res = finetuneResolution_->currentData().toString();
            const QString interval = finetuneInterval_->currentData().toString();
            const QString gray = finetuneGray_->currentData().toString();
            const QString audio = finetuneAudio_->currentData().toString();
            if (!res.isEmpty())
                (*overrides)["resolution"] = res.toStdString();
            if (!interval.isEmpty())
                (*overrides)["interval"] = interval.toInt();
            if (!gray.isEmpty())
                (*overrides)["grayscale"] = (gray == QLatin1String("true"));
            if (!audio.isEmpty())
                (*overrides)["audio"] = audio.toStdString();
            return;
        }
        default: {  // 等级模式(现状):L7 锁目标体积
            *level = levelCombo_->currentData().toString();
            if (*level == QLatin1String("L7"))
                (*overrides)["target_mb"] = targetSpin_->value();
            return;
        }
    }
}

void CompressPage::addVideos() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, one6s::i18n::t(QStringLiteral("compress.dialog.pick")), QString(),
        videoFilter());
    if (files.isEmpty()) return;

    if (!engines_.ok() || engines_.ffprobe.isEmpty()) {
        statusLabel_->setStyleSheet(QStringLiteral("color: #e06c75;"));
        statusLabel_->setText(
            one6s::i18n::t(QStringLiteral("compress.error.no_engine")));
        return;
    }

    // 选择只做暂存 + probe(早暴露读不了的文件);参数在点「开始压缩」时
    // 按当时界面选择取值,所见即所用。
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
        staged_.append(Staged{path, r.duration_s, r.has_audio});
        ++added;
    }
    refreshStageUi();

    statusLabel_->setStyleSheet({});
    if (failed.isEmpty()) {
        statusLabel_->setText(one6s::i18n::t(
            QStringLiteral("compress.status.staged"),
            {{QStringLiteral("n"), QString::number(added)}}));
    } else {
        statusLabel_->setStyleSheet(QStringLiteral("color: #e06c75;"));
        statusLabel_->setText(one6s::i18n::t(
            QStringLiteral("compress.status.added_failed"),
            {{QStringLiteral("n"), QString::number(added)},
             {QStringLiteral("failed"), failed.join(QStringLiteral("; "))}}));
    }
}

void CompressPage::startCompression() {
    if (staged_.isEmpty()) return;

    QString level;
    nlohmann::json overrides;
    resolveCurrentSelection(&level, &overrides);

    for (const Staged& s : staged_) {
        // 输出与源同目录:<stem>_1f6s.mp4,已存在则续号,永不覆盖(core 的 outputPath)。
        const QString out = QString::fromStdString(one6s::encode::outputPath(
            spec_, QFileInfo(s.path).absolutePath().toStdString(),
            s.path.toStdString()));
        model_->enqueue(s.path, out, level, overrides, s.duration_s, s.has_audio);
    }
    const int n = staged_.size();
    staged_.clear();
    refreshStageUi();

    statusLabel_->setStyleSheet({});
    statusLabel_->setText(one6s::i18n::t(
        QStringLiteral("compress.status.added"),
        {{QStringLiteral("n"), QString::number(n)}}));
}

void CompressPage::removeStaged() {
    QList<int> rows;
    for (QListWidgetItem* item : stageList_->selectedItems())
        rows.append(stageList_->row(item));
    // 行号在 staged_ 与 stageList_ 一一对应;从大行号删起避免位移。
    std::sort(rows.begin(), rows.end(), [](int a, int b) { return a > b; });
    for (int row : rows) staged_.removeAt(row);
    refreshStageUi();
}

void CompressPage::clearStaged() {
    staged_.clear();
    refreshStageUi();
}

void CompressPage::refreshStageUi() {
    stageList_->clear();
    for (const Staged& s : staged_) {
        auto* item = new QListWidgetItem(
            QFileInfo(s.path).fileName() + QStringLiteral(" · ") +
            durationText(s.duration_s));
        item->setToolTip(s.path);
        stageList_->addItem(item);
    }
    stageBox_->setVisible(!staged_.isEmpty());
    refreshGate();
}

void CompressPage::refreshGate() {
    // 压缩与切片共用一个 FIFO 队列:分割任务排队/运行中时灰化本页入口,
    // 并说明原因;同侧压缩任务不限制(批处理仍可继续入队)。
    const bool split_open =
        model_->hasOpenTasks(one6s::jobs::JobKind::Split);
    addBtn_->setEnabled(!split_open);
    startBtn_->setEnabled(!staged_.isEmpty() && !split_open);
    gateLabel_->setVisible(split_open);
    if (split_open)
        gateLabel_->setText(
            one6s::i18n::t(QStringLiteral("compress.gate.hint")));
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
    auto* cancel = new QPushButton(
        one6s::i18n::t(QStringLiteral("compress.action.cancel")), w);
    cancel->setToolTip(one6s::i18n::t(
        QStringLiteral("compress.action.cancel_tip")));
    connect(cancel, &QPushButton::clicked, this, [this, id] {
        model_->cancelTask(id);
    });
    auto* open = new QPushButton(
        one6s::i18n::t(QStringLiteral("compress.action.open_folder")), w);
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

void CompressPage::onTaskAdded(quint64 id) {
    refreshGate();   // 对侧(分割)任务入队 → 本页入口灰化
    insertRow(id);
}

void CompressPage::onTaskRemoved(quint64 id) {
    refreshGate();   // 对侧排队任务被移除 → 视情况解除灰化
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
    refreshGate();   // 对侧任务终结 → 视情况解除灰化(先于本页过滤)
    refreshRow(id);
    if (rowOf(id) < 0) return;   // 非本页任务(分割)
    if (state == one6s::jobs::JobState::Done) {
        statusLabel_->setStyleSheet({});
        statusLabel_->setText(one6s::i18n::t(
            QStringLiteral("compress.status.done"),
            {{QStringLiteral("path"), model_->record(id)->outputPath}}));
    } else if (state == one6s::jobs::JobState::Failed) {
        statusLabel_->setStyleSheet(QStringLiteral("color: #e06c75;"));
        statusLabel_->setText(one6s::i18n::t(
            QStringLiteral("compress.status.failed"),
            {{QStringLiteral("error"), error}}));
    }
}
