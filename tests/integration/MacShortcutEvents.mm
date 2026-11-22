#import <AppKit/AppKit.h>

#include <QWidget>

void postMacShortcut(QWidget &window, unsigned short keyCode, const char *characters,
                     Qt::KeyboardModifiers modifiers)
{
    auto *view = reinterpret_cast<NSView *>(window.winId());
    auto *text = [NSString stringWithUTF8String:characters];
    NSEventModifierFlags flags = NSEventModifierFlagCommand;
    if (modifiers.testFlag(Qt::ShiftModifier)) flags |= NSEventModifierFlagShift;
    if (modifiers.testFlag(Qt::AltModifier)) flags |= NSEventModifierFlagOption;
    if (modifiers.testFlag(Qt::KeypadModifier)) flags |= NSEventModifierFlagNumericPad;
    auto *event = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                  location:NSZeroPoint
                             modifierFlags:flags
                                 timestamp:NSProcessInfo.processInfo.systemUptime
                              windowNumber:view.window.windowNumber
                                   context:nil
                                characters:text
               charactersIgnoringModifiers:text
                                 isARepeat:NO
                                   keyCode:keyCode];
    [NSApp postEvent:event atStart:NO];
}
