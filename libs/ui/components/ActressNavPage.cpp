#include "ui/components/ActressNavPage.h"

#include "darkeye_ui/components/DesignButton.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace darkeye
{

ActressNavPage::ActressNavPage(QString configFile, QWidget *parent)
    : QWidget(parent), m_configFile(std::move(configFile))
{
    auto *layout = new QGridLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    loadButtons(layout);
}

void ActressNavPage::setNames(QString japaneseName, QString chineseName)
{
    m_japaneseName = std::move(japaneseName).trimmed();
    m_chineseName = std::move(chineseName).trimmed();
}

void ActressNavPage::loadButtons(QGridLayout *layout)
{
    QFile file(m_configFile);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isArray())
        return;

    constexpr int columns = 2;
    const QJsonArray buttons = document.array();
    for (qsizetype index = 0; index < buttons.size(); ++index)
    {
        const QJsonObject config = buttons.at(index).toObject();
        const QString title = config.value(QStringLiteral("name")).toString().trimmed();
        if (title.isEmpty())
            continue;
        auto *button = new DesignButton(title, this);
        button->setToolTip(config.value(QStringLiteral("description")).toString());
        button->setProperty("testId", QStringLiteral("ActressExternalLinkButton"));
        connect(button, &QPushButton::clicked, this, [this, config] { openLink(config); });
        layout->addWidget(button, index / columns, index % columns);
    }
    auto *reveal = new DesignButton(QStringLiteral("定位 JSON 配置文件"), this);
    reveal->setProperty("testId", QStringLiteral("ActressNavConfigButton"));
    reveal->setToolTip(QStringLiteral("在文件管理器中选中 actress_nav_buttons.json；其他系统则打开其所在文件夹"));
    connect(reveal, &QPushButton::clicked, this, [this] {
#ifdef Q_OS_WIN
        // Keep the Python page's reveal-in-file-manager behavior: show the
        // configuration file itself, instead of merely opening its directory.
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QStringLiteral("/select,%1").arg(QDir::toNativeSeparators(m_configFile))});
#elif defined(Q_OS_MACOS)
        QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), m_configFile});
#else
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_configFile).absolutePath()));
#endif
    });
    layout->addWidget(reveal, (buttons.size() + columns - 1) / columns, 0, 1, columns);
}

void ActressNavPage::openLink(const QJsonObject &config) const
{
    QString url = config.value(QStringLiteral("url")).toString().trimmed();
    if (url.isEmpty())
        return;
    if (url.startsWith(QStringLiteral("www."), Qt::CaseInsensitive))
        url.prepend(QStringLiteral("https://"));
    static const QRegularExpression tokenPattern(QStringLiteral("\\{([^{}]+)\\}"));
    QRegularExpressionMatchIterator matches = tokenPattern.globalMatch(url);
    while (matches.hasNext())
    {
        const QString token = matches.next().captured(1);
        if (token != QStringLiteral("jp_name") && token != QStringLiteral("cn_name"))
            return;
    }

    const QJsonArray quoted = config.value(QStringLiteral("quote")).toArray();
    const auto replace = [&url, &quoted](const QString &token, const QString &value) {
        if (url.contains(QStringLiteral("{%1}").arg(token)) && value.isEmpty())
            return false;
        const bool shouldQuote = std::any_of(quoted.begin(), quoted.end(), [&token](const QJsonValue &item) {
            return item.toString() == token;
        });
        url.replace(QStringLiteral("{%1}").arg(token), shouldQuote ? QString::fromUtf8(QUrl::toPercentEncoding(value)) : value);
        return true;
    };
    if (!replace(QStringLiteral("jp_name"), m_japaneseName) ||
        !replace(QStringLiteral("cn_name"), m_chineseName))
        return;
    QDesktopServices::openUrl(QUrl::fromEncoded(url.toUtf8()));
}

} // namespace darkeye
