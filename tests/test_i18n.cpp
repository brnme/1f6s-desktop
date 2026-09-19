// test_i18n.cpp — M4 i18n 体系测试。
//
// 契约:
//   1. parity:生成物 assets/i18n/{en,zh}.json 的 key 集合必须完全一致
//      (沿用网站仓 locales 的 parity 惯例);
//   2. desktop 源文件 assets/i18n/desktop.{en,zh}.json 同样 parity;
//   3. 关键 key 抽查:两语都必须存在且为非空字符串(等级/场景/三模式/
//      红线/目标体积/桌面专属);
//   4. 无孤立项:desktop.{en,zh}.json 里的每个 key 都必须被 src/app 下的
//      源码引用(网站抽取的 key 不查——它们按 level./scenario. 前缀动态拼接);
//   5. t() 缺失 key 回退 key 本身;{name} 占位符替换;语言切换。
#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <set>
#include <string>

#include "app/i18n.h"

#ifndef ASSETS_DIR
#define ASSETS_DIR "."
#endif
#ifndef APP_SRC_DIR
#define APP_SRC_DIR "."
#endif
#ifndef APP_MAIN_SRC
#define APP_MAIN_SRC "main.cpp"
#endif

using one6s::i18n::t;
using Args = QVector<QPair<QString, QString>>;

namespace {

QJsonObject loadJson(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

std::set<std::string> keySet(const QJsonObject& obj) {
    std::set<std::string> out;
    for (auto it = obj.begin(); it != obj.end(); ++it)
        out.insert(it.key().toStdString());
    return out;
}

// 递归收集 dir 下 .cpp/.h 文本,用于孤立项检查。
QString collectSources(const QString& dir) {
    QString all;
    for (const QFileInfo& entry :
         QDir(dir).entryInfoList({QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                                 QDir::Files)) {
        QFile f(entry.absoluteFilePath());
        if (f.open(QIODevice::ReadOnly)) all += QString::fromUtf8(f.readAll());
    }
    return all;
}

}  // namespace

class TestI18n : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        one6s::i18n::init(QStringLiteral(ASSETS_DIR));
    }

    void generatedParity()  // 契约 1:生成物两语 key 完全一致
    {
        const auto en = keySet(loadJson(QStringLiteral(ASSETS_DIR) +
                                        QStringLiteral("/i18n/en.json")));
        const auto zh = keySet(loadJson(QStringLiteral(ASSETS_DIR) +
                                        QStringLiteral("/i18n/zh.json")));
        QCOMPARE(en.empty(), false);
        QVERIFY(en == zh);
    }

    void desktopParity()  // 契约 2:desktop 源文件 parity
    {
        const auto en = keySet(loadJson(QStringLiteral(ASSETS_DIR) +
                                        QStringLiteral("/i18n/desktop.en.json")));
        const auto zh = keySet(loadJson(QStringLiteral(ASSETS_DIR) +
                                        QStringLiteral("/i18n/desktop.zh.json")));
        QCOMPARE(en.empty(), false);
        QVERIFY(en == zh);
    }

    void keySpotCheck()  // 契约 3:关键 key 存在且非空(抽查两语)
    {
        const QStringList required = {
            // 网站抽取:等级名/场景/预期体积
            "level.L1.name", "level.L4.name", "level.L8.name",
            "level.L4.scene", "level.L7.expected",
            "scenario.email", "scenario.email.hint",
            "scenario.archive", "scenario.offline.hint",
            // 三模式选项卡标题(网站抽取)与 hint/适用范围警示(桌面自有)
            "upload.mode.level", "upload.mode.level.hint",
            "upload.mode.scenario", "upload.mode.scenario.hint",
            "upload.mode.finetune", "upload.mode.finetune.hint",
            "upload.redline.body", "upload.redline.title",
            // 桌面专属
            "app.title", "app.tab.settings", "app.banner.no_engine",
            "compress.status.ready", "compress.state.failed",
            "split.status.ready", "split.tier.free",
            "settings.language", "settings.threads", "settings.preset.slow",
            "about.version", "about.spec_updated",
        };
        const auto check = [&required](const QString& file) {
            const QJsonObject obj = loadJson(file);
            for (const QString& key : required) {
                QVERIFY2(obj.contains(key),
                         qPrintable(file + QStringLiteral(" 缺 key ") + key));
                QVERIFY2(obj.value(key).isString() &&
                             !obj.value(key).toString().isEmpty(),
                         qPrintable(file + QStringLiteral(" 空词条 ") + key));
            }
        };
        check(QStringLiteral(ASSETS_DIR) + QStringLiteral("/i18n/en.json"));
        check(QStringLiteral(ASSETS_DIR) + QStringLiteral("/i18n/zh.json"));
    }

