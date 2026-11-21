#include "ui/pages/PersonDetailPage.h"
#include "ui/pages/PageScrollArea.h"

#include "darkeye_ui/components/ToastNotification.h"
#include "ui/components/PersonInfoPanel.h"

#include <QDesktopServices>
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
    root->setContentsMargins(16, 16, 16, 16);

    m_panel = new PersonInfoPanel(m_themes, std::move(m_imageDirectory), this);
    auto *cardRow = new QHBoxLayout;
    cardRow->addStretch();
    cardRow->addWidget(m_panel, 1);
    cardRow->addStretch();
    root->addLayout(cardRow);
    root->addStretch();

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
