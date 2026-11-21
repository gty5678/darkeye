#include "MacWorkspaceShortcuts.h"

#import <AppKit/AppKit.h>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QKeySequenceEdit>
#include <QWidget>

namespace darkeye
{
namespace
{

QKeySequence workspaceKey(NSEvent *event)
{
    constexpr auto shortcutModifiers = NSEventModifierFlagCommand | NSEventModifierFlagShift
        | NSEventModifierFlagControl | NSEventModifierFlagOption | NSEventModifierFlagNumericPad;
    const auto modifiers = event.modifierFlags & shortcutModifiers;
    if (!(modifiers & NSEventModifierFlagCommand) || (modifiers & NSEventModifierFlagControl))
        return {};

    NSString *characters = event.charactersIgnoringModifiers;
    if (modifiers == (NSEventModifierFlagCommand | NSEventModifierFlagShift)) {
        // Cocoa keeps Shift in charactersIgnoringModifiers. Translate the key
        // in the current layout without Shift to recover keys like 1 and [.
        characters = [event charactersByApplyingModifiers:NSEventModifierFlagCommand];
        if (characters.length != 1) return {};
        const auto key = [characters characterAtIndex:0];
        if ((key >= '0' && key <= '9') || key == '[' || key == ']')
            return QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key(key));
    }
    if (characters.length != 1) return {};

    const auto key = [characters characterAtIndex:0];
    if (modifiers == NSEventModifierFlagCommand && key == ',')
        return QKeySequence(Qt::CTRL | Qt::Key_Comma);

    // Cocoa marks the dedicated arrow keys as numeric-pad keys too.
    if ((modifiers & ~NSEventModifierFlagNumericPad)
        == (NSEventModifierFlagCommand | NSEventModifierFlagOption)) {
        if (key == NSLeftArrowFunctionKey) return QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Left);
        if (key == NSRightArrowFunctionKey) return QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Right);
    }
    return {};
}

} // namespace

void installMacWorkspaceShortcuts(QWidget &window, const QActionGroup &actions)
{
    auto *owner = &window;
    auto *shortcutActions = &actions;
    // Handle these combinations before the input method or Qt's layout mapper
    // can lose the key used by the registered shortcut.
    const id monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
        handler:^NSEvent *(NSEvent *event) {
            if (!owner->isActiveWindow() || QApplication::activePopupWidget()
                || QApplication::activeModalWidget())
                return event;
            for (auto *focus = QApplication::focusWidget(); focus; focus = focus->parentWidget()) {
                if (qobject_cast<QKeySequenceEdit *>(focus)) return event;
            }
            const auto key = workspaceKey(event);
            if (key.isEmpty()) return event;

            QAction *match = nullptr;
            for (auto *action : shortcutActions->actions()) {
                if (!action->isEnabled() || !action->isVisible()) continue;
                const auto shortcut = action->shortcut();
                if (shortcut.isEmpty() || shortcut[0] != key[0]) continue;
                if (match || shortcut.count() != 1) return event;
                match = action;
            }
            if (!match) return event;

            if (!event.isARepeat || match->autoRepeat())
                QMetaObject::invokeMethod(match, &QAction::trigger, Qt::QueuedConnection);
            return nil;
        }];
    QObject::connect(owner, &QObject::destroyed, [monitor] { [NSEvent removeMonitor:monitor]; });
}

} // namespace darkeye
