// settingspage.cpp — 「设置」页实现(M4)。
#include "app/settingspage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include "app/i18n.h"

namespace {

constexpr const char kPresetValues[] = "veryfast,faster,medium";  // 合法非默认值

bool isValidPreset(const QString& v) {
    return v.isEmpty() || QString::fromLatin1(kPresetValues).split(QLatin1Char(','))
                              .contains(v);
}

QString fileFilter() {
    return one6s::i18n::t(QStringLiteral("compress.filter.all"));
}

}  // namespace

namespace settings {

one6s::jobs::RuntimeOptions loadRuntimeOptions() {
    QSettings s;
    one6s::jobs::RuntimeOptions o;
    o.lowerPriority = s.value(QStringLiteral("lower_priority"), true).toBool();
    o.threadLimit = s.value(QStringLiteral("thread_limit"), 0).toInt();
    if (o.threadLimit < 0) o.threadLimit = 0;
    o.presetOverride = s.value(QStringLiteral("preset_speed"), QString()).toString();
    if (!isValidPreset(o.presetOverride)) o.presetOverride.clear();
    return o;
}

EnginePathOverrides loadEnginePathOverrides() {
    QSettings s;
    EnginePathOverrides o;
    o.ffmpeg = s.value(QStringLiteral("ffmpeg_path"), QString()).toString().trimmed();
    o.ffprobe = s.value(QStringLiteral("ffprobe_path"), QString()).toString().trimmed();
    return o;
}

}  // namespace settings

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    layout->setContentsMargins(16, 16, 16, 16);

    QSettings s;

    // --- 通用:语言 ---
    auto* general = new QGroupBox(
        one6s::i18n::t(QStringLiteral("settings.group.general")), this);
    auto* generalForm = new QFormLayout(general);
    languageCombo_ = new QComboBox(general);
    languageCombo_->addItem(QStringLiteral("English"), QStringLiteral("en"));
    languageCombo_->addItem(
        one6s::i18n::t(QStringLiteral("settings.language.system")),
        QStringLiteral("system"));
    languageCombo_->addItem(QStringLiteral("中文"), QStringLiteral("zh"));
    const int lang_idx = languageCombo_->findData(
        s.value(QStringLiteral("language"), QStringLiteral("en")).toString());
    if (lang_idx >= 0) languageCombo_->setCurrentIndex(lang_idx);
    generalForm->addRow(one6s::i18n::t(QStringLiteral("settings.language")),
                        languageCombo_);
    auto* langHint = new QLabel(
        one6s::i18n::t(QStringLiteral("settings.language.hint")), general);
    langHint->setWordWrap(true);
    langHint->setStyleSheet(QStringLiteral("color: #8a9099;"));
    generalForm->addRow(langHint);
    layout->addWidget(general);

    // --- 转码:优先级 / 线程 / 预设 ---
    auto* encode = new QGroupBox(
        one6s::i18n::t(QStringLiteral("settings.group.encode")), this);
    auto* encodeForm = new QFormLayout(encode);
    priorityCheck_ = new QCheckBox(
        one6s::i18n::t(QStringLiteral("settings.priority")), encode);
    priorityCheck_->setChecked(s.value(QStringLiteral("lower_priority"), true).toBool());
    encodeForm->addRow(priorityCheck_);
    auto* prioHint = new QLabel(
        one6s::i18n::t(QStringLiteral("settings.priority.hint")), encode);
    prioHint->setWordWrap(true);
    prioHint->setStyleSheet(QStringLiteral("color: #8a9099;"));
    encodeForm->addRow(prioHint);

    threadsSpin_ = new QSpinBox(encode);
    threadsSpin_->setRange(0, 128);  // 0 = 不限;上限只是防手滑
    threadsSpin_->setValue(s.value(QStringLiteral("thread_limit"), 0).toInt());
    encodeForm->addRow(one6s::i18n::t(QStringLiteral("settings.threads")),
                       threadsSpin_);
    auto* threadHint = new QLabel(
        one6s::i18n::t(QStringLiteral("settings.threads.hint")), encode);
    threadHint->setWordWrap(true);
    threadHint->setStyleSheet(QStringLiteral("color: #8a9099;"));
    encodeForm->addRow(threadHint);

    presetCombo_ = new QComboBox(encode);
    presetCombo_->addItem(
        one6s::i18n::t(QStringLiteral("settings.preset.slow")), QString());
    presetCombo_->addItem(
        one6s::i18n::t(QStringLiteral("settings.preset.veryfast")),
        QStringLiteral("veryfast"));
    presetCombo_->addItem(
        one6s::i18n::t(QStringLiteral("settings.preset.faster")),
        QStringLiteral("faster"));
    presetCombo_->addItem(
        one6s::i18n::t(QStringLiteral("settings.preset.medium")),
        QStringLiteral("medium"));
    const int preset_idx = presetCombo_->findData(
        s.value(QStringLiteral("preset_speed"), QString()).toString());
    if (preset_idx >= 0) presetCombo_->setCurrentIndex(preset_idx);
    encodeForm->addRow(one6s::i18n::t(QStringLiteral("settings.preset")),
                       presetCombo_);
    auto* presetHint = new QLabel(
        one6s::i18n::t(QStringLiteral("settings.preset.hint")), encode);
    presetHint->setWordWrap(true);
    presetHint->setStyleSheet(QStringLiteral("color: #e5c07b;"));
    encodeForm->addRow(presetHint);
    layout->addWidget(encode);

    // --- 高级:自定义引擎路径 ---
    auto* advanced = new QGroupBox(
        one6s::i18n::t(QStringLiteral("settings.group.advanced")), this);
    auto* advForm = new QFormLayout(advanced);
    ffmpegEdit_ = new QLineEdit(
        s.value(QStringLiteral("ffmpeg_path"), QString()).toString(), advanced);
    ffprobeEdit_ = new QLineEdit(
        s.value(QStringLiteral("ffprobe_path"), QString()).toString(), advanced);
    ffmpegEdit_->setPlaceholderText(QStringLiteral("ffmpeg"));
    ffprobeEdit_->setPlaceholderText(QStringLiteral("ffprobe"));
    auto makePathRow = [this](QLineEdit* edit) {
        auto* row = new QWidget(this);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto* browse = new QPushButton(
            one6s::i18n::t(QStringLiteral("settings.browse")), row);
        connect(browse, &QPushButton::clicked, this, [this, edit] {
            const QString path = QFileDialog::getOpenFileName(this, QString(),
                                                              edit->text(),
                                                              fileFilter());
            if (!path.isEmpty()) edit->setText(path);
        });
        h->addWidget(edit, /*stretch=*/1);
        h->addWidget(browse);
        return row;
    };
    advForm->addRow(one6s::i18n::t(QStringLiteral("settings.engines.ffmpeg")),
                    makePathRow(ffmpegEdit_));
    advForm->addRow(one6s::i18n::t(QStringLiteral("settings.engines.ffprobe")),
                    makePathRow(ffprobeEdit_));
    auto* engHint = new QLabel(
        one6s::i18n::t(QStringLiteral("settings.engines.hint")), advanced);
    engHint->setWordWrap(true);
    engHint->setStyleSheet(QStringLiteral("color: #8a9099;"));
    advForm->addRow(engHint);
    layout->addWidget(advanced);
    layout->addStretch(1);

    // --- 接线:即时生效项写 QSettings + 通知模型;语言/引擎路径重启生效 ---
    connect(languageCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        one6s::i18n::setLanguagePreference(
            languageCombo_->currentData().toString());
    });
    connect(priorityCheck_, &QCheckBox::toggled, this,
            &SettingsPage::saveRuntimeOptions);
    connect(threadsSpin_, &QSpinBox::valueChanged, this,
            &SettingsPage::saveRuntimeOptions);
    connect(presetCombo_, &QComboBox::currentIndexChanged, this,
            &SettingsPage::saveRuntimeOptions);
    connect(ffmpegEdit_, &QLineEdit::textChanged, this,
            &SettingsPage::saveEnginePaths);
    connect(ffprobeEdit_, &QLineEdit::textChanged, this,
            &SettingsPage::saveEnginePaths);
}

void SettingsPage::saveRuntimeOptions() {
    QSettings s;
    s.setValue(QStringLiteral("lower_priority"), priorityCheck_->isChecked());
    s.setValue(QStringLiteral("thread_limit"), threadsSpin_->value());
    s.setValue(QStringLiteral("preset_speed"), presetCombo_->currentData().toString());
    emit runtimeOptionsChanged();
}

void SettingsPage::saveEnginePaths() {
    QSettings s;
    s.setValue(QStringLiteral("ffmpeg_path"), ffmpegEdit_->text().trimmed());
    s.setValue(QStringLiteral("ffprobe_path"), ffprobeEdit_->text().trimmed());
}
