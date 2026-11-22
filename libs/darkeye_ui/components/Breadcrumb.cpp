#include "darkeye_ui/components/Breadcrumb.h"

#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignLabel.h"

#include <QHBoxLayout>

namespace darkeye {

Breadcrumb::Breadcrumb(const QStringList &items, QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("DesignBreadcrumb"));
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(4);
    setItems(items);
}

QStringList Breadcrumb::items() const { return m_items; }
int Breadcrumb::currentIndex() const { return m_currentIndex; }

void Breadcrumb::setItems(const QStringList &items)
{
    m_items = items;
    m_currentIndex = m_items.isEmpty() ? -1 : m_items.size() - 1;
    rebuild();
}

void Breadcrumb::setCurrentIndex(int index)
{
    m_currentIndex = m_items.isEmpty() ? -1 : qBound(0, index, m_items.size() - 1);
    rebuild();
}

void Breadcrumb::rebuild()
{
    while (QLayoutItem *item = m_layout->takeAt(0)) {
        if (item->widget() != nullptr) item->widget()->deleteLater();
        delete item;
    }
    for (int index = 0; index < m_items.size(); ++index) {
        auto *button = new DesignButton(m_items.at(index), this);
        button->setObjectName(QStringLiteral("DesignBreadcrumbCrumb"));
        if (index == m_currentIndex) {
            button->setVariant(QStringLiteral("primary"));
            button->setEnabled(false);
        } else {
            connect(button, &QPushButton::clicked, this, [this, index] {
                const QString value = m_items.at(index);
                setCurrentIndex(index);
                emit crumbClicked(index, value);
            });
        }
        m_layout->addWidget(button);
        if (index < m_items.size() - 1) {
            auto *separator = new DesignLabel(QStringLiteral("/"), this);
            separator->setObjectName(QStringLiteral("DesignBreadcrumbSeparator"));
            m_layout->addWidget(separator);
        }
    }
    m_layout->addStretch(1);
}

} // namespace darkeye
