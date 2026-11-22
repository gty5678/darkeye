#include "database/SchemaManager.h"
#include "database/SqliteConnection.h"
#include "darkeye_ui/theme/ThemeService.h"
#include "ui/pages/management/PersonalRecordManagementWidget.h"
#include "ui/components/ModelSearchBar.h"

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlTableModel>
#include <QTableView>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QtTest>

class PersonalRecordManagementWidgetTest final : public QObject
{
    Q_OBJECT

private slots:
    void editsSearchesAndSavesPrivateRecords();
    void searchNavigatesWithoutFiltering();
};

void PersonalRecordManagementWidgetTest::editsSearchesAndSavesPrivateRecords()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(temporaryDirectory.filePath(QStringLiteral("private.db")), false,
                             &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 connection, darkeye::DatabaseKind::Private, &errorMessage),
             qPrintable(errorMessage));

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themes(*application);
    darkeye::PersonalRecordManagementWidget widget(connection.database(), themes);

    auto *tableChoice = widget.findChild<QComboBox *>(QStringLiteral("DesignComboBox"));
    auto *search = widget.findChild<QLineEdit *>(QStringLiteral("DesignInput"));
    auto *table = widget.findChild<QTableView *>(QStringLiteral("personalRecordTable"));
    auto *model = widget.findChild<QSqlTableModel *>();
    QVERIFY(tableChoice != nullptr);
    QVERIFY(search != nullptr);
    QVERIFY(table != nullptr);
    QVERIFY(model != nullptr);
    QCOMPARE(tableChoice->count(), 3);
    QCOMPARE(tableChoice->itemText(0), QStringLiteral("love_making"));
    QCOMPARE(tableChoice->itemText(1), QStringLiteral("masturbation"));
    QCOMPARE(tableChoice->itemText(2), QStringLiteral("sexual_arousal"));
    QCOMPARE(model->tableName(), QStringLiteral("love_making"));

    const int row = model->rowCount();
    QVERIFY(model->insertRow(row));
    QVERIFY(model->setData(model->index(row, model->record().indexOf(QStringLiteral("event_time"))),
                            QStringLiteral("2026-10-01 12:00")));
    QVERIFY(model->setData(model->index(row, model->record().indexOf(QStringLiteral("rating"))), 4));
    QPushButton *saveButton = nullptr;
    for (QPushButton *button : widget.findChildren<QPushButton *>())
        if (button->accessibleName() == QStringLiteral("保存修改"))
            saveButton = button;
    QVERIFY(saveButton != nullptr);
    saveButton->click();

    QSqlQuery query(connection.database());
    QVERIFY(query.exec(QStringLiteral("SELECT event_time, rating FROM love_making")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("2026-10-01 12:00"));
    QCOMPARE(query.value(1).toInt(), 4);

    const QString primary = QStringLiteral("#a13a59");
    QVERIFY(themes.setTheme(darkeye::ThemeId::Light, primary));
    table->selectRow(0);
    table->ensurePolished();
    QCOMPARE(table->palette().color(QPalette::Active, QPalette::Highlight), QColor(primary));
    QCOMPARE(table->palette().color(QPalette::Inactive, QPalette::Highlight), QColor(primary));

    search->setText(QStringLiteral("不存在"));
    QCOMPARE(table->model()->rowCount(), 1);
    auto *result = widget.findChild<QLabel *>(QStringLiteral("DesignLabel"));
    const auto labels = widget.findChildren<QLabel *>();
    for (auto *label : labels)
        if (label->text() == QStringLiteral("无匹配结果")) result = label;
    QVERIFY(result != nullptr);
    QCOMPARE(result->text(), QStringLiteral("无匹配结果"));
    search->clear();
    QTRY_COMPARE(table->model()->rowCount(), 1);
}

void PersonalRecordManagementWidgetTest::searchNavigatesWithoutFiltering()
{
    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    QStandardItemModel model(3, 1);
    model.setData(model.index(0, 0), QStringLiteral("Match A"));
    model.setData(model.index(1, 0), QStringLiteral("other"));
    model.setData(model.index(2, 0), QStringLiteral("Match B"));
    QTableView table;
    table.setModel(&model);
    darkeye::ModelSearchBar bar(&table, themes);
    auto *input = bar.input();
    auto *result = bar.findChild<QLabel *>();
    QCOMPARE(input->objectName(), QStringLiteral("DesignInput"));
    QCOMPARE(input->width(), 200);
    input->setText(QStringLiteral("match"));
    QCOMPARE(result->text(), QStringLiteral("结果 1/2"));
    QCOMPARE(table.selectionModel()->selectedRows().first().row(), 0);
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(result->text(), QStringLiteral("结果 2/2"));
    QCOMPARE(table.selectionModel()->selectedRows().first().row(), 2);
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(result->text(), QStringLiteral("结果 1/2"));
    QTest::keyClick(input, Qt::Key_Return, Qt::ShiftModifier);
    QCOMPARE(result->text(), QStringLiteral("结果 2/2"));
    QCOMPARE(model.rowCount(), 3);
    QVERIFY(!table.isRowHidden(1));
    model.removeRow(2);
    QCOMPARE(result->text(), QStringLiteral("结果 1/1"));
    input->clear();
    QCOMPARE(result->text(), QStringLiteral("无搜索结果"));
    for (auto *button : bar.findChildren<QPushButton *>())
        QVERIFY(!button->isEnabled());
}

QTEST_MAIN(PersonalRecordManagementWidgetTest)
#include "PersonalRecordManagementWidgetTest.moc"
