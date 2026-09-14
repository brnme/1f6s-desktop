// splitpage.cpp — 「分割」页实现。
//
// 流程:选文件(probe)→ 选档位(free 2048 / pro 5120 / org 8192 / 自定义 MB)
// → 预览表(段数 × 每段时长/体积,含预检时长上限)→ 开始分割(进统一队列)
// → 结果表(段名/体积/时长/点数)+ 点数合计 + 打开文件夹 / 打开 1f6s 上传页。
#include "app/splitpage.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
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

#include <nlohmann/json.hpp>

#include "app/i18n.h"

#include <cmath>

namespace {

// 文件过滤器名走 i18n(与压缩页同一对 key),扩展名集合两语一致。
QString videoFilter() {
    return one6s::i18n::t(QStringLiteral("compress.filter.video")) +
           QStringLiteral(" (*.mp4 *.mkv *.mov *.avi *.webm *.m4v *.flv *.ts *.wmv);;") +
           one6s::i18n::t(QStringLiteral("compress.filter.all"));
}

// assets 目录候选集中在 one6s::i18n::assetCandidates(见 app/i18n.cpp),
// spec/tiers/词条三处加载共用,此处不再各留一份。

QString mibText(double bytes) {
    return QStringLiteral("%1 MB").arg(bytes / 1048576.0, 0, 'f', 1);
}

QString secText(double s) {
    return one6s::i18n::t(QStringLiteral("split.seconds"),
                          {{QStringLiteral("v"),
                            QString::number(s, 'f', 1)}});
}

}  // namespace

