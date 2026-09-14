// 主窗口(M4):四页布局 —— 「压缩」队列页 +「分割」向导页 +「设置」+
// 「关于」,共用同一个 JobModel 任务队列(FIFO 串行)。文案走 assets/i18n
// (one6s::i18n::t)。设置页的运行时选项变更即时灌进 JobModel(对之后启动
// 的任务生效);关于页发现官网规范更新时同步状态栏提示。
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
    one6s::jobs::JobModel model_;  // 各页共用;声明在页指针之前(先析构页)
    QLabel* banner_ = nullptr;     // 引擎/编码器自检失败提示;正常时隐藏
};
