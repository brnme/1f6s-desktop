#include "app/mainwindow.h"

#include <QLabel>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "app/aboutpage.h"
#include "app/compresspage.h"
#include "app/i18n.h"
#include "app/settingspage.h"
#include "app/splitpage.h"

MainWindow::MainWindow(const one6s::Spec& spec, one6s::jobs::EnginePaths engines,
                       QWidget* parent)
    : QMainWindow(parent), spec_(spec), engines_(engines), model_(this) {
    setWindowTitle(one6s::i18n::t(QStringLiteral("app.title")));
    model_.setSpec(spec_);
    model_.setEngines(engines_);
    // 设置页运行时选项(优先级/线程上限/预设速度)启动即灌入;此后设置页
    // 变更经 runtimeOptionsChanged 即时更新,对之后启动的任务生效。
    model_.setRuntimeOptions(settings::loadRuntimeOptions());

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
    auto* compress = new CompressPage(spec_, engines_, &model_, tabs);
    auto* split = new SplitPage(engines_, &model_, tabs);
    auto* settings = new SettingsPage(tabs);
    auto* about = new AboutPage(spec_, engines_, tabs);
    tabs->addTab(compress, one6s::i18n::t(QStringLiteral("app.tab.compress")));
    tabs->addTab(split, one6s::i18n::t(QStringLiteral("app.tab.split")));
    tabs->addTab(settings, one6s::i18n::t(QStringLiteral("app.tab.settings")));
    tabs->addTab(about, one6s::i18n::t(QStringLiteral("app.tab.about")));
    layout->addWidget(tabs, /*stretch=*/1);

    setCentralWidget(central);
    statusBar()->hide();  // 默认不占空间;只在规范更新提示时启用

    connect(settings, &SettingsPage::runtimeOptionsChanged, this, [this] {
        model_.setRuntimeOptions(settings::loadRuntimeOptions());
    });
    connect(about, &AboutPage::specUpdateFound, this,
            [this](const QString& remote, const QString& local) {
                statusBar()->showMessage(one6s::i18n::t(
                    QStringLiteral("about.spec_updated"),
                    {{QStringLiteral("remote"), remote},
                     {QStringLiteral("local"), local}}));
                statusBar()->show();
            });

    if (!engines_.ok()) {
        showFatal(one6s::i18n::t(QStringLiteral("app.banner.no_engine")));
    } else {
        const QString enc_err = one6s::jobs::verifyEncoders(engines_.ffmpeg);
        if (!enc_err.isEmpty()) showFatal(enc_err);
    }
}

void MainWindow::showFatal(const QString& message) {
    banner_->setText(message);
    banner_->show();
}
