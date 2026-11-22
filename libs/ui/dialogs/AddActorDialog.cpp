#include "ui/dialogs/AddActorDialog.h"

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

AddActorDialog::AddActorDialog(QSqlDatabase publicDatabase, QWidget *parent)
    : QDialog(parent), m_publicDatabase(std::move(publicDatabase))
{
    setObjectName(QStringLiteral("AddActorDialog"));
    setWindowTitle(QStringLiteral("添加新男优"));
    setWindowIcon(IconProvider::builtIn(QStringLiteral("mars"), QSize(24, 24),
                                        QColor(QStringLiteral("#4DD8F4"))));
    resize(300, 150);

    auto *chineseLabel = new DesignLabel(QStringLiteral("男优中文名："), this);
    m_chineseName = new DesignLineEdit(this);
    m_chineseName->setObjectName(QStringLiteral("ActorChineseNameInput"));
    auto *japaneseLabel = new DesignLabel(QStringLiteral("男优日文名："), this);
    m_japaneseName = new DesignLineEdit(this);
    m_japaneseName->setObjectName(QStringLiteral("ActorJapaneseNameInput"));

    auto *search = new DesignButton(QStringLiteral("日文名搜索"), this);
    search->setObjectName(QStringLiteral("ActorJapaneseSearchButton"));
    auto *commit = new DesignButton(QStringLiteral("添加"), this);
    commit->setObjectName(QStringLiteral("ActorCommitButton"));
    commit->setVariant(QStringLiteral("primary"));

    auto *layout = new QGridLayout(this);
    layout->addWidget(chineseLabel, 0, 0);
    layout->addWidget(m_chineseName, 0, 1);
    layout->addWidget(japaneseLabel, 1, 0);
    layout->addWidget(m_japaneseName, 1, 1);
    layout->addWidget(search, 2, 0);
    layout->addWidget(commit, 2, 1);

    connect(search, &QPushButton::clicked, this, &AddActorDialog::searchJapaneseName);
    connect(commit, &QPushButton::clicked, this, &AddActorDialog::submit);
}

void AddActorDialog::submit()
{
    const QString chineseName = m_chineseName->text();
    const QString japaneseName = m_japaneseName->text();
    if (japaneseName.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("日文名禁止为空"));
        return;
    }

    QString errorMessage;
    PersonRepository repository(m_publicDatabase);
    const std::optional<qint64> actorId =
        repository.create(PersonKind::Actor, chineseName, japaneseName, &errorMessage);
    if (!actorId.has_value())
    {
        QMessageBox::warning(this, QStringLiteral("添加新男优失败"),
                             errorMessage.isEmpty() ? QStringLiteral("重复男优") : errorMessage);
        accept();
        return;
    }

    QMessageBox::information(
        this, QStringLiteral("添加新男优成功"),
        QStringLiteral("中文名: %1\n日文名: %2").arg(chineseName, japaneseName));
    emit personAdded(*actorId);
    accept();
}

void AddActorDialog::searchJapaneseName()
{
    QUrl url(QStringLiteral("https://avdanyuwiki.com/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("s"), m_japaneseName->text());
    url.setQuery(query);
    QDesktopServices::openUrl(url);
}

} // namespace darkeye
