#include "ui/pages/StatisticsPage.h"

#include "ui/pages/PersonalDataPage.h"
#include "ui/pages/PlotTabPage.h"

#include <QVBoxLayout>
#include <QScrollArea>

namespace darkeye
{

StatisticsPage::StatisticsPage(QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                               ThemeService &themeService, QString actressImageDirectory,
                               QWidget *parent, Panel panel)
    : LazyWidget(parent), m_panel(panel), m_publicDatabase(std::move(publicDatabase)),
      m_privateDatabase(std::move(privateDatabase)), m_themeService(themeService),
      m_actressImageDirectory(std::move(actressImageDirectory))
{
}

void StatisticsPage::lazyLoad()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("StatisticsScrollArea"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    scroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    if (m_panel == Panel::Information)
    {
        m_personalData = new PersonalDataPage(m_publicDatabase, m_privateDatabase,
                                              m_themeService, m_actressImageDirectory);
        connect(m_personalData, &PersonalDataPage::actressDetailRequested, this,
                &StatisticsPage::actressDetailRequested);
        connect(m_personalData, &PersonalDataPage::actressEditRequested, this,
                &StatisticsPage::actressEditRequested);
        m_personalData->initialize();
        scroll->setWidget(m_personalData);
    }
    else
    {
        m_plots = new PlotTabPage(m_publicDatabase, m_privateDatabase, m_themeService);
        m_plots->initialize();
        scroll->setWidget(m_plots);
    }
    layout->addWidget(scroll);
}

QWidget *StatisticsPage::captureContent()
{
    initialize();
    if (m_plots) return m_plots->captureContent();
    return m_personalData;
}

void StatisticsPage::refresh()
{
    initialize();
    if (m_personalData) m_personalData->refresh();
    if (m_plots) m_plots->refresh();
}

} // namespace darkeye
