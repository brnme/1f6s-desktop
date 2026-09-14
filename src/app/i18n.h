// i18n.h — 桌面端国际化:加载 assets/i18n/{en,zh}.json,按 t(key) 查询。
//
// 词条文件由 tools/sync_i18n.sh 生成(网站 locales 子集 + desktop.{en,zh}.json
// 桌面专属文案合并),两语 key 集合必须一致(tests/test_i18n 有 parity 断言)。
// t() 缺失时回退返回 key 本身,页面永不因缺词条崩溃。
//
// 语言偏好存 QSettings(org "1f6s" / app "1f6s-desktop",由 main.cpp 设定),
// key 为 "language":en(默认)/ system(按 QLocale)/ zh;切换后重启生效,
// 不做运行时重译。占位符语法与网站一致:{name}(见 t 的 args 重载)。
#pragma once

#include <QPair>
#include <QString>
#include <QVector>

namespace one6s::i18n {

// 加载两种语言的词条表并解析语言偏好。assets_dir 为空时用默认候选
// (编译定义 ASSETS_DIR,再回退程序目录旁 assets/,与 spec 加载同策略)。
// 幂等:重复调用会重新加载并重解析偏好。
void init(const QString& assets_dir = QString());

// 查词条;缺失 → 返回 key 本身。未 init 时会先做一次默认路径的懒加载。
QString t(const QString& key);

// 带 {name} 占位符替换的查询:args 中每个 (name, value) 把 "{name}" 换成 value。
// 与网站 locales 的 str.format 风格一致,仅支持字面替换(无格式化)。
QString t(const QString& key, const QVector<QPair<QString, QString>>& args);

// 当前生效语言("en"/"zh";init 前返回 "en")。
QString language();

// 语言偏好(QSettings "language")→ 实际语言:"en"/"zh"。
// system → QLocale::system() 以 zh 开头算中文;其余取值(缺省/未知)一律英文。
QString resolveLanguage(const QString& preference);

// 写语言偏好到 QSettings;不重载词条(重启生效)。
void setLanguagePreference(const QString& preference);  // "system"/"en"/"zh"

// 供测试/罕见场景直接切换当前语言(不写 QSettings);未知值返回 false 且无副作用。
bool selectLanguage(const QString& lang);  // "en"/"zh"

}  // namespace one6s::i18n
