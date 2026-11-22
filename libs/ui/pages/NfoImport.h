#pragma once

#include "settings/Paths.h"
#include <QSqlDatabase>
#include <QStringList>
#include <QList>
#include <optional>

namespace darkeye::nfo
{
struct ParsedNfo final
{
    struct Cast final { QString name; QString thumb; };
    struct Fanart final { QString url; QString file; };
    QString serial, title, plot, director, releaseDate, notes, studio, series;
    std::optional<int> runtime;
    QStringList genres, tags, coverCandidates;
    QList<Cast> cast;
    QList<Fanart> fanart;
};

std::optional<ParsedNfo> parseNfo(const QString &path, bool mdcz, QString *error);
QString resolveLocalImage(const QString &source);
QString pickCover(const ParsedNfo &nfo);
bool isBlockedImage(const QString &source);
bool importNfo(QSqlDatabase database, const QString &path, bool mdcz,
               const settings::Paths &paths, QString *message);
} // namespace darkeye::nfo
