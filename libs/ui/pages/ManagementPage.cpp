#include "ui/pages/ManagementPage.h"

#include "darkeye_ui/theme/ThemeService.h"
#include "ui/pages/management/AddWorkTabPage3.h"
#include "ui/pages/management/MakerManagementWidget.h"
#include "ui/pages/management/LabelManagementWidget.h"
#include "ui/pages/management/PersonalRecordManagementWidget.h"
#include "ui/pages/management/ReferenceManagementWidget.h"
#include "ui/pages/management/SummaryQueryWidget.h"
#include "ui/pages/management/TagManagementWidget.h"
#include "ui/pages/management/WorkBatchStateWidget.h"
#include "ui/pages/management/WorkMaintenanceWidget.h"

#include <QTabWidget>
#include <QTabBar>
#include <QWheelEvent>
#include <QVBoxLayout>

#include <functional>

namespace darkeye
{

namespace
{

class ManagementTabBar final : public QTabBar
{
protected:
    void wheelEvent(QWheelEvent *event) override
    {
        // QTabWidget may restore a child's focus while changing pages.
        // Keep both the switch and subsequent keyboard input on the tabs.
        setFocus(Qt::MouseFocusReason);
        QTabBar::wheelEvent(event);
        setFocus(Qt::MouseFocusReason);
    }
};

class ManagementTabWidget final : public QTabWidget
{
public:
    explicit ManagementTabWidget(QWidget *parent) : QTabWidget(parent)
    {
        // setTabBar() can polish the bar immediately when a theme is already
        // installed. Set the final selector name before that first polish.
        setObjectName(QStringLiteral("ManagementTabs"));
        setTabBar(new ManagementTabBar);
        tabBar()->setObjectName(QStringLiteral("DesignTabBar"));
        // The token tabs use only the selected underline; Qt's native base
        // otherwise leaves an extra grey edge above the transparent tabs.
        tabBar()->setDrawBase(false);
    }
};

class DeferredTab final : public LazyWidget
{
public:
    using Factory = std::function<QWidget *(QWidget *parent)>;

    explicit DeferredTab(Factory factory, QWidget *parent = nullptr)
        : LazyWidget(parent), m_factory(std::move(factory))
    {
    }

private:
    void lazyLoad() override
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(m_factory(this));
    }

    Factory m_factory;
};

} // namespace

ManagementPage::ManagementPage(QSqlDatabase database, ThemeService &themes,
                               CrawlerScheduler &crawlerScheduler, QSqlDatabase privateDatabase,
                               QString coverDirectory,
                               QString actressImageDirectory, QString fanartDirectory, QUrl imageFetchEndpoint,
                               QUrl topActressesEndpoint, graph::GraphManager *graphManager,
                               QWidget *parent, QString actressPortraitDirectory, QString actorPortraitDirectory)
    : LazyWidget(parent), m_database(std::move(database)), m_privateDatabase(std::move(privateDatabase)), m_themes(themes),
      m_actressPortraitDirectory(std::move(actressPortraitDirectory)),
      m_actorPortraitDirectory(std::move(actorPortraitDirectory)),
      m_coverDirectory(std::move(coverDirectory)),
      m_actressImageDirectory(std::move(actressImageDirectory)),
      m_fanartDirectory(std::move(fanartDirectory)),
      m_imageFetchEndpoint(std::move(imageFetchEndpoint)),
      m_topActressesEndpoint(std::move(topActressesEndpoint)),
      m_crawlerScheduler(crawlerScheduler), m_graphManager(graphManager)
{
}

