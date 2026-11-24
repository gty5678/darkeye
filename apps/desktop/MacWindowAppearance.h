#pragma once

class QColor;
class QColorSpace;
class QWidget;

namespace darkeye
{

void setMacTitleBarBackground(QWidget &window, const QColor &color,
                              const QColorSpace &colorSpace, bool dark);

}