SplitPage::SplitPage(one6s::jobs::EnginePaths engines, one6s::jobs::JobModel* model,
                     QWidget* parent)
    : QWidget(parent), engines_(engines), model_(model) {
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(8);
    layout->setContentsMargins(16, 16, 16, 16);

    QString cfg_error;
    configOk_ = loadTierConfig(cfg_error);

    // --- 第 1 步:选文件 ---
    auto* pickRow = new QHBoxLayout;
    pickBtn_ = new QPushButton(one6s::i18n::t(QStringLiteral("split.pick")), this);
    connect(pickBtn_, &QPushButton::clicked, this, &SplitPage::pickVideo);
    fileLabel_ = new QLabel(one6s::i18n::t(QStringLiteral("split.no_file")), this);
    fileLabel_->setStyleSheet(QStringLiteral("color: #8a9099;"));
    pickRow->addWidget(pickBtn_);
    pickRow->addWidget(fileLabel_, /*stretch=*/1);
    layout->addLayout(pickRow);

    // --- 第 2 步:档位预设 ---
    auto* tierRow = new QHBoxLayout;
    tierRow->addWidget(new QLabel(one6s::i18n::t(QStringLiteral("split.tier.label")), this));
    tierCombo_ = new QComboBox(this);
    tierCombo_->addItem(one6s::i18n::t(QStringLiteral("split.tier.free")), QStringLiteral("free"));
    tierCombo_->addItem(one6s::i18n::t(QStringLiteral("split.tier.pro")), QStringLiteral("pro"));
    tierCombo_->addItem(one6s::i18n::t(QStringLiteral("split.tier.org")), QStringLiteral("org"));
    tierCombo_->addItem(one6s::i18n::t(QStringLiteral("split.tier.custom")), QStringLiteral("custom"));
    connect(tierCombo_, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshPreview(); });
    tierRow->addWidget(tierCombo_);
    customSpin_ = new QSpinBox(this);
    customSpin_->setRange(1, 1048576);  // MB;上限给到 1 PB 量级,用户自担
    customSpin_->setValue(2048);
    customSpin_->setSuffix(QStringLiteral(" MB"));
    connect(customSpin_, &QSpinBox::valueChanged, this,
            [this](int) { refreshPreview(); });
    tierRow->addWidget(customSpin_);
    tierRow->addStretch(1);
    layout->addLayout(tierRow);
    customSpin_->setVisible(false);
    // 档位值展示(从配置读;配置加载失败时也把框禁用掉)。
    if (configOk_) {
        tierCombo_->setItemText(0, one6s::i18n::t(QStringLiteral("split.tier.free")) + QStringLiteral("(%1 MB)")
                                       .arg(tierMb_.value(QStringLiteral("free"))));
        tierCombo_->setItemText(1, one6s::i18n::t(QStringLiteral("split.tier.pro")) + QStringLiteral("(%1 MB)")
                                       .arg(tierMb_.value(QStringLiteral("pro"))));
        tierCombo_->setItemText(2, one6s::i18n::t(QStringLiteral("split.tier.org")) + QStringLiteral("(%1 MB)")
                                       .arg(tierMb_.value(QStringLiteral("org"))));
    } else {
        tierCombo_->setEnabled(false);
        customSpin_->setEnabled(false);
    }
    connect(tierCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        customSpin_->setVisible(
            tierCombo_->currentData().toString() == QLatin1String("custom"));
    });

    // --- 第 3 步:预览 ---
    capLabel_ = new QLabel(this);
    capLabel_->setWordWrap(true);
    layout->addWidget(capLabel_);

    previewTable_ = new QTableWidget(this);
    previewTable_->setColumnCount(3);
    previewTable_->setHorizontalHeaderLabels({one6s::i18n::t(QStringLiteral("split.col.segment")),
                                              one6s::i18n::t(QStringLiteral("split.col.duration")),
                                              one6s::i18n::t(QStringLiteral("split.col.size"))});
    previewTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    previewTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    previewTable_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    previewTable_->setShowGrid(false);
    previewTable_->setAlternatingRowColors(true);
    previewTable_->setMaximumHeight(180);
    layout->addWidget(previewTable_);

    estimateLabel_ = new QLabel(this);
    layout->addWidget(estimateLabel_);

    // --- 第 4 步:执行 ---
    startBtn_ = new QPushButton(one6s::i18n::t(QStringLiteral("split.start")), this);
    startBtn_->setEnabled(false);
    connect(startBtn_, &QPushButton::clicked, this, &SplitPage::startSplit);
    layout->addWidget(startBtn_, /*stretch=*/0, Qt::AlignLeft);

    bar_ = new QProgressBar(this);
    bar_->setRange(0, 100);
    bar_->setValue(0);
    layout->addWidget(bar_);

    statusLabel_ = new QLabel(
        configOk_ ? one6s::i18n::t(QStringLiteral("split.status.ready"))
                  : one6s::i18n::t(QStringLiteral("split.status.config_error"),
                                    {{QStringLiteral("error"), cfg_error}}),
        this);
    statusLabel_->setWordWrap(true);
    if (!configOk_) statusLabel_->setStyleSheet(QStringLiteral("color: #e06c75;"));
    layout->addWidget(statusLabel_);

    warnLabel_ = new QLabel(this);
    warnLabel_->setWordWrap(true);
    warnLabel_->setStyleSheet(QStringLiteral("color: #e5c07b;"));
    warnLabel_->hide();
    layout->addWidget(warnLabel_);

    // --- 第 5 步:结果 ---
    resultTable_ = new QTableWidget(this);
    resultTable_->setColumnCount(4);
    resultTable_->setHorizontalHeaderLabels({one6s::i18n::t(QStringLiteral("split.result.name")),
                                             one6s::i18n::t(QStringLiteral("split.result.size")),
                                             one6s::i18n::t(QStringLiteral("split.result.duration")),
                                             one6s::i18n::t(QStringLiteral("split.result.points"))});
    resultTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    resultTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultTable_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    resultTable_->setShowGrid(false);
    resultTable_->setAlternatingRowColors(true);
    layout->addWidget(resultTable_, /*stretch=*/1);

    auto* resultRow = new QHBoxLayout;
    pointsLabel_ = new QLabel(this);
    openDirBtn_ = new QPushButton(one6s::i18n::t(QStringLiteral("split.open_folder")), this);
    connect(openDirBtn_, &QPushButton::clicked, this, &SplitPage::openFolder);
    uploadBtn_ = new QPushButton(one6s::i18n::t(QStringLiteral("split.open_upload")), this);
    connect(uploadBtn_, &QPushButton::clicked, this, &SplitPage::openUploadPage);
    resultRow->addWidget(pointsLabel_, /*stretch=*/1);
    resultRow->addWidget(openDirBtn_);
    resultRow->addWidget(uploadBtn_);
    layout->addLayout(resultRow);
    openDirBtn_->setEnabled(false);
    uploadBtn_->setEnabled(false);

    // --- 队列信号接线(只跟自己的最近一次分割任务) ---
    connect(model_, &one6s::jobs::JobModel::taskUpdated, this,
            &SplitPage::onTaskUpdated);
    connect(model_, &one6s::jobs::JobModel::jobProgress, this,
            &SplitPage::onProgress);
    connect(model_, &one6s::jobs::JobModel::jobFinished, this,
            &SplitPage::onFinished);
}

