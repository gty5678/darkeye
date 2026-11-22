#pragma once
#include <QString>

namespace darkeye::dvd_scene {
struct DvdItemViewModel {
    QString stableId;
    QString coverPath;
    QString title;
    int displayOrder = 0;
    bool selected = false;
    bool hasLocalVideo = false;
};
} // namespace darkeye::dvd_scene
