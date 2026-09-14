#include "app/mainwindow.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include "core/encode.h"

namespace {

// 视频扩展名过滤(选文件用)。
constexpr const char kVideoFilter[] =
    "视频文件 (*.mp4 *.mkv *.mov *.avi *.webm *.m4v *.flv *.ts *.wmv);;"
    "所有文件 (*)";

// 红线警示:与网站端口径一致(讲解/幻灯片类适用;体育/艺术/软件操作演示不适用)。
// 硬编码一句,M4 换 i18n key。
constexpr const char kRedlineWarning[] =
    "注意:1f6s 抽帧压缩面向讲解、培训、幻灯片类内容;"
    "体育、艺术视频,以及光标操作很重要的软件演示不适用,压缩后无法正常观看。";

}  // namespace

MainWindow::MainWindow(const one6s::Spec& spec, one6s::jobs::EnginePaths engines,
                       QWidget* parent)
    : QMainWindow(parent), spec_(spec), engines_(std::move(engines)), model_(this) {
    setWindowTitle(tr("1f6s 桌面压缩"));
    model_.setSpec(spec_);
    model_.setEngines(engines_);
    connect(&model_, &one6s::jobs::JobModel::jobProgress,
            this, &MainWindow::onJobProgress);
    connect(&model_, &one6s::jobs::JobModel::jobFinished,
            this, &MainWindow::onJobFinished);

    setCentralWidget(buildCentral());
    refreshLevelOptions();

    // 启动自检:引擎定位失败或编码器缺失时给出可读文案并禁用开始。
    if (!engines_.ok()) {
        showFatal(QStringLiteral("未找到 ffmpeg/ffprobe。请把引擎放在程序同目录,"
                                 "或确认系统 PATH 里有 ffmpeg。"));
    } else {
        const QString enc_err =
            one6s::jobs::verifyEncoders(engines_.ffmpeg);
        if (!enc_err.isEmpty()) showFatal(enc_err);
    }
}

QWidget* MainWindow::buildCentral() {
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setSpacing(10);
    layout->setContentsMargins(16, 16, 16, 16);

    // --- 第一行:选择视频 ---
    auto* pickRow = new QHBoxLayout;
    pickBtn_ = new QPushButton(QStringLiteral("选择视频…"), central);
    connect(pickBtn_, &QPushButton::clicked, this, &MainWindow::pickVideo);
    fileLabel_ = new QLabel(QStringLiteral("尚未选择视频"), central);
    fileLabel_->setStyleSheet(QStringLiteral("color: #8a9099;"));
    pickRow->addWidget(pickBtn_);
    pickRow->addWidget(fileLabel_, /*stretch=*/1);
    layout->addLayout(pickRow);

    // --- 输入信息 ---
    infoLabel_ = new QLabel(central);
    infoLabel_->setWordWrap(true);
    layout->addWidget(infoLabel_);

    // --- 等级 + L7 目标体积 + 预计体积 ---
    auto* levelRow = new QHBoxLayout;
    levelRow->addWidget(new QLabel(QStringLiteral("压缩等级:"), central));
    levelCombo_ = new QComboBox(central);
    connect(levelCombo_, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshLevelOptions(); });
    levelRow->addWidget(levelCombo_, /*stretch=*/1);
    targetLabel_ = new QLabel(QStringLiteral("目标体积 (MB):"), central);
    targetSpin_ = new QSpinBox(central);
    targetSpin_->setRange(1, 500);   // spec 白名单边界 kTargetMbMin/Max
    targetSpin_->setValue(8);
    targetSpin_->setSuffix(QStringLiteral(" MB"));
    connect(targetSpin_, &QSpinBox::valueChanged, this,
            [this](int) { refreshLevelOptions(); });
    levelRow->addWidget(targetLabel_);
    levelRow->addWidget(targetSpin_);
    layout->addLayout(levelRow);

    estimateLabel_ = new QLabel(central);
    layout->addWidget(estimateLabel_);

    // --- 红线警示 ---
    warnLabel_ = new QLabel(QString::fromUtf8(kRedlineWarning), central);
    warnLabel_->setWordWrap(true);
    warnLabel_->setStyleSheet(QStringLiteral("color: #e5c07b;"));
    layout->addWidget(warnLabel_);

    // --- 操作行:开始 / 取消 / 打开所在文件夹 ---
    auto* actionRow = new QHBoxLayout;
    startBtn_ = new QPushButton(QStringLiteral("开始压缩"), central);
    startBtn_->setEnabled(false);
    connect(startBtn_, &QPushButton::clicked, this, &MainWindow::startCompression);
    cancelBtn_ = new QPushButton(QStringLiteral("取消"), central);
    cancelBtn_->setEnabled(false);
    connect(cancelBtn_, &QPushButton::clicked, this, &MainWindow::cancelRunning);
    openBtn_ = new QPushButton(QStringLiteral("打开所在文件夹"), central);
    openBtn_->setEnabled(false);
    connect(openBtn_, &QPushButton::clicked, this, &MainWindow::openOutputFolder);
    actionRow->addWidget(startBtn_);
    actionRow->addWidget(cancelBtn_);
    actionRow->addWidget(openBtn_);
    actionRow->addStretch(1);
    layout->addLayout(actionRow);

    // --- 进度条 + 状态行 ---
    bar_ = new QProgressBar(central);
    bar_->setRange(0, 100);
    bar_->setValue(0);
    layout->addWidget(bar_);
    statusLabel_ = new QLabel(QStringLiteral("就绪。"), central);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    layout->addStretch(1);
    return central;
}

