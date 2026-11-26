#pragma once

#include <QPalette>

#include <optional>

class QPainter;
class QRectF;

namespace mdedit {

struct ResourceSnapshot;

void paintResource(QPainter &painter, const ResourceSnapshot &data, const QRectF &content,
                   const std::optional<QPalette> &theme = {});

} // namespace mdedit
