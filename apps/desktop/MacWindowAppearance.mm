#include "MacWindowAppearance.h"

#import <AppKit/AppKit.h>

#include <QColor>
#include <QColorSpace>
#include <QGuiApplication>
#include <QWidget>

namespace darkeye
{

void setMacTitleBarBackground(QWidget &window, const QColor &color,
                              const QColorSpace &colorSpace, bool dark)
{
    if (QGuiApplication::platformName() != QStringLiteral("cocoa")) return;

    // The Cocoa platform plugin exposes the native NSView as the widget ID.
    auto *view = reinterpret_cast<NSView *>(window.internalWinId());
    NSWindow *nativeWindow = view.window;
    nativeWindow.appearance = [NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
    nativeWindow.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;
    nativeWindow.backgroundColor = colorSpace == QColorSpace::DisplayP3
        ? [NSColor colorWithDisplayP3Red:color.redF() green:color.greenF() blue:color.blueF() alpha:1]
        : [NSColor colorWithSRGBRed:color.redF() green:color.greenF() blue:color.blueF() alpha:1];
}

}