bool SplitPage::loadTierConfig(QString& error) {
    QString tiers_path;
    QString precheck_path;
    for (const QString& base : one6s::i18n::assetCandidates()) {
        const QString t = base + QStringLiteral("/spec/tiers_limits.json");
        const QString p = base + QStringLiteral("/spec/precheck_factors.json");
        if (QFileInfo::exists(t) && QFileInfo::exists(p)) {
            tiers_path = t;
            precheck_path = p;
            break;
        }
    }
    if (tiers_path.isEmpty()) {
        error = one6s::i18n::t(QStringLiteral("split.error.cfg_missing"));
        return false;
    }
    QFile f(tiers_path);
    if (!f.open(QIODevice::ReadOnly)) {
        error = one6s::i18n::t(QStringLiteral("split.error.cfg_read"), {{QStringLiteral("path"), tiers_path}});
        return false;
    }
    nlohmann::json j = nlohmann::json::parse(f.readAll().constData(), nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        error = one6s::i18n::t(QStringLiteral("split.error.cfg_parse"));
        return false;
    }
    for (auto const& [tier, mb] : j.items())
        if (mb.is_number()) tierMb_[QString::fromStdString(tier)] = mb.get<double>();
    if (tierMb_.size() < 3) {
        error = one6s::i18n::t(QStringLiteral("split.error.cfg_tiers"));
        return false;
    }
    try {
        precheck_ = one6s::Precheck::load(precheck_path);
    } catch (const std::exception& e) {
        error = QString::fromUtf8(e.what());
        return false;
    }
    return true;
}

double SplitPage::currentTierMaxMb() const {
    const QString tier = tierCombo_->currentData().toString();
    if (tier == QLatin1String("custom")) return customSpin_->value();
    return tierMb_.value(tier, 2048.0);
}

void SplitPage::pickVideo() {
    if (!engines_.ok() || engines_.ffprobe.isEmpty()) {
        statusLabel_->setStyleSheet(QStringLiteral("color: #e06c75;"));
        statusLabel_->setText(one6s::i18n::t(QStringLiteral("split.error.no_ffprobe")));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, one6s::i18n::t(QStringLiteral("split.dialog.pick")), QString(),
        videoFilter());
    if (path.isEmpty()) return;

    one6s::ProbeResult r;
    try {
        r = one6s::probe(engines_.ffprobe, path);
    } catch (const std::exception& e) {
        statusLabel_->setStyleSheet(QStringLiteral("color: #e06c75;"));
        statusLabel_->setText(one6s::i18n::t(
            QStringLiteral("split.status.probe_failed"),
            {{QStringLiteral("error"), QString::fromUtf8(e.what())}}));
        return;
    }
    if (!one6s::splitter::containerFor(QFileInfo(path).suffix().toStdString())
             .has_value()) {
        statusLabel_->setStyleSheet(QStringLiteral("color: #e06c75;"));
        statusLabel_->setText(
            one6s::i18n::t(
                QStringLiteral("split.error.container"),
                {{QStringLiteral("fmt"), QFileInfo(path).suffix()}}));
        return;
    }
    inputPath_ = path;
    probe_ = r;
    fileLabel_->setText(one6s::i18n::t(
                QStringLiteral("split.file_info"),
                {{QStringLiteral("name"), QFileInfo(path).fileName()},
                 {QStringLiteral("dur"), QString::number(r.duration_s, 'f', 1)},
                 {QStringLiteral("w"), QString::number(r.width)},
                 {QStringLiteral("h"), QString::number(r.height)},
                 {QStringLiteral("size"), mibText(QFileInfo(path).size())}}));
    fileLabel_->setStyleSheet({});
    resetResultUi();
    refreshPreview();
}

