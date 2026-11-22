#include "darkeye_ui/base/LazyWidget.h"

#include <QShowEvent>

namespace darkeye
{

LazyWidget::LazyWidget(QWidget *parent) : QWidget(parent)
{
}

bool LazyWidget::isInitialized() const noexcept
{
    return m_initialized;
}

void LazyWidget::initialize()
{
    if (m_initialized || m_initializing)
    {
        return;
    }

    m_initializing = true;
    lazyLoad();
    m_initialized = true;
    m_initializing = false;
}

void LazyWidget::showEvent(QShowEvent *event)
{
    initialize();
    QWidget::showEvent(event);
}

} // namespace darkeye
