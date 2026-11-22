#include "ui/dialogs/AddMakeLoveDialog.h"

#include "database/repositories/PrivateRepository.h"
#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignInput.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/RatingSelector.h"
#include "darkeye_ui/components/TokenViews.h"
#include "darkeye_ui/theme/IconProvider.h"

#include <QDateTime>
#include <QGridLayout>
#include <QMessageBox>
#include <QTime>
#include <QTimeZone>

namespace darkeye
{

AddMakeLoveDialog::AddMakeLoveDialog(QSqlDatabase privateDatabase, QWidget *parent)
    : QDialog(parent), m_privateDatabase(std::move(privateDatabase))
{
    setObjectName(QStringLiteral("AddMakeLoveDialog"));
    setWindowTitle(QStringLiteral("添加做爱记录"));
    setWindowIcon(IconProvider::builtIn(QStringLiteral("square_pen")));
    resize(300, 300);

    auto *ratingLabel = new DesignLabel(QStringLiteral("评分"), this);
    m_rating = new RatingSelector(this);
    m_rating->setObjectName(QStringLiteral("ratingInput"));

    auto *commentLabel = new DesignLabel(QStringLiteral("事后评价"), this);
    m_comment = new DesignTextEdit(this);
    m_comment->setObjectName(QStringLiteral("commentInput"));

    auto *timeLabel = new DesignLabel(QStringLiteral("时间"), this);
    auto *dateTime = new TokenDateTimeEdit(this);
    m_dateTime = dateTime;
    m_dateTime->setObjectName(QStringLiteral("dateTimeInput"));
    m_dateTime->setDisplayFormat(QStringLiteral("yy-MM-dd HH:mm"));
    m_dateTime->setDateTime(QDateTime::currentDateTime());
    m_dateTime->setCalendarPopup(true);
    m_dateTime->setMinimumTime(QTime(0, 0));
    m_dateTime->setMaximumTime(QTime(23, 59));
    m_dateTime->setTimeZone(QTimeZone::systemTimeZone());

    auto *commitButton = new DesignButton(QStringLiteral("提交记录"), this);
    commitButton->setObjectName(QStringLiteral("commitButton"));
    connect(commitButton, &QPushButton::clicked, this, &AddMakeLoveDialog::commit);

    auto *layout = new QGridLayout(this);
    layout->addWidget(ratingLabel, 0, 0, Qt::AlignRight);
    layout->addWidget(m_rating, 0, 1);
    layout->addWidget(timeLabel, 1, 0, Qt::AlignRight);
    layout->addWidget(m_dateTime, 1, 1);
    layout->addWidget(commentLabel, 2, 0, Qt::AlignRight);
    layout->addWidget(m_comment, 2, 1);
    layout->addWidget(commitButton, 3, 1);
}

void AddMakeLoveDialog::commit()
{
    const int rating = m_rating->rating();
    if (rating == 0)
    {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请打分"));
        return;
    }

    QString errorMessage;
    PrivateRepository repository(m_privateDatabase);
    if (!repository.addLoveMakingRecord(
            m_dateTime->dateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")), rating,
            m_comment->toPlainText(), &errorMessage))
    {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("提交失败\n%1").arg(errorMessage));
        return;
    }

    QMessageBox::information(this, QStringLiteral("提示"),
                             QStringLiteral("成功提交一次做爱记录"));
    emit recordAdded();
    accept();
}

} // namespace darkeye
