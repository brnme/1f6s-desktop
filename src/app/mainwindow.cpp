#include "app/mainwindow.h"

#include <QLabel>

namespace {
// 暗色占位配色:仅示意后续 UI 基调,M1 由真正的主题/样式替代。
const char kPlaceholderQss[] =
    "QMainWindow { background-color: #1e1f22; }"
    "QLabel { color: #cfd2d6; }";
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("1f6s Desktop"));
    setStyleSheet(QString::fromLatin1(kPlaceholderQss));
    auto* placeholder = new QLabel(QStringLiteral("1f6s Desktop — skeleton (M0)"), this);
    placeholder->setAlignment(Qt::AlignCenter);
    setCentralWidget(placeholder);
}
