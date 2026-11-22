#include "darkeye_ui/components/EmptyState.h"

#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignLabel.h"

#include <QVBoxLayout>

namespace darkeye {

EmptyState::EmptyState(const QString &title, const QString &description,
                       const QString &actionText, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("DesignEmptyState"));
    auto *layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);

    m_icon = new DesignLabel(QStringLiteral("○"), this);
    m_icon->setObjectName(QStringLiteral("DesignEmptyStateIcon"));
    m_icon->setAlignment(Qt::AlignCenter);
    m_title = new DesignLabel(title, this);
    m_title->setObjectName(QStringLiteral("DesignEmptyStateTitle"));
    m_title->setAlignment(Qt::AlignCenter);
    m_description = new DesignLabel(description, this);
    m_description->setObjectName(QStringLiteral("DesignEmptyStateDescription"));
    m_description->setAlignment(Qt::AlignCenter);
    m_description->setWordWrap(true);
    m_action = new DesignButton(actionText, this);
    m_action->setObjectName(QStringLiteral("DesignEmptyStateAction"));
    static_cast<DesignButton *>(m_action)->setVariant(QStringLiteral("primary"));
    connect(m_action, &QPushButton::clicked, this, &EmptyState::actionTriggered);

    layout->addStretch();
    layout->addWidget(m_icon);
    layout->addWidget(m_title);
    layout->addWidget(m_description);
    layout->addWidget(m_action, 0, Qt::AlignCenter);
    layout->addStretch();
    setActionText(actionText);
}

void EmptyState::setTitle(const QString &title) { m_title->setText(title); }
void EmptyState::setDescription(const QString &description)
{
    m_description->setText(description);
}
void EmptyState::setIconText(const QString &text) { m_icon->setText(text); }
void EmptyState::setActionText(const QString &text)
{
    m_action->setText(text);
    m_action->setVisible(!text.trimmed().isEmpty());
}

} // namespace darkeye
