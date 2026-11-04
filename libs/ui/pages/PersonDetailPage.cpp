#include "ui/pages/PersonDetailPage.h"

#include "darkeye_ui/components/ToastNotification.h"
#include "ui/components/ActressWorkTimeline.h"
#include "ui/components/PersonInfoPanel.h"
#include "ui/pages/ForceDirectPage.h"

#include <QHBoxLayout>
#include <QScrollArea>
#include <QSizePolicy>
#include <QDesktopServices>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace darkeye
{

PersonDetailPage::PersonDetailPage(PersonKind kind, QSqlDatabase publicDatabase,
                                   QSqlDatabase privateDatabase, ThemeService &themes,
                                   QString imageDirectory, QWidget *parent,
                                   QString coverDirectory, graph::GraphManager *graphManager)
    : LazyWidget(parent), m_kind(kind), m_repository(std::move(publicDatabase)),
      m_privateRepository(std::move(privateDatabase)), m_themes(themes),
      m_imageDirectory(std::move(imageDirectory)), m_coverDirectory(std::move(coverDirectory))
{
    if (m_kind == PersonKind::Actress && graphManager != nullptr)
    {
        m_relationGraph = new ForceDirectPage(m_themes, *graphManager, this,
                                             m_imageDirectory, m_coverDirectory);
        m_relationGraph->setObjectName(QStringLiteral("actressRelationGraph"));
        m_relationGraph->setMinimumSize(320, 280);
        m_relationGraph->setFavoriteFilterToggleVisible(false);
        m_relationGraph->showEmptyGraph();
        connect(m_relationGraph, &ForceDirectPage::workRequested,
                this, &PersonDetailPage::workRequested);
        connect(m_relationGraph, &ForceDirectPage::actressRequested,
                this, [this](qint64 id) { showPerson(id); });
    }
}

void PersonDetailPage::lazyLoad()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    if (m_kind == PersonKind::Actress)
    {
        auto *infoRow = new QHBoxLayout;
        infoRow->setContentsMargins(0, 0, 0, 0);
        infoRow->setSpacing(0);
        m_panel = new PersonInfoPanel(m_themes, std::move(m_imageDirectory), this);
        m_panel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        if (m_relationGraph == nullptr) infoRow->addStretch();
        infoRow->addWidget(m_panel, 0, Qt::AlignTop);
        if (m_relationGraph != nullptr)
            infoRow->addWidget(m_relationGraph, 1);
        else
            infoRow->addStretch();
        root->addLayout(infoRow);

        m_timeline = new ActressWorkTimeline(this, std::move(m_coverDirectory));
        m_timeline->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        root->addWidget(m_timeline, 1);
    }
    else
    {
        auto *scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        m_panel = new PersonInfoPanel(m_themes, std::move(m_imageDirectory), scroll);
        scroll->setWidget(m_panel);
        root->addWidget(scroll);
    }

    connect(m_panel, &PersonInfoPanel::favoriteChanged, this, &PersonDetailPage::toggleFavorite);
    connect(m_panel, &PersonInfoPanel::editRequested, this,
            [this](qint64 personId) { emit editRequested(m_kind, personId); });
    connect(m_panel, &PersonInfoPanel::workRequested, this, &PersonDetailPage::workRequested);
    if (m_timeline != nullptr)
        connect(m_timeline, &ActressWorkTimeline::workRequested,
                this, &PersonDetailPage::workRequested);
    connect(m_panel, &PersonInfoPanel::actressExternalSearchRequested, this,
            [](const QString &name) {
        QUrl url(QStringLiteral("https://www.minnano-av.com/search_result.php"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("search_scope"), QStringLiteral("actress"));
        query.addQueryItem(QStringLiteral("search_word"), name);
        query.addQueryItem(QStringLiteral("search"), QStringLiteral(" Go"));
        url.setQuery(query);
        QDesktopServices::openUrl(url);
    });
}

bool PersonDetailPage::showPerson(qint64 personId)
{
    initialize();
    QString errorMessage;
    const auto details = m_repository.findDetails(m_kind, personId, &errorMessage);
    if (!details.has_value())
    {
        Toast::showError(window(),
                         errorMessage.isEmpty() ? QStringLiteral("人物不存在") : errorMessage,
                         &m_themes);
        return false;
    }
    m_details = details;
    const bool favorite = m_kind == PersonKind::Actress &&
                          m_privateRepository.isFavoriteActress(personId, &errorMessage);
    m_panel->setDetails(*details, favorite);
    if (m_timeline != nullptr)
        m_timeline->setWorks(details->works);
    if (m_relationGraph != nullptr)
        m_relationGraph->setEgoGraph(QStringLiteral("a%1").arg(personId), 3);
    return true;
}

QWidget *PersonDetailPage::captureContent()
{
    initialize();
    return m_panel;
}

qint64 PersonDetailPage::currentPersonId() const noexcept
{
    return m_details.has_value() ? m_details->id : 0;
}

PersonKind PersonDetailPage::kind() const noexcept
{
    return m_kind;
}

void PersonDetailPage::toggleFavorite(bool favorite)
{
    if (m_kind != PersonKind::Actress || !m_details.has_value())
        return;
    QString errorMessage;
    bool success = false;
    if (favorite)
    {
        const QString japaneseName =
            m_details->names.isEmpty() ? QString() : m_details->names.first().japanese.trimmed();
        if (japaneseName.isEmpty())
        {
            errorMessage = QStringLiteral("收藏女演员前需要填写日文名");
        }
        else
        {
            success =
                m_privateRepository.addFavoriteActress(m_details->id, japaneseName, &errorMessage);
        }
    }
    else
    {
        success = m_privateRepository.removeFavoriteActress(m_details->id, &errorMessage);
    }
    if (!success)
    {
        m_panel->setDetails(*m_details, !favorite);
        Toast::showError(window(), errorMessage, &m_themes);
        return;
    }
    emit favoriteChanged(m_details->id, favorite);
}

} // namespace darkeye