void SplitPage::refreshPreview() {
    setStartEnabled(false);
    previewTable_->setRowCount(0);
    estimateLabel_->clear();
    capLabel_->clear();
    if (!configOk_ || !probe_ || probe_->duration_s <= 0) {
        capLabel_->setText(one6s::i18n::t(QStringLiteral("split.preview.hint")));
        return;
    }

    // 预检时长上限(与执行器同一计算),再规划分割。
    const double precheck_max = precheck_.maxInputSeconds(
        probe_->width, probe_->height, probe_->codec);
    plan_ = one6s::splitter::planSplit(probe_->duration_s,
                                       static_cast<double>(QFileInfo(inputPath_).size()),
                                       currentTierMaxMb(), precheck_max);
    const double tier_mb = currentTierMaxMb();

    capLabel_->setText(
        one6s::i18n::t(
        QStringLiteral("split.cap.line"),
        {{QStringLiteral("precheck"),
          std::isinf(precheck_max) ? one6s::i18n::t(QStringLiteral("split.cap.unlimited"))
                                   : secText(precheck_max)},
         {QStringLiteral("tier"), QString::number(tier_mb, 'f', 0)},
         {QStringLiteral("plan"),
          std::isinf(plan_.segment_len_s)
              ? one6s::i18n::t(QStringLiteral("split.cap.whole"))
              : secText(plan_.segment_len_s)}}));

    const double size_bytes = static_cast<double>(QFileInfo(inputPath_).size());
    previewTable_->setRowCount(plan_.parts);
    const double per_size = size_bytes / plan_.parts;
    double points_sum = 0;
    for (int i = 0; i < plan_.parts; ++i) {
        const double seg_len = std::isinf(plan_.segment_len_s)
                                   ? probe_->duration_s
                                   : std::min(plan_.segment_len_s,
                                              probe_->duration_s - i * plan_.segment_len_s);
        const int pts = one6s::jobs::segmentPoints(per_size, seg_len);
        points_sum += pts;
        previewTable_->setItem(i, 0, new QTableWidgetItem(one6s::i18n::t(QStringLiteral("split.row.n"), {{QStringLiteral("n"), QString::number(i + 1)}})));
        previewTable_->setItem(i, 1, new QTableWidgetItem(secText(seg_len)));
        previewTable_->setItem(i, 2, new QTableWidgetItem(mibText(per_size)));
    }
    estimateLabel_->setText(
        one6s::i18n::t(
        QStringLiteral("split.estimate"),
        {{QStringLiteral("parts"), QString::number(plan_.parts)},
         {QStringLiteral("per"), QString::number(plan_.parts > 0 ? points_sum / plan_.parts : 0.0, 'f', 0)},
         {QStringLiteral("total"), QString::number(points_sum, 'f', 0)}}));
    setStartEnabled(true);
}

void SplitPage::setStartEnabled(bool on) {
    startBtn_->setEnabled(on && configOk_ && probe_.has_value() && plan_.parts >= 1);
}

void SplitPage::startSplit() {
    if (!probe_ || inputPath_.isEmpty() || !configOk_) return;
    one6s::jobs::SplitParams p;
    p.inputPath = inputPath_;
    p.outputDir = QFileInfo(inputPath_).absolutePath();
    p.durationS = probe_->duration_s;
    p.sizeBytes = static_cast<double>(QFileInfo(inputPath_).size());
    p.width = probe_->width;
    p.height = probe_->height;
    p.codec = probe_->codec;
    p.tierMaxMb = currentTierMaxMb();
    p.precheckMaxS = precheck_.maxInputSeconds(probe_->width, probe_->height,
                                               probe_->codec);
    // 下发预览确认过的显式计划,保证所见即所跑。
    p.segmentLenS = plan_.segment_len_s;
    p.plannedParts = plan_.parts;

    lastSplitId_ = model_->enqueueSplit(p);
    bar_->setValue(0);
    statusLabel_->setStyleSheet({});
    statusLabel_->setText(one6s::i18n::t(QStringLiteral("split.status.enqueued")));
    resetResultUi();
}

