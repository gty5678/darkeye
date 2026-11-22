#include "darkeye_ui/components/ModalDialog.h"

#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignLabel.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

namespace darkeye {

ModalDialog::ModalDialog(const QString &title, const QString &message,
                         const QString &confirmText, const QString &cancelText,
                         bool showCancel, bool danger, ThemeService *, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("DesignModalDialog"));
    setModal(true);
    setWindowTitle(title.isEmpty() ? QStringLiteral("Notice") : title);
    setMinimumWidth(420);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    auto *titleLabel = new DesignLabel(windowTitle(), this);
    titleLabel->setObjectName(QStringLiteral("DesignModalTitle"));
    titleLabel->setWordWrap(true);
    auto *messageLabel = new DesignLabel(message, this);
    messageLabel->setObjectName(QStringLiteral("DesignModalMessage"));
    messageLabel->setWordWrap(true);
    messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *confirmButton = new DesignButton(confirmText, this);
    confirmButton->setObjectName(QStringLiteral("DesignModalConfirm"));
    confirmButton->setVariant(danger ? QStringLiteral("danger")
                                     : QStringLiteral("primary"));
    auto *cancelButton = new DesignButton(cancelText, this);
    cancelButton->setObjectName(QStringLiteral("DesignModalCancel"));
    cancelButton->setVisible(showCancel);
    connect(confirmButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    auto *footer = new QHBoxLayout;
    footer->addStretch();
    footer->addWidget(cancelButton);
    footer->addWidget(confirmButton);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    layout->addWidget(titleLabel);
    layout->addWidget(messageLabel);
    layout->addLayout(footer);
}

bool ModalDialog::confirm(QWidget *parent, const QString &title,
                          const QString &message, ThemeService *themes,
                          const QString &confirmText, const QString &cancelText)
{
    ModalDialog dialog(title, message, confirmText, cancelText, true, false,
                       themes, parent);
    return dialog.exec() == QDialog::Accepted;
}

bool ModalDialog::dangerConfirm(QWidget *parent, const QString &title,
                                const QString &message, ThemeService *themes,
                                const QString &confirmText,
                                const QString &cancelText)
{
    ModalDialog dialog(title, message, confirmText, cancelText, true, true,
                       themes, parent);
    return dialog.exec() == QDialog::Accepted;
}

} // namespace darkeye
