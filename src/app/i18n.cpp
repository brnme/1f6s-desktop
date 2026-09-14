// i18n.cpp — 词条加载与查询实现。只依赖 QtCore(QJsonDocument/QSettings),
// 因此可被 test_i18n 在 QCoreApplication 下复用,不给 jobs/core 层添 GUI 依赖。
#include "app/i18n.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMap>
#include <QSettings>

namespace one6s::i18n {

namespace {

constexpr const char kLanguageKey[] = "language";  // QSettings key

// 词条表与当前语言:进程内单份,UI 单线程访问,无需加锁。
QMap<QString, QString>& table(const QString& lang) {
    static QMap<QString, QString> en;
    static QMap<QString, QString> zh;
    return lang == QLatin1String("zh") ? zh : en;
}

QString& currentLang() {
    static QString lang = QStringLiteral("en");
    return lang;
}

bool& initialized() {
    static bool ok = false;
    return ok;
}

// 从 <dir>/i18n/<lang>.json 读词条;文件缺失/坏 JSON → 空表(t() 回退 key)。
QMap<QString, QString> loadTable(const QString& dir, const QString& lang) {
    QFile f(dir + QStringLiteral("/i18n/%1.json").arg(lang));
    QMap<QString, QString> out;
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it)
        if (it.value().isString()) out.insert(it.key(), it.value().toString());
    return out;
}

}  // namespace

QStringList assetCandidates() {
    QStringList out;
#ifdef ASSETS_DIR
    out << QString::fromLocal8Bit(ASSETS_DIR);
#endif
    if (QCoreApplication::instance()) {
        // 打包态两种布局都列上,先到先得:
        //   上级 assets —— AppImage(usr/bin→usr/assets)、macOS(Contents/MacOS→Contents/assets);
        //   同级 assets —— Windows 便携 zip(exe 与 assets/ 并列,用户解压即双击)。
        const QString exe_dir = QCoreApplication::applicationDirPath();
        out << exe_dir + QStringLiteral("/../assets")
            << exe_dir + QStringLiteral("/assets");
    }
    return out;
}

void init(const QString& assets_dir) {
    QStringList candidates;
    if (!assets_dir.isEmpty()) candidates << assets_dir;
    else candidates = assetCandidates();
    for (const QString& dir : candidates) {
        if (!QFileInfo::exists(dir + QStringLiteral("/i18n/en.json"))) continue;
        table(QStringLiteral("en")) = loadTable(dir, QStringLiteral("en"));
        table(QStringLiteral("zh")) = loadTable(dir, QStringLiteral("zh"));
        currentLang() = resolveLanguage(
            QSettings().value(kLanguageKey, QStringLiteral("en")).toString());
        initialized() = true;
        return;
    }
    initialized() = true;  // 找不到词条目录也标记完成,避免每次 t() 重扫
    currentLang() = resolveLanguage(
        QSettings().value(kLanguageKey, QStringLiteral("en")).toString());
}

QString t(const QString& key) {
    if (!initialized()) init();
    const QMap<QString, QString>& tbl = table(currentLang());
    return tbl.value(key, key);  // 缺失回退 key 本身
}

QString t(const QString& key, const QVector<QPair<QString, QString>>& args) {
    QString s = t(key);
    for (const auto& [name, value] : args)
        s.replace(QLatin1Char('{') + name + QLatin1Char('}'), value);
    return s;
}

QString language() { return currentLang(); }

QString resolveLanguage(const QString& preference) {
    if (preference == QLatin1String("en") || preference == QLatin1String("zh"))
        return preference;
    if (preference == QLatin1String("system"))
        return QLocale::system().name().startsWith(QLatin1String("zh"))
                   ? QStringLiteral("zh")
                   : QStringLiteral("en");
    // 其余取值(缺省/未知)一律英文:默认语言是英语,与网站英文优先一致。
    return QStringLiteral("en");
}

void setLanguagePreference(const QString& preference) {
    QSettings().setValue(kLanguageKey, preference);
}

bool selectLanguage(const QString& lang) {
    if (lang != QLatin1String("en") && lang != QLatin1String("zh")) return false;
    if (!initialized()) init();
    currentLang() = lang;
    return true;
}

}  // namespace one6s::i18n
