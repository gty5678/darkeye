#include "database/SchemaManager.h"
#include "darkeye_ui/components/RatingSelector.h"
#include "ui/dialogs/AddActorDialog.h"
#include "ui/dialogs/AddActressDialog.h"
#include "ui/dialogs/AddMakeLoveDialog.h"
#include "ui/dialogs/AddMasturbationDialog.h"
#include "ui/dialogs/AddSexualArousalDialog.h"
#include "ui/dialogs/TermsDialog.h"

#include <QDate>
#include <QDateTimeEdit>
#include <QDir>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QtTest>

class PersonalRecordDialogTest final : public QObject
{
    Q_OBJECT

private slots:
    void mirrorsPythonDialogFieldsAndDefaults();
};

void PersonalRecordDialogTest::mirrorsPythonDialogFieldsAndDefaults()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection publicConnection;
    darkeye::SqliteConnection privateConnection;
    QString errorMessage;
    QVERIFY2(publicConnection.open(QDir(temporaryDirectory.path()).filePath("public.db"), false,
                                   &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(privateConnection.open(QDir(temporaryDirectory.path()).filePath("private.db"), false,
                                    &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 publicConnection, darkeye::DatabaseKind::Public, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 privateConnection, darkeye::DatabaseKind::Private, &errorMessage),
             qPrintable(errorMessage));

    darkeye::AddMakeLoveDialog loveMaking(privateConnection.database());
    QCOMPARE(loveMaking.windowTitle(), QStringLiteral("添加做爱记录"));
    QVERIFY(loveMaking.findChild<darkeye::RatingSelector *>(QStringLiteral("ratingInput")));
    auto *loveTime =
        loveMaking.findChild<QDateTimeEdit *>(QStringLiteral("dateTimeInput"));
    QVERIFY(loveTime);
    QCOMPARE(loveTime->displayFormat(), QStringLiteral("yy-MM-dd HH:mm"));
    QVERIFY(loveMaking.findChild<QTextEdit *>(QStringLiteral("commentInput")));

    darkeye::AddMasturbationDialog masturbation(publicConnection.database(),
                                                 privateConnection.database());
    QCOMPARE(masturbation.windowTitle(), QStringLiteral("添加自慰记录"));
    QVERIFY(masturbation.findChild<QLineEdit *>(QStringLiteral("serialNumberInput")));
    QVERIFY(masturbation.findChild<QLineEdit *>(QStringLiteral("toolInput")));
    QVERIFY(masturbation.findChild<darkeye::RatingSelector *>(QStringLiteral("ratingInput")));

    darkeye::AddSexualArousalDialog arousal(privateConnection.database());
    QCOMPARE(arousal.windowTitle(), QStringLiteral("添加无意识性器官唤醒记录"));
    auto *arousalTime = arousal.findChild<QDateTimeEdit *>(QStringLiteral("dateTimeInput"));
    QVERIFY(arousalTime);
    QCOMPARE(arousalTime->date(), QDate::currentDate());
    QCOMPARE(arousalTime->time().hour(), 6);
    QCOMPARE(arousalTime->time().minute(), 0);

    darkeye::TermsDialog terms;
    QCOMPARE(terms.windowTitle(), QStringLiteral("用户使用条款"));
    QCOMPARE(terms.size(), QSize(500, 400));
    QVERIFY(terms.findChild<QWidget *>(QStringLiteral("TermsText")));
    QVERIFY(terms.findChild<QWidget *>(QStringLiteral("TermsAgreeButton")));
    QVERIFY(terms.findChild<QWidget *>(QStringLiteral("TermsDisagreeButton")));

    darkeye::AddActorDialog actor(publicConnection.database());
    QCOMPARE(actor.windowTitle(), QStringLiteral("添加新男优"));
    QVERIFY(actor.findChild<QLineEdit *>(QStringLiteral("ActorChineseNameInput")));
    QVERIFY(actor.findChild<QLineEdit *>(QStringLiteral("ActorJapaneseNameInput")));
    QVERIFY(actor.findChild<QWidget *>(QStringLiteral("ActorJapaneseSearchButton")));

    darkeye::AddActressDialog actress(publicConnection.database());
    QCOMPARE(actress.windowTitle(), QStringLiteral("添加新女优"));
    QVERIFY(actress.findChild<QLineEdit *>(QStringLiteral("ActressChineseNameInput")));
    QVERIFY(actress.findChild<QLineEdit *>(QStringLiteral("ActressJapaneseNameInput")));
    QVERIFY(actress.findChild<QWidget *>(QStringLiteral("ActressJapaneseSearchButton")));
}

QTEST_MAIN(PersonalRecordDialogTest)
#include "PersonalRecordDialogTest.moc"