void SplitPage::onTaskUpdated(quint64 id) {
    if (id != lastSplitId_) return;
    const one6s::jobs::JobRecord* rec = model_->record(id);
    if (!rec) return;
    if (rec->state == one6s::jobs::JobState::Queued)
        statusLabel_->setText(one6s::i18n::t(QStringLiteral("split.status.queued")));
}

void SplitPage::onProgress(quint64 id, int percent) {
    if (id != lastSplitId_) return;
    bar_->setValue(percent);
    statusLabel_->setStyleSheet({});
    statusLabel_->setText(one6s::i18n::t(QStringLiteral("split.status.running"), {{QStringLiteral("pct"), QString::number(percent)}}));
}

void SplitPage::resetResultUi() {
    resultTable_->setRowCount(0);
    pointsLabel_->clear();
    warnLabel_->hide();
    warnLabel_->clear();
    openDirBtn_->setEnabled(false);
    uploadBtn_->setEnabled(false);
}

void SplitPage::onFinished(quint64 id, one6s::jobs::JobState state,
                           const QString& error) {
    if (id != lastSplitId_) return;
    const one6s::jobs::JobRecord* rec = model_->record(id);
    if (!rec) return;
    switch (state) {
        case one6s::jobs::JobState::Done: {
            bar_->setValue(100);
            statusLabel_->setStyleSheet({});
            statusLabel_->setText(one6s::i18n::t(QStringLiteral("split.status.done"), {{QStringLiteral("n"), QString::number(rec->splitSegments.size())}}));
            resultTable_->setRowCount(rec->splitSegments.size());
            int row = 0;
            for (const one6s::jobs::SplitSegment& seg : rec->splitSegments) {
                auto* name = new QTableWidgetItem(QFileInfo(seg.path).fileName());
                name->setToolTip(seg.path);
                if (seg.overLimit) {  // 递归到底仍超限:标红提示
                    name->setForeground(QColor(0xe0, 0x6c, 0x75));
                    name->setToolTip(one6s::i18n::t(QStringLiteral("split.warn.overlimit")) + QLatin1Char('\n') + seg.path);
                }
                resultTable_->setItem(row, 0, name);
                resultTable_->setItem(row, 1, new QTableWidgetItem(mibText(seg.sizeBytes)));
                resultTable_->setItem(row, 2, new QTableWidgetItem(secText(seg.durationS)));
                resultTable_->setItem(row, 3, new QTableWidgetItem(QString::number(seg.points)));
                ++row;
            }
            pointsLabel_->setText(one6s::i18n::t(QStringLiteral("split.points_total"), {{QStringLiteral("n"), QString::number(rec->splitPoints)}}));
            if (rec->splitWarnings.isEmpty()) {
                warnLabel_->hide();
            } else {
                warnLabel_->setText(rec->splitWarnings.join(QLatin1Char('\n')));
                warnLabel_->show();
            }
            openDirBtn_->setEnabled(true);
            uploadBtn_->setEnabled(true);
            break;
        }
        case one6s::jobs::JobState::Cancelled:
            statusLabel_->setStyleSheet({});
            statusLabel_->setText(one6s::i18n::t(QStringLiteral("split.status.cancelled")));
            break;
        case one6s::jobs::JobState::Failed:
            statusLabel_->setStyleSheet(QStringLiteral("color: #e06c75;"));
            statusLabel_->setText(one6s::i18n::t(QStringLiteral("split.status.failed"), {{QStringLiteral("error"), error}}));
            break;
        default:
            break;
    }
}

void SplitPage::openFolder() {
    if (inputPath_.isEmpty()) return;
    // 分段落在与源同目录(执行器默认输出目录)。
    QDesktopServices::openUrl(QUrl::fromLocalFile(
        QDir(model_->record(lastSplitId_)
                 ? model_->record(lastSplitId_)->outputPath
                 : QFileInfo(inputPath_).absolutePath())
            .absolutePath()));
}

void SplitPage::openUploadPage() {
    // 深链按档位跳对应套餐/上传页属二期;现阶段开首页。
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://1f6s.com/")));
}
