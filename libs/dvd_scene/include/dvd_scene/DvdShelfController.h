#pragma once
#include "DvdItemViewModel.h"
#include <QList>
#include <QHash>
#include <utility>

namespace darkeye::dvd_scene {
class DvdShelfController {
public:
    static constexpr float spacing = 0.0145f;
    static constexpr int maxVisibleItems = 60;
    bool setItems(QList<DvdItemViewModel> items, QString *error = nullptr);
    bool updateItem(const DvdItemViewModel &item, QString *error = nullptr);
    [[nodiscard]] const QList<DvdItemViewModel> &items() const { return m_items; }
    [[nodiscard]] int indexOf(const QString &stableId) const;
    [[nodiscard]] std::pair<int,int> visibleRange(float cameraX) const;
    [[nodiscard]] float clampCameraX(float cameraX) const;
private:
    QList<DvdItemViewModel> m_items;
    QHash<QString,int> m_indices;
};
} // namespace darkeye::dvd_scene
