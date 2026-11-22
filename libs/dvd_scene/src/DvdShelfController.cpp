#include "dvd_scene/DvdShelfController.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace darkeye::dvd_scene {
bool DvdShelfController::setItems(QList<DvdItemViewModel> items, QString *error)
{
    QHash<QString,int> indices;
    if (items.size() > std::numeric_limits<int>::max()) {
        if (error) *error = QStringLiteral("Too many DVD items");
        return false;
    }
    std::stable_sort(items.begin(),items.end(),[](const auto &a,const auto &b) {
        return a.displayOrder < b.displayOrder;
    });
    for (int i=0;i<items.size();++i) {
        const auto &id=items[i].stableId;
        if (id.isEmpty() || indices.contains(id)) {
            if (error) *error = QStringLiteral("DVD IDs must be nonempty and unique: %1").arg(id);
            return false;
        }
        indices.insert(id,i);
    }
    m_items=std::move(items);
    m_indices=std::move(indices);
    return true;
}

bool DvdShelfController::updateItem(const DvdItemViewModel &item, QString *error)
{
    const int index=indexOf(item.stableId);
    if (index<0) {
        if (error) *error=QStringLiteral("Unknown DVD ID: %1").arg(item.stableId);
        return false;
    }
    auto items=m_items;
    items[index]=item;
    return setItems(std::move(items),error);
}
int DvdShelfController::indexOf(const QString &id) const { return m_indices.value(id,-1); }

std::pair<int,int> DvdShelfController::visibleRange(float cameraX) const
{
    const int count=std::min(int(m_items.size()),maxVisibleItems);
    const int center=int(std::round(clampCameraX(cameraX)/spacing));
    const int start=std::clamp(center-count/2,0,int(m_items.size())-count);
    return {start,count};
}

float DvdShelfController::clampCameraX(float cameraX) const
{
    if (!std::isfinite(cameraX)) return 0;
    return std::clamp(cameraX,0.0f,std::max(0,int(m_items.size())-1)*spacing);
}
} // namespace darkeye::dvd_scene
