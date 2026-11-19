#include "ui/pages/ManagementPage.h"
#include "ui/pages/PageScrollArea.h"

#include "darkeye_ui/theme/ThemeService.h"
#include "ui/pages/management/AddWorkTabPage3.h"
#include "ui/pages/management/MakerManagementWidget.h"
#include "ui/pages/management/LabelManagementWidget.h"
#include "ui/pages/management/PersonalRecordManagementWidget.h"
#include "ui/pages/management/SummaryQueryWidget.h"
#include "ui/pages/management/TagManagementWidget.h"
#include "ui/pages/management/WorkBatchStateWidget.h"

#include <QCloseEvent>
#include <QCoreApplication>

namespace darkeye
{

ManagementPage::ManagementPage(Section section, QSqlDatabase database, ThemeService &themes,
                               CrawlerScheduler &crawlerScheduler, QSqlDatabase privateDatabase,
                               QString coverDirectory,
                               QString fanartDirectory, QUrl imageFetchEndpoint,
                               QWidget *parent, QString actressPortraitDirectory, QString actorPortraitDirectory)
    : LazyWidget(parent), m_section(section), m_database(std::move(database)), m_privateDatabase(std::move(privateDatabase)), m_themes(themes),
      m_actressPortraitDirectory(std::move(actressPortraitDirectory)),
      m_actorPortraitDirectory(std::move(actorPortraitDirectory)),
      m_coverDirectory(std::move(coverDirectory)),
      m_fanartDirectory(std::move(fanartDirectory)),
      m_imageFetchEndpoint(std::move(imageFetchEndpoint)),
      m_crawlerScheduler(crawlerScheduler)
{
}

void ManagementPage::lazyLoad()
{
    if (m_section == Section::WorkEditor)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        m_workEditor = createWorkEditor();
        layout->addWidget(m_workEditor);
        return;
    }

    addPageScrollArea(this, createSection());
}

AddWorkTabPage3 *ManagementPage::createWorkEditor()
{
    auto *editor = new AddWorkTabPage3(m_database, m_themes, m_crawlerScheduler,
                                      m_coverDirectory, m_fanartDirectory,
                                      m_imageFetchEndpoint, this,
                                      m_actressPortraitDirectory, m_actorPortraitDirectory);
    editor->setWorkCreationEnabled(false);
    connect(editor, &AddWorkTabPage3::workSaved, this,
            [this](qint64 workId, bool) { emit workChanged(workId); });
    connect(editor, &AddWorkTabPage3::peopleChanged, this, &ManagementPage::peopleChanged);
    connect(editor, &AddWorkTabPage3::tagsChanged, this, &ManagementPage::tagsChanged);
    connect(editor, &AddWorkTabPage3::referencesChanged, this, &ManagementPage::referencesChanged);
    connect(editor, &AddWorkTabPage3::actressesCreated, this, &ManagementPage::actressesCreated);
    connect(editor, &AddWorkTabPage3::workLinkRequested, this, &ManagementPage::workRequested);
    connect(editor, &AddWorkTabPage3::actressLinkRequested, this, &ManagementPage::actressRequested);
    connect(editor, &AddWorkTabPage3::personDetailRequested, this, &ManagementPage::personDetailRequested);
    connect(editor, &AddWorkTabPage3::personEditRequested, this, &ManagementPage::personEditRequested);
    return editor;
}

QWidget *ManagementPage::createSection()
{
    switch (m_section)
    {
    case Section::Tags:
    {
        auto *page = new TagManagementWidget(m_database, m_themes, this);
        connect(page, &TagManagementWidget::tagsChanged, this, &ManagementPage::tagsChanged);
        connect(page, &TagManagementWidget::tagTypesChanged, this, &ManagementPage::tagsChanged);
        return page;
    }
    case Section::Makers:
    {
        auto *page = new MakerManagementWidget(m_database, m_themes, this);
        connect(page, &MakerManagementWidget::referencesChanged, this, &ManagementPage::referencesChanged);
        connect(page, &MakerManagementWidget::prefixesChanged, this,
                [this] { emit referencesChanged(ReferenceKind::Maker); });
        return page;
    }
    case Section::Labels:
    case Section::Series:
    {
        const auto kind = m_section == Section::Labels ? ReferenceKind::Label : ReferenceKind::Series;
        auto *page = new LabelManagementWidget(kind, m_database, m_themes, this);
        connect(page, &LabelManagementWidget::referencesChanged, this, &ManagementPage::referencesChanged);
        return page;
    }
    case Section::SummaryQuery:
    {
        auto *page = new SummaryQueryWidget(m_database, m_themes, this);
        connect(page, &SummaryQueryWidget::workRequested, this, &ManagementPage::workEditRequested);
        return page;
    }
    case Section::PersonalRecords:
        return new PersonalRecordManagementWidget(m_privateDatabase, m_themes, this);
    case Section::SoftDelete:
    case Section::RecycleBin:
    {
        const bool recycleBin = m_section == Section::RecycleBin;
        m_workState = new WorkBatchStateWidget(recycleBin ? WorkStateMode::RecycleBin : WorkStateMode::Active,
                                              m_database, m_themes, recycleBin ? m_coverDirectory : QString(),
                                              recycleBin ? m_fanartDirectory : QString(), this);
        connect(m_workState, &WorkBatchStateWidget::worksChanged, this, &ManagementPage::worksChanged);
        return m_workState;
    }
    case Section::WorkEditor:
        Q_UNREACHABLE();
    }
    Q_UNREACHABLE();
}

void ManagementPage::loadWork(qint64 workId)
{
    Q_ASSERT(m_section == Section::WorkEditor);
    initialize();
    m_workEditor->loadWorkAsync(workId);
}

void ManagementPage::refreshPersonSelectors()
{
    if (m_workEditor)
        m_workEditor->refreshAssociations();
}

void ManagementPage::refreshWorkLists()
{
    if (m_workEditor)
        m_workEditor->refreshCompletions();
    if (m_workState)
        m_workState->refresh();
}

void ManagementPage::refreshReferences()
{
    if (m_workEditor)
        m_workEditor->refreshReferences(true);
}

void ManagementPage::closeEvent(QCloseEvent *event)
{
    if (m_workEditor)
    {
        QCloseEvent check;
        QCoreApplication::sendEvent(m_workEditor, &check);
        event->setAccepted(check.isAccepted());
    }
    else
    {
        event->accept();
    }
}

} // namespace darkeye
