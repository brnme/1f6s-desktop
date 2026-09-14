#include "app/mainwindow.h"

#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "app/compresspage.h"
#include "app/splitpage.h"

MainWindow::MainWindow(const one6s::Spec& spec, one6s::jobs::EnginePaths engines,
                       QWidget* parent)
    : QMainWindow(parent), spec_(spec), engines_(engines), model_(this) {
    setWindowTitle(tr("1f6s 桌面压缩"));
    model_.setSpec(spec_);
    model_.setEngines(engines_);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 引擎/编码器自检横幅:失败给出可读文案(页面仍可见,便于查看问题)。
    banner_ = new QLabel(central);
    banner_->setWordWrap(true);
    banner_->setStyleSheet(
        QStringLiteral("color: #ffffff; background: #a03c3c; padding: 8px 16px;"));
    banner_->hide();
    layout->addWidget(banner_);

    auto* tabs = new QTabWidget(central);
    tabs->addTab(new CompressPage(spec_, engines_, &model_, tabs),
                 QStringLiteral("压缩"));
    tabs->addTab(new SplitPage(engines_, &model_, tabs), QStringLiteral("分割"));
    layout->addWidget(tabs, /*stretch=*/1);

    setCentralWidget(central);

    if (!engines_.ok()) {
        showFatal(QStringLiteral("未找到 ffmpeg/ffprobe。请把引擎放在程序同目录,"
                                 "或确认系统 PATH 里有 ffmpeg。"));
    } else {
        const QString enc_err = one6s::jobs::verifyEncoders(engines_.ffmpeg);
        if (!enc_err.isEmpty()) showFatal(enc_err);
    }
}

void MainWindow::showFatal(const QString& message) {
    banner_->setText(message);
    banner_->show();
}