    void noOrphanDesktopKeys()  // 契约 4:desktop key 必须被 src/app 源码引用
    {
        const QJsonObject desktop = loadJson(QStringLiteral(ASSETS_DIR) +
                                             QStringLiteral("/i18n/desktop.en.json"));
        QString sources = collectSources(QStringLiteral(APP_SRC_DIR));
        {   // src/main.cpp 也在 UI 面(app.error.* 词条在其内),一并纳入扫描。
            QFile f(QStringLiteral(APP_MAIN_SRC));
            if (f.open(QIODevice::ReadOnly))
                sources += QString::fromUtf8(f.readAll());
        }
        QStringList orphans;
        for (auto it = desktop.begin(); it != desktop.end(); ++it) {
            const QString needle = QStringLiteral("\"") + it.key() +
                                   QStringLiteral("\"");
            if (!sources.contains(needle)) orphans << it.key();
        }
        QVERIFY2(orphans.isEmpty(),
                 qPrintable(QStringLiteral("desktop 词条未被引用: ") +
                            orphans.join(QStringLiteral(", "))));
    }

    void tFallbackAndPlaceholder()  // 契约 5:缺失回退 + 占位符 + 语言切换
    {
        QVERIFY(one6s::i18n::selectLanguage(QStringLiteral("en")));
        // 占位符替换(英文词条)。
        QCOMPARE(t(QStringLiteral("compress.status.added"),
                   Args{{QStringLiteral("n"), QStringLiteral("3")}}),
                 QStringLiteral("Added 3 task(s); the queue runs them one by one."));
        // 缺失 key 回退 key 本身,永不崩溃。
        QCOMPARE(t(QStringLiteral("no.such.key")), QStringLiteral("no.such.key"));

        QVERIFY(one6s::i18n::selectLanguage(QStringLiteral("zh")));
        QCOMPARE(t(QStringLiteral("compress.status.added"),
                   Args{{QStringLiteral("n"), QStringLiteral("3")}}),
                 QStringLiteral("已加入 3 个任务,队列按顺序串行执行。"));
        QCOMPARE(t(QStringLiteral("no.such.key.zh")),
                 QStringLiteral("no.such.key.zh"));

        // 非法语言拒绝切换且无副作用。
        QVERIFY(!one6s::i18n::selectLanguage(QStringLiteral("fr")));
        QCOMPARE(one6s::i18n::language(), QStringLiteral("zh"));

        // resolveLanguage:显式值直通;system 按系统 locale;未知/空值一律英文(默认语言)。
        QCOMPARE(one6s::i18n::resolveLanguage(QStringLiteral("en")),
                 QStringLiteral("en"));
        QCOMPARE(one6s::i18n::resolveLanguage(QStringLiteral("zh")),
                 QStringLiteral("zh"));
        QVERIFY(one6s::i18n::resolveLanguage(QStringLiteral("system")) ==
                    QStringLiteral("en") ||
                one6s::i18n::resolveLanguage(QStringLiteral("system")) ==
                    QStringLiteral("zh"));
        QCOMPARE(one6s::i18n::resolveLanguage(QStringLiteral("fr")),
                 QStringLiteral("en"));
        QCOMPARE(one6s::i18n::resolveLanguage(QString()),
                 QStringLiteral("en"));
    }
};

QTEST_MAIN(TestI18n)
#include "test_i18n.moc"
