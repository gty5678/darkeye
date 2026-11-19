#include "ui/pages/HomePage.h"
#include "ui/pages/PageScrollArea.h"

#include "app/Resources.h"
#include "ui/components/ShopSceneWidget.h"
#include "database/repositories/WorkRepository.h"
#include "darkeye_ui/components/InteractionEffects.h"
#include "darkeye_ui/theme/ThemeService.h"
#include "darkeye_ui/components/ToastNotification.h"

#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QRandomGenerator>

namespace darkeye {

HomePage::HomePage(QSqlDatabase database, const QString &coverDirectory, ThemeService &themes,
                   QWidget *parent)
    : LazyWidget(parent), m_themes(themes), m_database(std::move(database)),
      m_coverDirectory(coverDirectory)
{
    setObjectName(QStringLiteral("HomePage"));
    connect(&m_themes, &ThemeService::themeChanged, this, &HomePage::updateOutlineColor);
}

void HomePage::lazyLoad()
{
    Resources::ensureInitialized();
    auto *root = new QGridLayout(addPageScrollArea(this));
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(10, 10, 10, 10);
    bar->addStretch();
    auto *reload = new RotateButton(QStringLiteral("refresh_cw"), {}, 24, 24, true,
                                    &m_themes, this);
    reload->setToolTip(QStringLiteral("刷新"));
    reload->setAccessibleName(QStringLiteral("刷新"));
    reload->setObjectName(QStringLiteral("HomeRefreshButton"));
    reload->setStyleSheet(QStringLiteral(
        "QPushButton#HomeRefreshButton { background: transparent; border: none; }"));
    bar->addWidget(reload);

    m_view = new ShopSceneWidget(this);
    m_view->setObjectName(QStringLiteral("HomeShopView"));
    m_view->setMinimumHeight(360);
    root->addWidget(m_view, 0, 0);
    root->addLayout(bar, 0, 0, Qt::AlignTop);
    reload->raise();
    connect(reload, &QPushButton::clicked, this, &HomePage::refresh);
    connect(m_view, &ShopSceneWidget::coverClicked, this, &HomePage::openCover);
    connect(m_view, &ShopSceneWidget::sceneError, this, [this](const QString &message) {
        ToastNotification::showMessage(window(), message, ToastNotification::Level::Error);
    });
    const auto modelError = m_view->canvas()->property("modelError").toString();
    if (!modelError.isEmpty())
        ToastNotification::showMessage(window(), modelError, ToastNotification::Level::Error);
    updateOutlineColor();
    refresh();
}

void HomePage::updateOutlineColor()
{
    if (m_view != nullptr)
        m_view->setOutlineColor(QColor(m_themes.currentTokens().primary));
}

void HomePage::openCover(int index)
{
    if (index >= 0 && index < m_works.size()) emit detailRequested(m_works.at(index).id);
}

void HomePage::refresh()
{
    WorkSearch search;
    search.limit = 18;
    search.order = WorkSortOrder::Random;
    search.randomSeed = QRandomGenerator::global()->generate();
    search.randomSeed2 = QRandomGenerator::global()->generate();
    QString error;
    const auto works = WorkRepository(m_database).search(search, &error);
    if (!error.isEmpty())
    {
        ToastNotification::showMessage(window(), QStringLiteral("刷新失败：%1").arg(error),
                                       ToastNotification::Level::Error);
        return;
    }
    m_works = works;
    QStringList textures;
    for (const WorkSummary &work : m_works)
    {
        const QString path = QDir(m_coverDirectory).filePath(work.imageUrl);
        textures.append(!work.imageUrl.isEmpty() && QFileInfo::exists(path)
            ? path : QString());
    }
    m_view->setCoverPaths(textures);
}

} // namespace darkeye
