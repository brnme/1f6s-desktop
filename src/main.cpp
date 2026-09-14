// 1f6s-desktop 入口(M2):Fusion 暗色主题 + 规范加载 + 主窗口接线。
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPalette>
#include <QTimer>

#include "app/mainwindow.h"
#include "core/spec.h"
#include "jobs/engine.h"

namespace {

// 暗色 QPalette(Fusion 基调,深灰配色;M4 精修)。
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

// assets 路径解析:开发态用编译定义 ASSETS_DIR(指向仓库 assets/,由 CMake 注入);
// 打包态回退 applicationDirPath()/../assets。两态都实现,先到先得。
bool loadSpec(one6s::Spec& out, QString& error) {
    QStringList candidates;
#ifdef ASSETS_DIR
    candidates << QString::fromLocal8Bit(ASSETS_DIR);
#endif
    candidates << QCoreApplication::applicationDirPath()
                        + QStringLiteral("/../assets");
    for (const QString& base : candidates) {
        const QString levels = base + QStringLiteral("/spec/levels.json");
        if (!QFileInfo::exists(levels)) continue;
        try {
            out = one6s::Spec::load(levels.toStdString());
            return true;
        } catch (const std::exception& e) {
            error = QString::fromUtf8(e.what());
            return false;
        }
    }
    error = QStringLiteral("找不到规范文件 assets/spec/levels.json");
    return false;
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("1f6s-desktop"));
    app.setOrganizationName(QStringLiteral("1f6s"));
    app.setStyle(QStringLiteral("Fusion"));
    app.setPalette(darkPalette());

    one6s::Spec spec;
    QString spec_error;
    if (!loadSpec(spec, spec_error)) {
        QMessageBox::critical(nullptr, QStringLiteral("1f6s 桌面压缩"),
                              QStringLiteral("规范加载失败:%1").arg(spec_error));
        return 1;
    }

    MainWindow win(spec, one6s::jobs::locateEngines());
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
