#include "MacWindowAppearance.h"

#import <AppKit/AppKit.h>

#include <QWidget>

namespace darkeye
{

void hideMacTitleBarSeparator(QWidget &window)
{
    // The Cocoa platform plugin exposes the native NSView as the widget ID.
    auto *view = reinterpret_cast<NSView *>(window.winId());
    view.window.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;
}

}
