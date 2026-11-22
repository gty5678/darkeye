#pragma once
#include "DvdItemViewModel.h"
#include <QList>

namespace darkeye::dvd_scene {
// Standalone adapter. The future business-page adapter can produce the same
// value objects without exposing SQLite or domain Work objects to the renderer.
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