void MainWindow::refreshLevelOptions() {
    const QString prev = currentLevelId();
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
    const bool is_l7 = currentLevelId() == QLatin1String("L7");
    targetLabel_->setVisible(is_l7);
    targetSpin_->setVisible(is_l7);

    // 预计输出体积(estimateMb;L7 时取目标体积)。
    if (probe_ && probe_->duration_s > 0) {
        const std::optional<int> target =
            is_l7 ? std::optional<int>(targetSpin_->value()) : std::nullopt;
        const double mb = one6s::encode::estimateMb(
            currentLevelId().toStdString(), probe_->duration_s, target);
        estimateLabel_->setText(
            QStringLiteral("预计输出体积 ≈ %1 MB(按时长 %2 秒估算)")
                .arg(mb, 0, 'f', 1)
                .arg(probe_->duration_s, 0, 'f', 1));
    } else {
        estimateLabel_->setText(QStringLiteral("选择视频后显示预计输出体积。"));
    }
}

QString MainWindow::currentLevelId() const {
    return levelCombo_ ? levelCombo_->currentData().toString() : QString();
}

void MainWindow::pickVideo() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择视频"), QString(),
        QString::fromUtf8(kVideoFilter));
    if (path.isEmpty()) return;

    if (!engines_.ok() || engines_.ffprobe.isEmpty()) {
        showFatal(QStringLiteral("未找到 ffprobe,无法读取视频信息。"));
        return;
    }
    one6s::ProbeResult r;
    try {
        r = one6s::probe(engines_.ffprobe, path);
    } catch (const std::exception& e) {
        statusLabel_->setStyleSheet(QStringLiteral("color: #e06c75;"));
        statusLabel_->setText(QStringLiteral("读取失败:%1")
                                  .arg(QString::fromUtf8(e.what())));
        return;
    }
    inputPath_ = path;
    probe_ = r;
    fileLabel_->setText(QFileInfo(path).fileName());
    fileLabel_->setStyleSheet({});
    infoLabel_->setText(QStringLiteral("时长 %1 秒 · %2×%3 · %4 MB · 编码 %5%6")
                            .arg(r.duration_s, 0, 'f', 1)
                            .arg(r.width)
                            .arg(r.height)
                            .arg(QFileInfo(path).size() / 1048576.0, 0, 'f', 1)
                            .arg(QString::fromStdString(r.codec),
                                 r.has_audio ? QString() : QStringLiteral("(无音轨)")));
    startBtn_->setEnabled(true);
    refreshLevelOptions();
}

void MainWindow::startCompression() {
    if (!probe_ || inputPath_.isEmpty() || jobId_ != 0) return;
    // 输出与源同目录:<stem>_1f6s.mp4,已存在则续号,永不覆盖(core 的 outputPath)。
    const QString out = QString::fromStdString(one6s::encode::outputPath(
        spec_, QFileInfo(inputPath_).absolutePath().toStdString(),
        inputPath_.toStdString()));
    const QString level = currentLevelId();
    nlohmann::json overrides = nlohmann::json::object();
    if (level == QLatin1String("L7")) overrides["target_mb"] = targetSpin_->value();

    jobId_ = model_.enqueue(inputPath_, out, level, overrides,
                            probe_->duration_s, probe_->has_audio);
    lastOutput_ = out;
    setRunningUi(true);
    statusLabel_->setStyleSheet({});
    statusLabel_->setText(QStringLiteral("压缩中… 输出:%1").arg(out));
}

void MainWindow::cancelRunning() {
    model_.cancelCurrent();
    statusLabel_->setText(QStringLiteral("正在取消…"));
}

void MainWindow::onJobProgress(quint64 id, int percent) {
    if (id != jobId_) return;
    bar_->setValue(percent);
}

void MainWindow::onJobFinished(quint64 id, one6s::jobs::JobState state,
                               const QString& error) {
    if (id != jobId_) return;
    jobId_ = 0;
    setRunningUi(false);
    switch (state) {
        case one6s::jobs::JobState::Done:
            bar_->setValue(100);
            statusLabel_->setStyleSheet({});
            statusLabel_->setText(
                QStringLiteral("完成:%1").arg(lastOutput_));
            openBtn_->setEnabled(true);
            break;
        case one6s::jobs::JobState::Cancelled:
            statusLabel_->setStyleSheet({});
            statusLabel_->setText(QStringLiteral("已取消。"));
            break;
        case one6s::jobs::JobState::Failed:
            statusLabel_->setStyleSheet(QStringLiteral("color: #e06c75;"));
            statusLabel_->setText(
                QStringLiteral("失败:%1").arg(error));
            break;
        default:
            break;
    }
}

void MainWindow::openOutputFolder() {
    if (lastOutput_.isEmpty()) return;
    const QString dir = QFileInfo(lastOutput_).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::setRunningUi(bool running) {
    startBtn_->setEnabled(!running && probe_.has_value());
    pickBtn_->setEnabled(!running);
    levelCombo_->setEnabled(!running);
    targetSpin_->setEnabled(!running);
    cancelBtn_->setEnabled(running);
}

void MainWindow::showFatal(const QString& message) {
    startBtn_->setEnabled(false);
    statusLabel_->setStyleSheet(QStringLiteral("color: #e06c75;"));
    statusLabel_->setText(message);
}
