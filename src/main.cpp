// 1f6s-desktop 入口(M4):Fusion 暗色主题 + 规范加载 + i18n + 四页主窗口。
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPalette>
#include <QSettings>
#include <QTimer>

#include "app/i18n.h"
#include "app/mainwindow.h"
#include "app/settingspage.h"
#include "core/spec.h"
#include "jobs/engine.h"

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

namespace {

// 暗色 QPalette(Fusion 基调,深灰配色)。
QPalette darkPalette() {
    QPalette p;
    const QColor window(0x1e, 0x1f, 0x22);
    const QColor base(0x25, 0x27, 0x2b);
    const QColor button(0x2a, 0x2d, 0x31);
    const QColor text(0xcf, 0xd2, 0xd6);
    const QColor disabledText(0x6a, 0x6f, 0x76);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, button);
    p.setColor(QPalette::ToolTipBase, QColor(0x35, 0x38, 0x3d));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, QColor(0x3d, 0x6b, 0x9e));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x35, 0x38, 0x3d));
    p.setColor(QPalette::PlaceholderText, QColor(0x8a, 0x90, 0x99));
    return p;
}

// M4 精修:在 QPalette 之上补 QSS,把进度条/选项卡/表格观感调顺
// (圆角、分区描边、表头、斑马纹;M5 后再迭代)。
QString darkQss() {
    return QStringLiteral(R"(
QProgressBar {
    background: #22242a; border: 1px solid #3a3d42; border-radius: 4px;
    color: #cfd2d6; text-align: center; min-height: 14px;
}
QProgressBar::chunk { background: #3d6b9e; border-radius: 3px; }
QTabWidget::pane {
    border: 1px solid #35383d; border-radius: 3px; top: -1px;
    background: #232529;
}
QTabBar::tab {
    background: transparent; color: #9aa0a8; padding: 6px 16px;
    border: 1px solid transparent; border-bottom: none;
    border-top-left-radius: 3px; border-top-right-radius: 3px;
}
QTabBar::tab:selected { background: #232529; color: #e8eaec; border-color: #35383d; }
QTabBar::tab:hover:!selected { color: #c3c7cc; }
QTableWidget, QTableView {
    background: #232529; alternate-background-color: #272a2f;
    selection-background-color: #3d6b9e; selection-color: #ffffff;
    border: 1px solid #35383d; border-radius: 3px; gridline-color: transparent;
}
QHeaderView::section {
    background: #2a2d31; color: #9aa0a8; border: none;
    border-bottom: 1px solid #35383d; padding: 5px 8px;
}
QGroupBox {
    border: 1px solid #35383d; border-radius: 4px; margin-top: 10px;
    padding-top: 6px;
}
QGroupBox::title { subcontrol-origin: margin; left: 10px; color: #9aa0a8; }
QLineEdit, QComboBox, QSpinBox {
    background: #22242a; border: 1px solid #3a3d42; border-radius: 3px;
    padding: 3px 6px; selection-background-color: #3d6b9e;
}
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView {
    background: #2a2d31; border: 1px solid #3a3d42;
    selection-background-color: #3d6b9e;
}
QPushButton { background: #2f3338; border: 1px solid #3a3d42; border-radius: 3px; padding: 5px 12px; }
QPushButton:hover { background: #373c42; }
QPushButton:pressed { background: #2a2e33; }
QPushButton:disabled { color: #6a6f76; background: #282a2e; }
QStatusBar { background: #232529; color: #9aa0a8; }
)");
}

// assets 基准目录候选集中在 one6s::i18n::assetCandidates(开发态编译定义
// ASSETS_DIR + 打包态两种布局),spec/tiers/词条三处加载共用。
bool loadSpec(one6s::Spec& out, QString& error) {
    for (const QString& base : one6s::i18n::assetCandidates()) {
        const QString levels = base + QStringLiteral("/spec/levels.json");
        if (!QFileInfo::exists(levels)) continue;
        try {
            out = one6s::Spec::load(levels);
            return true;
        } catch (const std::exception& e) {
            error = QString::fromUtf8(e.what());
            return false;
        }
    }
    error = one6s::i18n::t(QStringLiteral("app.error.spec_missing"));
    return false;
}

// 设置页自定义引擎路径(高级项):可执行才覆盖,否则保留定位器结果。
void applyEngineOverrides(one6s::jobs::EnginePaths& engines) {
    const settings::EnginePathOverrides ov = settings::loadEnginePathOverrides();
    const auto usable = [](const QString& p) {
        if (p.isEmpty()) return false;
        const QFileInfo info(p);
        return info.isFile() && info.isExecutable();
    };
    if (usable(ov.ffmpeg)) engines.ffmpeg = ov.ffmpeg;
    if (usable(ov.ffprobe)) engines.ffprobe = ov.ffprobe;
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("1f6s-desktop"));
    app.setOrganizationName(QStringLiteral("1f6s"));
    app.setApplicationVersion(QStringLiteral(APP_VERSION));
    app.setStyle(QStringLiteral("Fusion"));
    app.setPalette(darkPalette());
    app.setStyleSheet(darkQss());

    // i18n:org/app 名先设好(QSettings 读语言偏好),再加载词条。
    one6s::i18n::init();

    one6s::Spec spec;
    QString spec_error;
    if (!loadSpec(spec, spec_error)) {
        QMessageBox::critical(
            nullptr, one6s::i18n::t(QStringLiteral("app.title")),
            one6s::i18n::t(QStringLiteral("app.error.spec_load"),
                           {{QStringLiteral("error"), spec_error}}));
        return 1;
    }

    one6s::jobs::EnginePaths engines = one6s::jobs::locateEngines();
    applyEngineOverrides(engines);

    MainWindow win(spec, engines);
    win.resize(880, 600);
    win.show();

    // 冒烟测试钩子:设 1F6S_SMOKE_EXIT_MS 后 N 毫秒自动退出
    // (QT_QPA_PLATFORM=offscreen 下验证启动接线无运行时错误)。
    bool smoke_ok = false;
    const int smoke_ms = qEnvironmentVariableIntValue("1F6S_SMOKE_EXIT_MS", &smoke_ok);
    if (smoke_ok && smoke_ms > 0)
        QTimer::singleShot(smoke_ms, &app, &QCoreApplication::quit);

    return app.exec();
}