void ManagementPage::lazyLoad()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    m_tabs = new ManagementTabWidget(this);

    const auto addDeferredTab = [this](const QString &title, DeferredTab::Factory factory)
    {
        m_tabs->addTab(new DeferredTab(std::move(factory), m_tabs), title);
    };
    // Python puts AddWorkTabPage3 itself in the first tab, so keep the editor
    // directly attached here and let its first showEvent perform lazy loading.
    m_workEditor = new AddWorkTabPage3(m_database, m_themes, m_crawlerScheduler,
                                       m_coverDirectory, m_fanartDirectory,
                                       m_imageFetchEndpoint, m_graphManager, m_tabs,
                                       m_actressPortraitDirectory, m_actorPortraitDirectory);
    connect(m_workEditor, &AddWorkTabPage3::workSaved, this, [this](qint64 workId, bool)
    {
        if (m_softDelete != nullptr)
            m_softDelete->refresh();
        emit workChanged(workId);
    });
    connect(m_workEditor, &AddWorkTabPage3::actressesCreated, this,
            &ManagementPage::actressesCreated);
    connect(m_workEditor, &AddWorkTabPage3::workLinkRequested, this,
            &ManagementPage::workRequested);
    connect(m_workEditor, &AddWorkTabPage3::actressLinkRequested, this,
            &ManagementPage::actressRequested);
    connect(m_workEditor, &AddWorkTabPage3::personDetailRequested, this,
            &ManagementPage::personDetailRequested);
    connect(m_workEditor, &AddWorkTabPage3::personEditRequested, this,
            &ManagementPage::personEditRequested);
    connect(this, &ManagementPage::referencesChanged, m_workEditor,
            &AddWorkTabPage3::refreshReferences);
    connect(this, &ManagementPage::worksChanged, m_workEditor,
            &AddWorkTabPage3::refreshCompletions);
    connect(this, &ManagementPage::tagsChanged, m_workEditor,
            &AddWorkTabPage3::refreshAssociations);
    connect(this, &ManagementPage::actressesChanged, m_workEditor,
            &AddWorkTabPage3::refreshAssociations);
    m_tabs->addTab(m_workEditor, QStringLiteral("添加/修改作品"));
    addDeferredTab(QStringLiteral("作品标签管理"), [this](QWidget *parent)
    {
        auto *tagPage = new QWidget(parent);
        auto *layout = new QVBoxLayout(tagPage);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *tagManagement = new TagManagementWidget(m_database, m_themes, tagPage);
        layout->addWidget(tagManagement);
        connect(tagManagement, &TagManagementWidget::tagsChanged, this, &ManagementPage::tagsChanged);
        connect(tagManagement, &TagManagementWidget::tagTypesChanged, this,
                &ManagementPage::tagsChanged);
        return tagPage;
    });
    addDeferredTab(QStringLiteral("番号/片商管理"), [this](QWidget *parent)
    {
        auto *page = new MakerManagementWidget(m_database, m_themes, parent);
        connect(page, &MakerManagementWidget::referencesChanged, this,
                &ManagementPage::referencesChanged);
        connect(page, &MakerManagementWidget::prefixesChanged, this,
                [this] { emit referencesChanged(ReferenceKind::Maker); });
        return page;
    });
    const auto addReferenceTab = [this, &addDeferredTab](ReferenceKind kind, const QString &title)
    {
        addDeferredTab(title, [this, kind](QWidget *parent)
        {
            auto *page = new LabelManagementWidget(kind, m_database, m_themes, parent);
            connect(page, &LabelManagementWidget::referencesChanged, this,
                    &ManagementPage::referencesChanged);
            return page;
        });
    };
    addReferenceTab(ReferenceKind::Label, QStringLiteral("厂牌管理"));
    addReferenceTab(ReferenceKind::Series, QStringLiteral("系列管理"));
    addDeferredTab(QStringLiteral("批量操作"), [this](QWidget *parent)
    {
        auto *page = new WorkMaintenanceWidget(m_database, m_themes, m_coverDirectory,
                                               m_actressImageDirectory, m_imageFetchEndpoint,
                                               m_topActressesEndpoint, parent);
        connect(page, &WorkMaintenanceWidget::worksChanged, this, &ManagementPage::worksChanged);
        connect(page, &WorkMaintenanceWidget::actressesChanged, this,
                &ManagementPage::actressesChanged);
        return page;
    });
    addDeferredTab(QStringLiteral("汇总查询表"), [this](QWidget *parent)
    {
        auto *page = new SummaryQueryWidget(m_database, m_themes, parent);
        connect(page, &SummaryQueryWidget::workRequested, this,
                [this](qint64 workId) { loadWork(workId); });
        return page;
    });
    addDeferredTab(QStringLiteral("综合管理"), [this](QWidget *parent)
    {
        return new PersonalRecordManagementWidget(m_privateDatabase, m_themes, parent);
    });
    addDeferredTab(QStringLiteral("作品软删除"), [this](QWidget *parent)
    {
        m_softDelete = new WorkBatchStateWidget(WorkStateMode::Active, m_database, m_themes,
                                                {}, {}, parent);
        connect(m_softDelete, &WorkBatchStateWidget::worksChanged, this, [this]
        {
            if (m_recycleBin != nullptr)
                m_recycleBin->refresh();
            emit worksChanged();
        });
        return m_softDelete;
    });
    addDeferredTab(QStringLiteral("回收站"), [this](QWidget *parent)
    {
        m_recycleBin = new WorkBatchStateWidget(WorkStateMode::RecycleBin, m_database, m_themes,
                                                m_coverDirectory, m_fanartDirectory, parent);
        connect(m_recycleBin, &WorkBatchStateWidget::worksChanged, this, [this]
        {
            if (m_softDelete != nullptr)
                m_softDelete->refresh();
            emit worksChanged();
        });
        return m_recycleBin;
    });
    root->addWidget(m_tabs);
}

void ManagementPage::ensureTabLoaded(int index)
{
    if (index < 0)
        return;

    if (auto *tab = dynamic_cast<LazyWidget *>(m_tabs->widget(index)); tab != nullptr)
        tab->initialize();
}

bool ManagementPage::loadWork(qint64 workId)
{
    initialize();
    ensureTabLoaded(WorkEditorTab);
    if (m_workEditor == nullptr)
        return false;
    // Python's ManagementPage delegates work hydration to AddWorkTabPage3's
    // thread-pool loader.  Keep the route responsive while its details load.
    m_workEditor->loadWorkAsync(workId);
    m_tabs->setCurrentIndex(WorkEditorTab);
    return true;
}

void ManagementPage::refreshPersonSelectors()
{
    if (m_workEditor != nullptr)
        m_workEditor->refreshAssociations();
}

} // namespace darkeye
