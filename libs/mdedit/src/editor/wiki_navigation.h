#pragma once

#include <QList>
#include <QString>
#include <QUrl>
#include <functional>

struct WikiLinkContext {
    QUrl baseUrl;
    QString projectRoot;
};

struct WikiLinkTarget {
    QString reference;
    QString title;
    QString detail;
    QString type;
};

// Callbacks run on the UI thread. Search should use cached data and return an
// initial list for an empty query. References use the registered prefix:ID.
struct WikiLinkProvider {
    std::function<QList<WikiLinkTarget>(const QString &query,const WikiLinkContext &)> search;
    std::function<QList<WikiLinkTarget>(const QString &reference,const WikiLinkContext &)> resolve;
    std::function<void(const WikiLinkTarget &,const WikiLinkContext &)> activate;
};
