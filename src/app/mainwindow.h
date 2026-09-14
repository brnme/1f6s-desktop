// 主窗口占位(M0 骨架)。M1 起逐步填充:规范选择/微调表单/任务队列视图。
#pragma once

#include <QMainWindow>

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
};
