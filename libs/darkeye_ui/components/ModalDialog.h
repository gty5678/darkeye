#pragma once

#include <QDialog>

namespace darkeye {

class ThemeService;

class ModalDialog : public QDialog
{
public:
    explicit ModalDialog(const QString &title, const QString &message,
                         const QString &confirmText = QStringLiteral("Confirm"),
                         const QString &cancelText = QStringLiteral("Cancel"),
                         bool showCancel = true, bool danger = false,
                         ThemeService *themes = nullptr,
                         QWidget *parent = nullptr);

    static bool confirm(QWidget *parent, const QString &title,
                        const QString &message, ThemeService *themes = nullptr,
                        const QString &confirmText = QStringLiteral("Confirm"),
                        const QString &cancelText = QStringLiteral("Cancel"));
    static bool dangerConfirm(QWidget *parent, const QString &title,
                              const QString &message,
                              ThemeService *themes = nullptr,
                              const QString &confirmText = QStringLiteral("Delete"),
                              const QString &cancelText = QStringLiteral("Cancel"));
};

using Dialog = ModalDialog;
using TokenModalDialog = ModalDialog;

} // namespace darkeye
