#include "ui/pages/PlaceholderPage.h"
#include "ui/pages/PageScrollArea.h"

#include <QLabel>
#include <QVBoxLayout>

namespace darkeye {

PlaceholderPage::PlaceholderPage(const QString &title, const QString &routeName, QWidget *parent)
    : LazyWidget(parent), m_title(title), m_routeName(routeName)
{
}

void PlaceholderPage::lazyLoad()
{
    auto *layout = new QVBoxLayout(addPageScrollArea(this));
    layout->setContentsMargins(40, 32, 40, 32);

    auto *heading = new QLabel(m_title, this);
    QFont headingFont = heading->font();
    headingFont.setPointSize(22);
    headingFont.setBold(true);
    heading->setFont(headingFont);

    auto *status = new QLabel(
        QStringLiteral("C++ 迁移页面骨架 · 路由：%1").arg(m_routeName), this);
    status->setStyleSheet(QStringLiteral("color: #71717a;"));

    layout->addWidget(heading);
    layout->addWidget(status);
    layout->addStretch();
}

} // namespace darkeye


