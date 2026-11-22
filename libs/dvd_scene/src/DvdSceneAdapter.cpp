#include "dvd_scene/DvdSceneAdapter.h"
#include "dvd_scene/DvdShelfController.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <cmath>
#include <limits>

namespace darkeye::dvd_scene {
DvdItemLoadResult DvdSceneAdapter::loadJson(const QString &path)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) return {{},file.errorString()};
    if(file.size()>16*1024*1024) return {{},QStringLiteral("DVD JSON exceeds 16 MiB")};
    QJsonParseError error;
    const auto document=QJsonDocument::fromJson(file.readAll(),&error);
    if(error.error!=QJsonParseError::NoError) return {{},error.errorString()};
    if(!document.isObject() || !document.object()["items"].isArray())
        return {{},QStringLiteral("Expected an object containing an items array")};
    const auto entries=document.object()["items"].toArray();
    if(entries.size()>100000) return {{},QStringLiteral("DVD JSON exceeds 100000 items")};
    const auto directory=QFileInfo(path).absoluteDir();
    QList<DvdItemViewModel> items;
    for(int i=0;i<entries.size();++i) {
        if(!entries[i].isObject()) return {{},QStringLiteral("DVD item %1 is not an object").arg(i)};
        const auto entry=entries[i].toObject();
        if(!entry["stableId"].isString()
            || (entry.contains("coverPath") && !entry["coverPath"].isString())
            || (entry.contains("title") && !entry["title"].isString()))
            return {{},QStringLiteral("DVD item %1 has invalid text fields").arg(i)};
        if((entry.contains("displayOrder") && !entry["displayOrder"].isDouble())
            || (entry.contains("selected") && !entry["selected"].isBool()))
            return {{},QStringLiteral("DVD item %1 has invalid order/selection fields").arg(i)};
        const auto order=entry["displayOrder"].toDouble(i);
        if(!std::isfinite(order) || order!=std::floor(order)
            || order<std::numeric_limits<int>::min() || order>std::numeric_limits<int>::max())
            return {{},QStringLiteral("DVD item %1 has invalid displayOrder").arg(i)};
        DvdItemViewModel item;
        item.stableId=entry["stableId"].toString();
        item.title=entry["title"].toString();
        item.coverPath=entry["coverPath"].toString();
        if(!item.coverPath.isEmpty() && QFileInfo(item.coverPath).isRelative())
            item.coverPath=directory.absoluteFilePath(item.coverPath);
        item.displayOrder=int(order);
        item.selected=entry["selected"].toBool(false);
        items.push_back(std::move(item));
    }
    DvdShelfController validation;
    QString message;
    if(!validation.setItems(items,&message)) return {{},message};
    return {validation.items(),{}};
}
} // namespace darkeye::dvd_scene
