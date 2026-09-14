// 1f6s-desktop 入口:最小 QApplication + 主窗口占位(M0 骨架)。
#include <QApplication>

#include "app/mainwindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("1f6s-desktop"));
    app.setOrganizationName(QStringLiteral("1f6s"));

    MainWindow win;
    win.resize(900, 600);
    win.show();
    return app.exec();
}
