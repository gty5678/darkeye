#pragma once

#include <QUrl>
#include <QtGlobal>

namespace darkeye::utils {

// Override for a local MkDocs preview, e.g. http://127.0.0.1:8000/.
inline QUrl documentationUrl(const QString &page = QStringLiteral("usage/"))
{
    QString base = qEnvironmentVariable("DARKEYE_DOCS_URL").trimmed();
    if (base.isEmpty())
        base = QStringLiteral("https://gty5678.github.io/darkeye/");
    if (!base.endsWith(QLatin1Char('/')))
        base += QLatin1Char('/');
    return QUrl(base).resolved(QUrl(page));
}

} // namespace darkeye::utils
