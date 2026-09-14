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

#include <cmath>

namespace {

constexpr const char kVideoFilter[] =
    "视频文件 (*.mp4 *.mkv *.mov *.avi *.webm *.m4v *.flv *.ts *.wmv);;"
    "所有文件 (*)";

// assets 目录:开发态用编译定义(根 CMakeLists 注入),打包态回退程序目录旁。
QStringList assetCandidates() {
    QStringList out;
#ifdef ASSETS_DIR
    out << QString::fromLocal8Bit(ASSETS_DIR);
#endif
    out << QCoreApplication::applicationDirPath() + QStringLiteral("/../assets");
    return out;
}

QString mibText(double bytes) {
    return QStringLiteral("%1 MB").arg(bytes / 1048576.0, 0, 'f', 1);
}

QString secText(double s) {
    return QStringLiteral("%1 秒").arg(s, 0, 'f', 1);
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
    pickBtn_ = new QPushButton(QStringLiteral("选择视频…"), this);
    connect(pickBtn_, &QPushButton::clicked, this, &SplitPage::pickVideo);
    fileLabel_ = new QLabel(QStringLiteral("尚未选择视频"), this);
    fileLabel_->setStyleSheet(QStringLiteral("color: #8a9099;"));
    pickRow->addWidget(pickBtn_);
    pickRow->addWidget(fileLabel_, /*stretch=*/1);
    layout->addLayout(pickRow);

    // --- 第 2 步:档位预设 ---
    auto* tierRow = new QHBoxLayout;
    tierRow->addWidget(new QLabel(QStringLiteral("目标档位(单段体积上限):"), this));
    tierCombo_ = new QComboBox(this);
    tierCombo_->addItem(QStringLiteral("free 免费档"), QStringLiteral("free"));
    tierCombo_->addItem(QStringLiteral("pro 专业档"), QStringLiteral("pro"));
    tierCombo_->addItem(QStringLiteral("org 组织档"), QStringLiteral("org"));
    tierCombo_->addItem(QStringLiteral("自定义"), QStringLiteral("custom"));
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
        tierCombo_->setItemText(0, QStringLiteral("free 免费档(%1 MB)")
                                       .arg(tierMb_.value(QStringLiteral("free"))));
        tierCombo_->setItemText(1, QStringLiteral("pro 专业档(%1 MB)")
                                       .arg(tierMb_.value(QStringLiteral("pro"))));
        tierCombo_->setItemText(2, QStringLiteral("org 组织档(%1 MB)")
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
    previewTable_->setHorizontalHeaderLabels({QStringLiteral("段"),
                                              QStringLiteral("预计时长"),
                                              QStringLiteral("预计体积")});
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
    startBtn_ = new QPushButton(QStringLiteral("开始分割"), this);
    startBtn_->setEnabled(false);
    connect(startBtn_, &QPushButton::clicked, this, &SplitPage::startSplit);
    layout->addWidget(startBtn_, /*stretch=*/0, Qt::AlignLeft);

    bar_ = new QProgressBar(this);
    bar_->setRange(0, 100);
    bar_->setValue(0);
    layout->addWidget(bar_);

    statusLabel_ = new QLabel(
        configOk_ ? QStringLiteral("就绪。分割使用流复制(-c copy),不重编码,速度极快。")
                  : QStringLiteral("档位/预检配置加载失败:%1").arg(cfg_error),
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
    resultTable_->setHorizontalHeaderLabels({QStringLiteral("段名"),
                                             QStringLiteral("体积"),
                                             QStringLiteral("时长"),
                                             QStringLiteral("点数")});
    resultTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    resultTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultTable_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    resultTable_->setShowGrid(false);
    resultTable_->setAlternatingRowColors(true);
    layout->addWidget(resultTable_, /*stretch=*/1);

    auto* resultRow = new QHBoxLayout;
    pointsLabel_ = new QLabel(this);
    openDirBtn_ = new QPushButton(QStringLiteral("打开所在文件夹"), this);
    connect(openDirBtn_, &QPushButton::clicked, this, &SplitPage::openFolder);
    uploadBtn_ = new QPushButton(QStringLiteral("打开 1f6s 上传页"), this);
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
    for (const QString& base : assetCandidates()) {
        const QString t = base + QStringLiteral("/spec/tiers_limits.json");
        const QString p = base + QStringLiteral("/spec/precheck_factors.json");
        if (QFileInfo::exists(t) && QFileInfo::exists(p)) {
            tiers_path = t;
            precheck_path = p;
            break;
        }
    }
    if (tiers_path.isEmpty()) {
        error = QStringLiteral("找不到 assets/spec/{tiers_limits,precheck_factors}.json");
        return false;
    }
    QFile f(tiers_path);
    if (!f.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("无法读取 %1").arg(tiers_path);
        return false;
    }
    nlohmann::json j = nlohmann::json::parse(f.readAll().constData(), nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        error = QStringLiteral("tiers_limits.json 解析失败");
        return false;
    }
    for (auto const& [tier, mb] : j.items())
        if (mb.is_number()) tierMb_[QString::fromStdString(tier)] = mb.get<double>();
    if (tierMb_.size() < 3) {
        error = QStringLiteral("tiers_limits.json 缺少 free/pro/org 档位");
        return false;
    }
    try {
        precheck_ = one6s::Precheck::load(precheck_path.toStdString());
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
        statusLabel_->setText(QStringLiteral("未找到 ffprobe,无法读取视频信息。"));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择视频"), QString(), QString::fromUtf8(kVideoFilter));
    if (path.isEmpty()) return;

    one6s::ProbeResult r;
    try {
        r = one6s::probe(engines_.ffprobe, path);
    } catch (const std::exception& e) {
        statusLabel_->setStyleSheet(QStringLiteral("color: #e06c75;"));
        statusLabel_->setText(QStringLiteral("读取失败:%1")
                                  .arg(QString::fromUtf8(e.what())));
        return;
    }
    if (!one6s::splitter::containerFor(QFileInfo(path).suffix().toStdString())
             .has_value()) {
        statusLabel_->setStyleSheet(QStringLiteral("color: #e06c75;"));
        statusLabel_->setText(
            QStringLiteral("容器格式「%1」不支持流复制分割。")
                .arg(QFileInfo(path).suffix()));
        return;
    }
    inputPath_ = path;
    probe_ = r;
    fileLabel_->setText(QStringLiteral("%1(时长 %2 秒 · %3×%4 · %5)")
                            .arg(QFileInfo(path).fileName())
                            .arg(r.duration_s, 0, 'f', 1)
                            .arg(r.width)
                            .arg(r.height)
                            .arg(mibText(QFileInfo(path).size())));
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
        capLabel_->setText(QStringLiteral("选择视频后显示分段预览。"));
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
        QStringLiteral("单段时长上限:预检 %1 · 档位 %2 MB(留 5% 余量)· 规划段长 %3")
            .arg(std::isinf(precheck_max) ? QStringLiteral("不设限")
                                          : secText(precheck_max))
            .arg(tier_mb, 0, 'f', 0)
            .arg(std::isinf(plan_.segment_len_s)
                     ? QStringLiteral("整段")
                     : secText(plan_.segment_len_s)));

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
        previewTable_->setItem(i, 0, new QTableWidgetItem(QStringLiteral("第 %1 段").arg(i + 1)));
        previewTable_->setItem(i, 1, new QTableWidgetItem(secText(seg_len)));
        previewTable_->setItem(i, 2, new QTableWidgetItem(mibText(per_size)));
    }
    estimateLabel_->setText(
        QStringLiteral("预计 %1 段 · 每段 %2 点 · 点数合计 %3 点")
            .arg(plan_.parts)
            .arg(plan_.parts > 0 ? points_sum / plan_.parts : 0.0, 0, 'f', 0)
            .arg(points_sum, 0, 'f', 0));
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
    statusLabel_->setText(QStringLiteral("分割任务已加入队列…"));
    resetResultUi();
}

void SplitPage::onTaskUpdated(quint64 id) {
    if (id != lastSplitId_) return;
    const one6s::jobs::JobRecord* rec = model_->record(id);
    if (!rec) return;
    if (rec->state == one6s::jobs::JobState::Queued)
        statusLabel_->setText(QStringLiteral("分割任务排队中(前面还有任务)…"));
}

void SplitPage::onProgress(quint64 id, int percent) {
    if (id != lastSplitId_) return;
    bar_->setValue(percent);
    statusLabel_->setStyleSheet({});
    statusLabel_->setText(QStringLiteral("分割中… %1%").arg(percent));
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
            statusLabel_->setText(QStringLiteral("分割完成,共 %1 段。")
                                      .arg(rec->splitSegments.size()));
            resultTable_->setRowCount(rec->splitSegments.size());
            int row = 0;
            for (const one6s::jobs::SplitSegment& seg : rec->splitSegments) {
                auto* name = new QTableWidgetItem(QFileInfo(seg.path).fileName());
                name->setToolTip(seg.path);
                if (seg.overLimit) {  // 递归到底仍超限:标红提示
                    name->setForeground(QColor(0xe0, 0x6c, 0x75));
                    name->setToolTip(QStringLiteral("%1\n警告:该段体积仍超档位上限")
                                         .arg(seg.path));
                }
                resultTable_->setItem(row, 0, name);
                resultTable_->setItem(row, 1, new QTableWidgetItem(mibText(seg.sizeBytes)));
                resultTable_->setItem(row, 2, new QTableWidgetItem(secText(seg.durationS)));
                resultTable_->setItem(row, 3, new QTableWidgetItem(QString::number(seg.points)));
                ++row;
            }
            pointsLabel_->setText(QStringLiteral("点数合计:%1 点")
                                      .arg(rec->splitPoints));
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
            statusLabel_->setText(QStringLiteral("已取消。"));
            break;
        case one6s::jobs::JobState::Failed:
            statusLabel_->setStyleSheet(QStringLiteral("color: #e06c75;"));
            statusLabel_->setText(QStringLiteral("失败:%1").arg(error));
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
    // M4:换 i18n 深链(按档位跳对应套餐/上传页,文案出 assets/i18n)。
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://1f6s.com/")));
}
