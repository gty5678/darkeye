#pragma once
#include "dvd_scene/DvdItemViewModel.h"
#include <QList>

namespace darkeye::dvd_scene {
struct DvdItemLoadResult {
    QList<DvdItemViewModel> items;
    QString error;
    explicit operator bool() const { return error.isEmpty(); }
};
class DvdSceneAdapter {
public:
    [[nodiscard]] static DvdItemLoadResult loadJson(const QString &path);
};
} // namespace darkeye::dvd_scene
