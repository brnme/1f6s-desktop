// aboutpage.cpp — 「关于」页实现(M4)。
#include "app/aboutpage.h"

#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QUrl>
#include <QVBoxLayout>

#include "app/i18n.h"

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

namespace {

// 官网规范镜像(agent 路由只读接口,levels.json 的机器可读镜像)。
constexpr const char kSpecUrl[] = "https://1f6s.com/api/levels";
constexpr int kSpecTimeoutMs = 3000;

}  // namespace

AboutPage::AboutPage(const one6s::Spec& spec, one6s::jobs::EnginePaths engines,
                     QWidget* parent)
    : QWidget(parent),
      appVersion_(QStringLiteral(APP_VERSION)),
      localSpecVersion_(QString::fromStdString(spec.version)),
      ffmpegPath_(engines.ffmpeg) {
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    layout->setContentsMargins(16, 16, 16, 16);

    // 官网规范更新提示条:默认隐藏,检查命中才显示。
    updateLabel_ = new QLabel(this);
    updateLabel_->setWordWrap(true);
    updateLabel_->setStyleSheet(
        QStringLiteral("color: #e5c07b; background: #3a3524;"
                       " border: 1px solid #5a5132; border-radius: 4px;"
                       " padding: 8px 12px;"));
    updateLabel_->hide();
    layout->addWidget(updateLabel_);

    auto* form = new QFormLayout;
    form->addRow(one6s::i18n::t(QStringLiteral("about.version")),
                 new QLabel(appVersion_, this));
    form->addRow(one6s::i18n::t(QStringLiteral("about.ffmpeg")),
                 new QLabel(ffmpegVersion(), this));
    form->addRow(one6s::i18n::t(QStringLiteral("about.spec")),
                 new QLabel(localSpecVersion_.isEmpty()
                                ? one6s::i18n::t(QStringLiteral("about.unknown"))
                                : localSpecVersion_,
                            this));
    layout->addLayout(form);

    auto* blurb = new QLabel(
        one6s::i18n::t(QStringLiteral("about.blurb")), this);
    blurb->setWordWrap(true);
    blurb->setStyleSheet(QStringLiteral("color: #8a9099;"));
    layout->addWidget(blurb);
    layout->addStretch(1);

    checkRemoteSpec();
}

QString AboutPage::ffmpegVersion() const {
    if (ffmpegPath_.isEmpty())
        return one6s::i18n::t(QStringLiteral("about.unknown"));
    QProcess proc;
    proc.start(ffmpegPath_, {QStringLiteral("-version")});
    if (!proc.waitForStarted(3000) || !proc.waitForFinished(3000))
        return one6s::i18n::t(QStringLiteral("about.unknown"));
    const QString first =
        QString::fromUtf8(proc.readAllStandardOutput().split('\n').value(0))
            .trimmed();
    return first.isEmpty()
               ? one6s::i18n::t(QStringLiteral("about.unknown"))
               : first;
}

void AboutPage::checkRemoteSpec() {
    if (!nam_) nam_ = new QNetworkAccessManager(this);
    QNetworkRequest req{QUrl(QString::fromLatin1(kSpecUrl))};
    req.setTransferTimeout(kSpecTimeoutMs);
    QNetworkReply* reply = nam_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        // 失败静默:网站尚未上线,这里必须是优雅降级,不弹任何错误。
        if (reply->error() != QNetworkReply::NoError) return;
        const QJsonObject obj =
            QJsonDocument::fromJson(reply->readAll()).object();
        const QString remote = obj.value(QStringLiteral("version")).toString();
        if (remote.isEmpty() || remote == localSpecVersion_) return;
        updateLabel_->setText(one6s::i18n::t(
            QStringLiteral("about.spec_updated"),
            {{QStringLiteral("remote"), remote},
             {QStringLiteral("local"), localSpecVersion_}}));
        updateLabel_->show();
        emit specUpdateFound(remote, localSpecVersion_);
    });
}
