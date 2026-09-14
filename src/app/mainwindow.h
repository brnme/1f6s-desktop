// 主窗口(M3):双页布局 —— 「压缩」队列页 + 「分割」向导页,共用同一个
// JobModel 任务队列(FIFO 串行)。本阶段文案中文硬编码,M4 统一换 i18n key。
#pragma once

#include <QMainWindow>
#include <QString>

#include "core/spec.h"
#include "jobs/engine.h"
#include "jobs/jobmodel.h"

class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const one6s::Spec& spec, one6s::jobs::EnginePaths engines,
                        QWidget* parent = nullptr);

private:
    void showFatal(const QString& message);  // 引擎/规范问题:横幅置红

    one6s::Spec spec_;
    one6s::jobs::EnginePaths engines_;
    one6s::jobs::JobModel model_;  // 两页共用;声明在页指针之前(先析构页)
    QLabel* banner_ = nullptr;     // 引擎/编码器自检失败提示;正常时隐藏
};
