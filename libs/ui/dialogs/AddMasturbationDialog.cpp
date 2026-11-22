#include "ui/dialogs/AddMasturbationDialog.h"

#include "database/repositories/PrivateRepository.h"
#include "database/repositories/WorkRepository.h"
#include "darkeye_ui/components/CompleterLineEdit.h"
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

AddMasturbationDialog::AddMasturbationDialog(QSqlDatabase publicDatabase,
                                               QSqlDatabase privateDatabase, QWidget *parent)
    : QDialog(parent), m_publicDatabase(std::move(publicDatabase)),
      m_privateDatabase(std::move(privateDatabase))
{
    setObjectName(QStringLiteral("AddMasturbationDialog"));
    setWindowTitle(QStringLiteral("添加自慰记录"));
    setWindowIcon(IconProvider::builtIn(QStringLiteral("square_pen")));
    resize(300, 300);

    WorkRepository workRepository(m_publicDatabase);
    const QStringList serialNumbers = workRepository.serialSuggestions();
    PrivateRepository privateRepository(m_privateDatabase);
    const QStringList tools = privateRepository.masturbationToolSuggestions();

    auto *serialLabel = new DesignLabel(QStringLiteral("番号"), this);
    auto *serialInput = new CompleterLineEdit([serialNumbers] { return serialNumbers; }, this);
    m_serialNumber = serialInput;
    m_serialNumber->setObjectName(QStringLiteral("serialNumberInput"));

    auto *ratingLabel = new DesignLabel(QStringLiteral("评分"), this);
    m_rating = new RatingSelector(this);
    m_rating->setObjectName(QStringLiteral("ratingInput"));

    auto *toolLabel = new DesignLabel(QStringLiteral("使用工具"), this);
    auto *toolInput = new CompleterLineEdit([tools] { return tools; }, this);
    m_tool = toolInput;
    m_tool->setObjectName(QStringLiteral("toolInput"));
    m_tool->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

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
    connect(commitButton, &QPushButton::clicked, this, &AddMasturbationDialog::commit);

    auto *layout = new QGridLayout(this);
    layout->addWidget(serialLabel, 0, 0, Qt::AlignRight);
    layout->addWidget(m_serialNumber, 0, 1);
    layout->addWidget(ratingLabel, 1, 0, Qt::AlignRight);
    layout->addWidget(m_rating, 1, 1);
    layout->addWidget(toolLabel, 2, 0, Qt::AlignRight);
    layout->addWidget(m_tool, 2, 1);
    layout->addWidget(timeLabel, 3, 0, Qt::AlignRight);
    layout->addWidget(m_dateTime, 3, 1);
    layout->addWidget(commentLabel, 4, 0, Qt::AlignRight);
    layout->addWidget(m_comment, 4, 1);
    layout->addWidget(commitButton, 5, 1);
}

void AddMasturbationDialog::commit()
{
    const QString serialNumber = m_serialNumber->text().trimmed();
    QString errorMessage;
    WorkRepository workRepository(m_publicDatabase);
    const std::optional<qint64> workId =
        serialNumber.isEmpty() ? std::optional<qint64>()
                               : workRepository.findIdBySerial(serialNumber, &errorMessage);
    if (!errorMessage.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("提示"), errorMessage);
        return;
    }
    if (!serialNumber.isEmpty() && !workId.has_value())
    {
        QMessageBox::question(this, QStringLiteral("提示"),
                              QStringLiteral("库内没有该番号，是否添加新作品？"));
        return;
    }
    if (m_rating->rating() == 0)
    {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请打分"));
        return;
    }
    if (m_tool->text().isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请输入一个自慰工具"));
        return;
    }

    PrivateRepository privateRepository(m_privateDatabase);
    if (!privateRepository.addMasturbationRecord(
            workId.value_or(0), serialNumber,
            m_dateTime->dateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")), m_tool->text(),
            m_rating->rating(), m_comment->toPlainText(), &errorMessage))
    {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("提交失败\n%1").arg(errorMessage));
        return;
    }

    QMessageBox::information(this, QStringLiteral("提示"),
                             QStringLiteral("成功提交一次自慰记录"));
    emit recordAdded();
    accept();
}

} // namespace darkeye
