#include "ui/dialogs/AddActressDialog.h"

#include "database/repositories/PersonRepository.h"
#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignInput.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/theme/IconProvider.h"

#include <QDesktopServices>
#include <QGridLayout>
#include <QMessageBox>
#include <QUrl>
#include <QUrlQuery>

namespace darkeye
{

AddActressDialog::AddActressDialog(QSqlDatabase publicDatabase, QWidget *parent)
    : QDialog(parent), m_publicDatabase(std::move(publicDatabase))
{
    setObjectName(QStringLiteral("AddActressDialog"));
    setWindowTitle(QStringLiteral("添加新女优"));
    setWindowIcon(IconProvider::builtIn(QStringLiteral("venus"), QSize(24, 24),
                                        QColor(QStringLiteral("#F44D92"))));
    resize(300, 150);

    auto *chineseLabel = new DesignLabel(QStringLiteral("女优中文名："), this);
    m_chineseName = new DesignLineEdit(this);
    m_chineseName->setObjectName(QStringLiteral("ActressChineseNameInput"));
    auto *japaneseLabel = new DesignLabel(QStringLiteral("女优日文名："), this);
    m_japaneseName = new DesignLineEdit(this);
    m_japaneseName->setObjectName(QStringLiteral("ActressJapaneseNameInput"));

    auto *search = new DesignButton(QStringLiteral("日文名搜索"), this);
    search->setObjectName(QStringLiteral("ActressJapaneseSearchButton"));
    auto *commit = new DesignButton(QStringLiteral("添加"), this);
    commit->setObjectName(QStringLiteral("ActressCommitButton"));
    commit->setVariant(QStringLiteral("primary"));

    auto *layout = new QGridLayout(this);
    layout->addWidget(chineseLabel, 0, 0);
    layout->addWidget(m_chineseName, 0, 1);
    layout->addWidget(japaneseLabel, 1, 0);
    layout->addWidget(m_japaneseName, 1, 1);
    layout->addWidget(search, 2, 0);
    layout->addWidget(commit, 2, 1);

    connect(search, &QPushButton::clicked, this, &AddActressDialog::searchJapaneseName);
    connect(commit, &QPushButton::clicked, this, &AddActressDialog::submit);
}

void AddActressDialog::submit()
{
    const QString chineseName = m_chineseName->text().trimmed();
    const QString japaneseName = m_japaneseName->text().trimmed();
    if (japaneseName.isEmpty())
    {
        QMessageBox::information(
            this, QStringLiteral("提示"),
            QStringLiteral("日文名禁止为空，爬虫会根据日文名爬取信息"));
        return;
    }

    QString errorMessage;
    PersonRepository repository(m_publicDatabase);
    const std::optional<qint64> actressId =
        repository.create(PersonKind::Actress, chineseName, japaneseName, &errorMessage);
    if (!actressId.has_value())
    {
        QMessageBox::warning(this, QStringLiteral("添加新女优失败"),
                             errorMessage.isEmpty() ? QStringLiteral("重复女优") : errorMessage);
        accept();
        return;
    }

    QMessageBox::information(
        this, QStringLiteral("添加新女优成功"),
        QStringLiteral("中文名: %1\n日文名: %2").arg(chineseName, japaneseName));
    emit personAdded(*actressId);
    accept();
}

void AddActressDialog::searchJapaneseName()
{
    QUrl url(QStringLiteral("https://www.minnano-av.com/search_result.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("search_scope"), QStringLiteral("actress"));
    query.addQueryItem(QStringLiteral("search_word"), m_japaneseName->text());
    query.addQueryItem(QStringLiteral("search"), QStringLiteral(" Go"));
    url.setQuery(query);
    QDesktopServices::openUrl(url);
}

} // namespace darkeye
