#pragma once
#include "dvd_scene/DvdItemViewModel.h"
#include <QList>

namespace darkeye::render3d_demo {
struct DvdItemLoadResult {
    QList<dvd_scene::DvdItemViewModel> items;
    QString error;
    explicit operator bool() const { return error.isEmpty(); }
};

[[nodiscard]] DvdItemLoadResult loadDvdItemsFromJson(const QString &path);
} // namespace darkeye::render3d_demo
