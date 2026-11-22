#include "ui/pages/PersonDetailPage.h"
#include "ui/pages/PageScrollArea.h"

#include "darkeye_ui/components/ToastNotification.h"
#include "ui/components/PersonInfoPanel.h"

#include <QCoreApplication>
#include <QEvent>
#include <QDesktopServices>
#include <QLabel>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace darkeye
{

PersonDetailPage::PersonDetailPage(QSqlDatabase publicDatabase,
                                   QSqlDatabase privateDatabase, ThemeService &themes,
                                   QString imageDirectory, QWidget *parent)
    : LazyWidget(parent), m_repository(std::move(publicDatabase)),
      m_privateRepository(std::move(privateDatabase)), m_themes(themes),
      m_imageDirectory(std::move(imageDirectory))
{
}

void PersonDetailPage::lazyLoad()
{
    // Keep the card centered in the same viewport when the alias list grows.
    auto *root = new QVBoxLayout(addPageScrollArea(this, nullptr, Qt::ScrollBarAlwaysOn));
    auto *scroll = findChild<QScrollArea *>(QString{}, Qt::FindDirectChildrenOnly);
    root->setContentsMargins(4, 4, 4, 4);

    m_panel = new PersonInfoPanel(m_themes, std::move(m_imageDirectory), this);
    auto *cardRow = new QHBoxLayout;
    cardRow->addStretch();
    cardRow->addWidget(m_panel, 1);
    cardRow->addStretch();
    root->addLayout(cardRow);
    m_emptyHint = new QLabel(QStringLiteral("打开一位女演员后，这里会显示她的详情。"), this);
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setWordWrap(true);
    root->addWidget(m_emptyHint);
    root->addStretch();
    m_panel->hide();

    connect(m_panel, &PersonInfoPanel::aliasesToggled, this, [this, root, scroll](bool expanded) {
        if (expanded || !scroll->isVisible())
            return;

        // Finish shrinking the card and clamping the scroll offset before painting either change.
        auto *viewport = scroll->viewport();
        const bool updatesEnabled = viewport->updatesEnabled();
        viewport->setUpdatesEnabled(false);
        m_panel->layout()->activate();
        root->activate();
        QEvent request(QEvent::LayoutRequest);
        QCoreApplication::sendEvent(scroll, &request);
        viewport->setUpdatesEnabled(updatesEnabled);
    });

    connect(m_panel, &PersonInfoPanel::favoriteChanged, this, &PersonDetailPage::toggleFavorite);
    connect(m_panel, &PersonInfoPanel::editRequested, this,
            [this](qint64 personId) { emit editRequested(PersonKind::Actress, personId); });
    connect(m_panel, &PersonInfoPanel::workRequested, this, &PersonDetailPage::workRequested);
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

bool PersonDetailPage::showPerson(qint64 personId, bool reportError)
{
    QString errorMessage;
    const auto details = m_repository.findDetails(PersonKind::Actress, personId, &errorMessage);
    if (!details.has_value())
    {
        if (reportError)
            Toast::showError(window(),
                             errorMessage.isEmpty() ? QStringLiteral("人物不存在") : errorMessage,
                             &m_themes);
        return false;
    }
    initialize();
    m_details = details;
    const bool favorite = m_privateRepository.isFavoriteActress(personId, &errorMessage);
    m_panel->setDetails(*details, favorite);
    m_emptyHint->hide();
    m_panel->show();
    return true;
}

QWidget *PersonDetailPage::captureContent()
{
    initialize();
    return m_details ? m_panel : nullptr;
}

void PersonDetailPage::clearPerson()
{
    if (!m_details) return;
    m_details.reset();
    m_panel->hide();
    m_emptyHint->show();
}

qint64 PersonDetailPage::currentPersonId() const noexcept
{
    return m_details.has_value() ? m_details->id : 0;
}

QString PersonDetailPage::currentPersonName() const
{
    if (!m_details || m_details->names.isEmpty()) return {};
    const auto &name = m_details->names.first();
    return name.chinese.isEmpty() ? name.japanese : name.chinese;
}

void PersonDetailPage::toggleFavorite(bool favorite)
{
    if (!m_details.has_value())
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
