#include "settings/Paths.h"
#include "darkeye_ui/theme/ThemeService.h"
#include "darkeye_ui/base/LazyWidget.h"
#include "database/SchemaManager.h"
#include "database/SqliteConnection.h"
#include "database/repositories/PersonRepository.h"
#include "database/repositories/ReferenceRepository.h"
#include "database/repositories/WorkRepository.h"
#include "MainWindow.h"
#include "ui/pages/DashboardPage.h"
#include "ui/pages/HomePage.h"
#include "ui/components/ShopSceneWidget.h"
#include "ui/pages/ManagementPage.h"
#include "ui/pages/PersonDetailPage.h"
#include "ui/pages/ForceDirectPage.h"
#include "ui/pages/PersonPage.h"
#include "ui/pages/StatisticsPage.h"
#include "ui/pages/PlotTabPage.h"
#include "ui/pages/SettingsPage.h"
#include "ui/pages/ShelfPage.h"
#include "ui/pages/WorkPage.h"
#include "ui/components/IdCheckList.h"
#include "ui/components/WorkTagSelector.h"
#include "ui/components/PersonTransferSelector.h"
#include "ui/components/JsonTransferBar.h"
#include "ui/components/ModelSearchBar.h"
#include "darkeye_ui/components/Sidebar.h"
#include "ui/pages/management/MakerManagementWidget.h"
#include "ui/pages/management/LabelManagementWidget.h"
#include "darkeye_ui/components/ModernScrollMenu.h"
#include "darkeye_ui/components/LinkCard.h"
#include "ui/components/PathManagement.h"
#include "ui/pages/management/TagManagementWidget.h"
#include "darkeye_ui/components/TokenViews.h"
#include "ui/pages/management/WorkBatchStateWidget.h"
#include "ui/pages/management/AddWorkTabPage3.h"
#include "ui/pages/management/WorkMaintenanceWidget.h"
#include "ui/pages/management/SummaryQueryWidget.h"

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QFontDatabase>
#include <QLayout>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QKeySequenceEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QImage>
#include <QVector3D>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QSqlQuery>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <algorithm>

namespace
{
QPushButton *findSettingsButton(QObject *root, const QString &text)
{
    if (!root)
        return nullptr;
    for (auto *button : root->findChildren<QPushButton *>())
        if (button->text() == text && button->objectName() == QStringLiteral("DesignButton"))
            return button;
    return nullptr;
}

QComboBox *findThemeSelector(QObject *root)
{
    for (auto *combo : root->findChildren<QComboBox *>())
        if (combo->accessibleName() == QStringLiteral("主题"))
            return combo;
    return nullptr;
}

template <typename T>
T *findWorkControl(QObject *root, const QString &controlId)
{
    if (root == nullptr) return nullptr;
    for (T *child : root->findChildren<T *>())
        if (child->property("workControlId").toString() == controlId)
            return child;
    return nullptr;
}
}

class MainWindowSmokeTest final : public QObject
{
    Q_OBJECT

private slots:
    void resourceUsageAppearsInWindowTitle();
    void referenceManagementMatchesPython_data();
    void referenceManagementMatchesPython();
    void makerManagementAddsAndSavesDraftRows();
    void makerManagementSearchMatchesPythonBehavior();
    void histogramShowsPercentileLines();
    void homeShowsThreeRandomRows();
    void exposesAllPrimaryNavigationPages();
    void backgroundChangesRefreshOpenPages();
    void summarySearchKeyboardNavigation_data();
    void summarySearchKeyboardNavigation();
};

void MainWindowSmokeTest::resourceUsageAppearsInWindowTitle()
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    darkeye::MainWindow window(themes);
    const QRegularExpression metrics(QStringLiteral(
        "CPU ([0-9]+\\.[0-9]+)% \\| 公共内存 ([0-9]+\\.[0-9]+) MiB \\| 私有内存 ([0-9]+\\.[0-9]+) MiB"));
    QTRY_VERIFY_WITH_TIMEOUT(metrics.match(window.windowTitle()).hasMatch(), 3000);
    auto sample = metrics.match(window.windowTitle());
    QVERIFY(sample.captured(2).toDouble() + sample.captured(3).toDouble() > 0.0);

    // Accumulate CPU time between timer samples to catch unit conversion and
    // sampling bugs, rather than accepting a permanently hard-coded zero.
    QElapsedTimer work;
    work.start();
    while (work.elapsed() < 250) {}
    QTRY_VERIFY_WITH_TIMEOUT(
        metrics.match(window.windowTitle()).captured(1).toDouble() > 0.0, 3000);
    sample = metrics.match(window.windowTitle());
    QVERIFY(sample.captured(1).toDouble() <= 100.0);
#else
    QSKIP("Process resource metrics are implemented for Windows and macOS");
#endif
}

void MainWindowSmokeTest::referenceManagementMatchesPython_data()
{
    QTest::addColumn<bool>("series");
    QTest::newRow("label") << false;
    QTest::newRow("series") << true;
}

void MainWindowSmokeTest::referenceManagementMatchesPython()
{
    QFETCH(bool, series);
    QTemporaryDir directory;
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY(connection.open(directory.filePath(QStringLiteral("public.db")), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        connection, darkeye::DatabaseKind::Public, &error));
    const auto kind = series ? darkeye::ReferenceKind::Series : darkeye::ReferenceKind::Label;
    QSqlQuery clear(connection.database());
    QVERIFY(clear.exec(series ? QStringLiteral("DELETE FROM series") : QStringLiteral("DELETE FROM label")));
    darkeye::ReferenceRepository repository(connection.database());
    QVERIFY(repository.create({kind, 0, QStringLiteral("Alpha一"), {}, {}, QStringLiteral("详情"),
                               series ? QStringLiteral("相关") : QString()}, &error).has_value());
    QVERIFY(repository.create(kind, QStringLiteral("Alpha二"), &error).has_value());
    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    darkeye::LabelManagementWidget widget(kind, connection.database(), themes);
    auto *table = widget.findChild<QTableWidget *>();
    QLineEdit *search = nullptr;
    for (auto *edit : widget.findChildren<QLineEdit *>())
        if (edit->property("referenceControlId").toString() == QStringLiteral("search")) search = edit;
    auto *add = findSettingsButton(&widget, QStringLiteral("新增行"));
    auto *remove = findSettingsButton(&widget, QStringLiteral("删除行"));
    auto *save = findSettingsButton(&widget, QStringLiteral("保存修改"));
    auto *revert = findSettingsButton(&widget, QStringLiteral("撤销修改"));
    QVERIFY(table && search && add && remove && save && revert);
    QCOMPARE(table->columnCount(), series ? 6 : 5);
    QVERIFY(table->isColumnHidden(series ? 5 : 4));
    QVERIFY(table->editTriggers().testFlag(QAbstractItemView::DoubleClicked));
    QCOMPARE(table->columnWidth(0), 180);
    QCOMPARE(table->columnWidth(2), 220);
    QCOMPARE(search->width(), 200);
    QCOMPARE(widget.findChildren<QPlainTextEdit *>().size(), 0);
    const QString previewDirectory = qEnvironmentVariable("DARKEYE_REFERENCE_PREVIEW_DIR");
    if (!previewDirectory.isEmpty())
    {
        widget.resize(1200, 700);
        widget.show();
        QCoreApplication::processEvents();
        QVERIFY(widget.grab().save(QDir(previewDirectory).filePath(
            series ? QStringLiteral("series.png") : QStringLiteral("label.png"))));
    }
    QCOMPARE(search->objectName(), QStringLiteral("DesignInput"));
    QList<QLineEdit *> formEdits;
    for (auto *edit : widget.findChildren<QLineEdit *>(Qt::FindDirectChildrenOnly))
        if (edit != search) formEdits.append(edit);
    QCOMPARE(formEdits.size(), 3);
    table->setCurrentCell(0, 0);
    formEdits.at(2)->setText(QStringLiteral("表单别名"));
    QCOMPARE(table->item(0, 2)->text(), QStringLiteral("表单别名"));
    search->setText(QStringLiteral("alpha"));
    QCOMPARE(table->currentRow(), 0);
    QTest::keyClick(search, Qt::Key_Return);
    QCOMPARE(table->currentRow(), 1);
    QTest::keyClick(search, Qt::Key_Return, Qt::ShiftModifier);
    QCOMPARE(table->currentRow(), 0);
    for (int row = 0; row < table->rowCount(); ++row) QVERIFY(!table->isRowHidden(row));
    search->clear();
    table->item(0, 3)->setText(QStringLiteral("新详情"));
    if (series) table->item(0, 4)->setText(QStringLiteral("新相关系列"));
    table->item(0, 0)->setText(QStringLiteral("Alpha修改"));
    table->setCurrentCell(1, 0);
    table->item(1, 2)->setText(QStringLiteral("修改别名"));
    add->click();
    QCOMPARE(table->rowCount(), 3);
    table->item(2, 0)->setText(QStringLiteral("草稿"));
    QCOMPARE(repository.list(kind).size(), 2);
    save->click();
    const auto records = repository.list(kind);
    QCOMPARE(records.size(), 3);
    auto modified = std::find_if(records.cbegin(), records.cend(), [](const auto &r) {
        return r.chineseName == QStringLiteral("Alpha修改");
    });
    QVERIFY(modified != records.cend());
    QCOMPARE(modified->detail, QStringLiteral("新详情"));
    if (series) QCOMPARE(modified->extra, QStringLiteral("新相关系列"));
    table->selectRow(0);
    remove->click();
    QCOMPARE(table->rowCount(), 2);
    QCOMPARE(repository.list(kind).size(), 3);
    revert->click();
    QCOMPARE(table->rowCount(), 3);
    table->selectRow(0);
    remove->click();
    save->click();
    QCOMPARE(repository.list(kind).size(), 2);
    // A bad draft must roll back earlier updates in the same save operation.
    table->item(0, 0)->setText(QStringLiteral("不应提交"));
    add->click();
    save->click();
    QCOMPARE(repository.list(kind).size(), 2);
    for (const auto &record : repository.list(kind))
        QVERIFY(record.chineseName != QStringLiteral("不应提交"));
    revert->click();
    QCOMPARE(table->rowCount(), 2);
}

void MainWindowSmokeTest::makerManagementAddsAndSavesDraftRows()
{
    QTemporaryDir directory;
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY(connection.open(directory.filePath(QStringLiteral("public.db")), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        connection, darkeye::DatabaseKind::Public, &error));
    QSqlQuery clear(connection.database());
    QVERIFY(clear.exec(QStringLiteral("DELETE FROM prefix_maker_relation")));
    QVERIFY(clear.exec(QStringLiteral("DELETE FROM maker")));
    darkeye::ReferenceRepository repository(connection.database());
    const auto makerId = repository.create(darkeye::ReferenceKind::Maker, QStringLiteral("原片商"), &error);
    QVERIFY(makerId.has_value());
    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    darkeye::MakerManagementWidget widget(connection.database(), themes);
    QTableWidget *prefixes = nullptr;
    QTableWidget *makers = nullptr;
    for (auto *table : widget.findChildren<QTableWidget *>())
        if (table->columnCount() == 3) prefixes = table;
        else if (table->columnCount() == 4) makers = table;
    QVERIFY(prefixes && makers);
    const auto button = [&](const QString &tooltip) -> QPushButton * {
        for (auto *candidate : widget.findChildren<QPushButton *>())
            if (candidate->toolTip() == tooltip) return candidate;
        return nullptr;
    };
    auto *add = button(QStringLiteral("新增行"));
    auto *save = button(QStringLiteral("保存修改"));
    auto *remove = button(QStringLiteral("删除行"));
    auto *revert = button(QStringLiteral("撤销修改"));
    QVERIFY(add && save && remove && revert);
    auto edits = widget.findChildren<QLineEdit *>();
    edits.removeIf([](QLineEdit *edit) {
        return edit->property("makerControlId").toString() == QStringLiteral("MakerManagementSearch");
    });
    QCOMPARE(edits.size(), 4);
    auto *makerChoice = widget.findChild<QComboBox *>();
    QVERIFY(makerChoice);
    QCOMPARE(prefixes->rowCount(), 0);
    QCOMPARE(makers->rowCount(), 1);
    add->click();
    QCOMPARE(prefixes->rowCount(), 1);
    QCOMPARE(prefixes->currentRow(), 0);
    QCOMPARE(prefixes->item(0, 0)->text(), QStringLiteral("番号前缀"));
    QCOMPARE(prefixes->item(0, 1)->data(Qt::UserRole).toLongLong(), 1);
    QVERIFY(prefixes->isColumnHidden(2));
    QVERIFY(makers->isColumnHidden(3));
    QVERIFY(prefixes->editTriggers().testFlag(QAbstractItemView::DoubleClicked));
    edits[0]->setText(QStringLiteral("AAA"));
    makerChoice->setCurrentIndex(makerChoice->findData(*makerId));
    QCOMPARE(prefixes->item(0, 0)->text(), QStringLiteral("AAA"));
    add->click();
    QCOMPARE(prefixes->rowCount(), 2);
    edits[0]->setText(QStringLiteral("BBB"));
    makerChoice->setCurrentIndex(0);
    // An invalid second draft must roll back the first insertion and keep both drafts.
    save->click();
    QCOMPARE(repository.listMakerPrefixes().size(), 0);
    QCOMPARE(prefixes->rowCount(), 2);
    makerChoice->setCurrentIndex(makerChoice->findData(*makerId));
    prefixes->setCurrentCell(0, 0);
    QCOMPARE(edits[0]->text(), QStringLiteral("AAA"));
    makers->setCurrentCell(0, 0);
    add->click();
    QCOMPARE(makers->rowCount(), 2);
    makers->item(1, 0)->setText(QStringLiteral("新片商"));
    QCOMPARE(edits[1]->text(), QStringLiteral("新片商"));
    add->click();
    QCOMPARE(makers->rowCount(), 3);
    remove->click();
    QCOMPARE(makers->rowCount(), 2);
    QCOMPARE(repository.list(darkeye::ReferenceKind::Maker).size(), 1);
    save->click();
    QCOMPARE(repository.listMakerPrefixes().size(), 0);
    QCOMPARE(prefixes->item(0, 0)->text(), QStringLiteral("AAA"));
    QCOMPARE(repository.list(darkeye::ReferenceKind::Maker).size(), 2);
    prefixes->setCurrentCell(1, 0);
    save->click();
    QCOMPARE(repository.listMakerPrefixes().size(), 2);
    makers->setCurrentCell(0, 0);
    QCOMPARE(prefixes->rowCount(), 2);
    QCOMPARE(makers->rowCount(), 2);
    add->click();
    QCOMPARE(makers->rowCount(), 3);
    revert->click();
    QCOMPARE(makers->rowCount(), 2);
    QCOMPARE(prefixes->rowCount(), 2);
    // Deletion is staged and can be reverted in the active table.
    makers->setCurrentCell(0, 0);
    remove->click();
    QCOMPARE(makers->rowCount(), 1);
    QCOMPARE(repository.list(darkeye::ReferenceKind::Maker).size(), 2);
    revert->click();
    QCOMPARE(makers->rowCount(), 2);
    // Reverting one table must leave the other table's draft intact.
    prefixes->setCurrentCell(0, 0);
    add->click();
    QCOMPARE(prefixes->rowCount(), 3);
    makers->setCurrentCell(0, 0);
    revert->click();
    QCOMPARE(prefixes->rowCount(), 3);
    prefixes->setCurrentCell(0, 0);
    revert->click();
    QCOMPARE(prefixes->rowCount(), 2);
    prefixes->setCurrentCell(0, 0);
    remove->click();
    QCOMPARE(repository.listMakerPrefixes().size(), 2);
    save->click();
    QCOMPARE(repository.listMakerPrefixes().size(), 1);
}

void MainWindowSmokeTest::makerManagementSearchMatchesPythonBehavior()
{
    QTemporaryDir directory;
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY(connection.open(directory.filePath(QStringLiteral("public.db")), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        connection, darkeye::DatabaseKind::Public, &error));
    QSqlQuery clear(connection.database());
    QVERIFY(clear.exec(QStringLiteral("DELETE FROM prefix_maker_relation")));
    QVERIFY(clear.exec(QStringLiteral("DELETE FROM maker")));
    darkeye::ReferenceRepository repository(connection.database());
    const auto makerId = repository.create(darkeye::ReferenceKind::Maker, QStringLiteral("Alpha片商"), &error);
    QVERIFY(makerId.has_value());
    QVERIFY(repository.createMakerPrefix(QStringLiteral("ALPHA-1"), *makerId, &error).has_value());
    QVERIFY(repository.createMakerPrefix(QStringLiteral("ALPHA-2"), *makerId, &error).has_value());
    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    darkeye::MakerManagementWidget widget(connection.database(), themes);
    QLineEdit *search = nullptr;
    QLabel *result = nullptr;
    for (auto *edit : widget.findChildren<QLineEdit *>())
        if (edit->property("makerControlId").toString() == QStringLiteral("MakerManagementSearch"))
            search = edit;
    for (auto *label : widget.findChildren<QLabel *>())
        if (label->property("makerControlId").toString() == QStringLiteral("MakerManagementSearchResult"))
            result = label;
    QPushButton *previous = nullptr;
    QPushButton *next = nullptr;
    for (auto *button : widget.findChildren<QPushButton *>())
    {
        if (button->accessibleName() == QStringLiteral("上一个搜索结果")) previous = button;
        if (button->accessibleName() == QStringLiteral("下一个搜索结果")) next = button;
    }
    QTableWidget *prefixes = nullptr;
    QTableWidget *makers = nullptr;
    for (auto *table : widget.findChildren<QTableWidget *>())
        if (table->columnCount() == 3) prefixes = table;
        else makers = table;
    QVERIFY(search && result && previous && next && prefixes && makers);
    QVERIFY(search->isClearButtonEnabled());
    QVERIFY(!previous->isEnabled() && !next->isEnabled());
    // Search all columns, ignoring case and counting each matching row only once.
    search->setText(QStringLiteral(" aLpHa "));
    QCOMPARE(result->text(), QStringLiteral("结果 1/2"));
    QCOMPARE(prefixes->currentRow(), 0);
    QCOMPARE(prefixes->rowCount(), 2);
    QTest::keyClick(search, Qt::Key_Return);
    QCOMPARE(prefixes->currentRow(), 1);
    QCOMPARE(result->text(), QStringLiteral("结果 2/2"));
    QTest::keyClick(search, Qt::Key_Return);
    QCOMPARE(prefixes->currentRow(), 0);
    QTest::keyClick(search, Qt::Key_Enter, Qt::ShiftModifier);
    QCOMPARE(prefixes->currentRow(), 1);
    previous->click();
    QCOMPARE(prefixes->currentRow(), 0);
    next->click();
    QCOMPARE(prefixes->currentRow(), 1);
    // Maker names displayed in the prefix relation are searchable too.
    search->setText(QStringLiteral("片商"));
    QCOMPARE(result->text(), QStringLiteral("结果 1/2"));
    makers->setCurrentCell(0, 0);
    QCOMPARE(result->text(), QStringLiteral("结果 1/1"));
    next->click();
    QCOMPARE(makers->currentRow(), 0);
    makers->item(0, 0)->setText(QStringLiteral("改名"));
    makers->item(0, 1)->setText(QStringLiteral("改名"));
    QTRY_COMPARE(result->text(), QStringLiteral("无匹配结果"));
    QVERIFY(!next->isEnabled() && !previous->isEnabled());
    search->clear();
    QCOMPARE(result->text(), QStringLiteral("无搜索结果"));
    // Search does not submit edits and includes unsaved new rows.
    for (auto *button : widget.findChildren<QPushButton *>())
        if (button->toolTip() == QStringLiteral("新增行")) button->click();
    makers->item(1, 0)->setText(QStringLiteral("草稿片商"));
    search->setText(QStringLiteral("草稿"));
    QCOMPARE(result->text(), QStringLiteral("结果 1/1"));
    QCOMPARE(makers->currentRow(), 1);
    QCOMPARE(repository.list(darkeye::ReferenceKind::Maker).size(), 1);
    for (auto *button : widget.findChildren<QPushButton *>())
        if (button->toolTip() == QStringLiteral("撤销修改")) button->click();
    QTRY_COMPARE(result->text(), QStringLiteral("无匹配结果"));
    prefixes->setCurrentCell(1, 0);
    search->setText(QStringLiteral("ALPHA"));
    QCOMPARE(result->text(), QStringLiteral("结果 1/2"));
    for (auto *button : widget.findChildren<QPushButton *>())
        if (button->toolTip() == QStringLiteral("删除行")) button->click();
    QTRY_COMPARE(result->text(), QStringLiteral("找到 1 个结果"));
    next->click();
    QCOMPARE(result->text(), QStringLiteral("结果 1/1"));
    QCOMPARE(prefixes->currentRow(), 0);
}

void MainWindowSmokeTest::histogramShowsPercentileLines()
{
    QTemporaryDir directory;
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY(connection.open(directory.filePath(QStringLiteral("public.db")), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        connection, darkeye::DatabaseKind::Public, &error));
    QSqlQuery query(connection.database());
    QVERIFY(query.exec(QStringLiteral("INSERT INTO actress(height) VALUES (150), (160), (170)")));
    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    darkeye::PlotTabPage page(connection.database(), {}, themes);
    page.resize(1200, 700);
    page.initialize();
    for (auto *radio : page.findChildren<QRadioButton *>())
        if (radio->text() == QStringLiteral("公共数据库")) radio->click();
    for (auto *button : page.findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("女优身高分布直方图")) button->click();
    page.show();
    QTest::qWait(50);
    QWidget *canvas = page.captureContent();
    QVERIFY(canvas != nullptr);
    const QImage image = canvas->grab().toImage();
    const QString snapshot = qEnvironmentVariable("DARKEYE_PERCENTILE_SNAPSHOT");
    if (!snapshot.isEmpty()) QVERIFY(image.save(snapshot));
    const qreal dpr = image.devicePixelRatio();
    const QRect plot = canvas->rect().adjusted(90, 66, -34, -84);
    const auto hasRedLine = [&](qreal percentile) {
        const int x = qRound((plot.left() + (percentile - 149.5) / 21 * plot.width()) * dpr);
        int redRows = 0;
        for (int y = qRound((plot.top() + 30) * dpr); y < qRound(plot.bottom() * dpr); ++y) {
            for (int dx = -1; dx <= 1; ++dx) {
                const QColor color = image.pixelColor(x + dx, y);
                // Antialiasing blends the one-pixel line with the bar below it.
                if (color.red() > color.green() + 40 && color.red() > color.blue() + 20) {
                    ++redRows;
                    break;
                }
            }
        }
        return redRows > plot.height() * dpr / 4;
    };
    // Python's cumulative-weight interpolation for equal weights:
    // 5% is the first sample; 95% is 85% of the way from 160 to 170.
    QVERIFY(hasRedLine(150));
    QVERIFY(hasRedLine(168.5));
    QVERIFY(!hasRedLine(170));
}

void MainWindowSmokeTest::homeShowsThreeRandomRows()
{
    QTemporaryDir directory;
    darkeye::SqliteConnection connection;
    QString error;
    QVERIFY(connection.open(directory.filePath(QStringLiteral("public.db")), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        connection, darkeye::DatabaseKind::Public, &error));
    darkeye::WorkRepository repository(connection.database());
    for (int i = 0; i < 24; ++i)
    {
        darkeye::Work work;
        work.serialNumber = QStringLiteral("HOME-%1").arg(i);
        work.imageUrl = QStringLiteral("%1.png").arg(i);
        QImage cover(3200, 2200, QImage::Format_RGB32);
        cover.fill(QColor::fromHsv((i * 30) % 360, 150, 200));
        QVERIFY(cover.save(directory.filePath(work.imageUrl)));
        QVERIFY(repository.insertComplete(work, {}, {}, {}, &error).has_value());
    }
    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    darkeye::HomePage page(connection.database(), directory.path(), themes);
    page.initialize();
    auto *view = page.findChild<darkeye::ShopSceneWidget *>();
    QVERIFY(view);
    for (const auto *widget : page.findChildren<QWidget *>())
        QVERIFY(!widget->inherits("QQuickWidget"));
    auto *canvas = view->canvas();
    QVERIFY(canvas->property("modelError").toString().isEmpty());
    QCOMPARE(view->coverPaths().size(), 18);
    QCOMPARE(canvas->camera().target, QVector3D(0, 0.865f, 0));
    QCOMPARE(canvas->sampleCount(), 2);
    const auto initial = view->coverPaths();
    const auto scene = canvas->scene();
    const auto size = scene->size();
    const auto firstMesh = scene->renderItems().back().mesh;
    auto *refresh = page.findChild<QPushButton *>(QStringLiteral("HomeRefreshButton"));
    QVERIFY(refresh);
    bool changed = false;
    for (int i=0; i<5 && !changed; ++i) {
        refresh->click();
        changed = view->coverPaths()!=initial;
    }
    QVERIFY(changed);
    QCOMPARE(scene->size(), size);
    QCOMPARE(scene->renderItems().back().mesh, firstMesh);
    page.resize(1100, 650);
    page.show();
    QTRY_VERIFY(canvas->statistics().submittedFrames>0);
    QTRY_VERIFY([&] {
        int ready = 0;
        for (const auto &item : scene->renderItems())
            if (item.material && item.material->name==QStringLiteral("pic")
                && item.material->baseColorTexture
                && view->coverPaths().contains(item.material->baseColorTexture->source)
                && item.material->baseColorTexture->image.width()>=256
                && item.material->baseColorTexture->image.width()<=1024
                && item.material->baseColorTexture->maxAnisotropy==8) ++ready;
        return ready==18;
    }());
    // Small home covers should stay inexpensive, including their mip chains.
    quint64 textureBytes = 0;
    for (const auto &item : scene->renderItems()) {
        if (!item.material || item.material->name!=QStringLiteral("pic")) continue;
        QSize mip = item.material->baseColorTexture->image.size();
        while (true) {
            textureBytes += quint64(mip.width())*mip.height()*4;
            if (mip==QSize(1,1)) break;
            mip = QSize(std::max(1,mip.width()/2),std::max(1,mip.height()/2));
        }
    }
    canvas->update();
    QTRY_VERIFY(canvas->statistics().sourceTextureBytes>=textureBytes);
    QVERIFY(textureBytes<64ULL*1024*1024);
    QSignalSpy selected(&page, &darkeye::HomePage::detailRequested);
    const auto center = canvas->camera().project({-0.394006f,1.115f,-0.043f},canvas->size());
    QVERIFY(center);
    QMouseEvent hover(QEvent::MouseMove, *center, canvas->mapToGlobal(center->toPoint()),
                      Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &hover);
    QCOMPARE(canvas->cursor().shape(), Qt::PointingHandCursor);
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, center->toPoint());
    QCOMPARE(selected.size(), 1);
    QVERIFY(selected.first().first().toLongLong()>0);
    const auto before = canvas->camera();
    QTest::mousePress(canvas, Qt::RightButton, Qt::NoModifier, QPoint(400,300));
    QTest::mouseMove(canvas,QPoint(440,320));
    QTest::mouseRelease(canvas, Qt::RightButton, Qt::NoModifier, QPoint(440,320));
    QVERIFY(canvas->camera().yaw!=before.yaw);
    QCOMPARE(canvas->camera().position(),before.position());
    const auto menuAppears = [&](QContextMenuEvent::Reason reason) {
        bool appeared = false;
        QTimer::singleShot(0, canvas, [&] {
            if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                appeared = true;
                menu->close();
            }
        });
        QContextMenuEvent context(reason, QPoint(440,320),
                                  canvas->mapToGlobal(QPoint(440,320)));
        QApplication::sendEvent(canvas, &context);
        QApplication::processEvents();
        return appeared;
    };
    QVERIFY(!menuAppears(QContextMenuEvent::Mouse));
    // A new stationary click must clear the previous drag suppression.
    QTest::mouseClick(canvas, Qt::RightButton, Qt::NoModifier, QPoint(440,320));
    QVERIFY(menuAppears(QContextMenuEvent::Mouse));
    // Moving back to the press position still counts as a camera drag.
    QTest::mousePress(canvas, Qt::RightButton, Qt::NoModifier, QPoint(440,320));
    for (const QPoint position : {QPoint(441,320), QPoint(440,320)}) {
        QMouseEvent move(QEvent::MouseMove, position, canvas->mapToGlobal(position),
                         Qt::NoButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &move);
    }
    QTest::mouseRelease(canvas, Qt::RightButton, Qt::NoModifier, QPoint(440,320));
    QVERIFY(!menuAppears(QContextMenuEvent::Mouse));
    QVERIFY(menuAppears(QContextMenuEvent::Keyboard));
    const auto frame = canvas->grabFramebuffer();
    QVERIFY(!frame.isNull());
    QVERIFY(canvas->lastError().isEmpty());
    const QString snapshot = qEnvironmentVariable("DARKEYE_HOME_SNAPSHOT");
    if (!snapshot.isEmpty()) QVERIFY(page.grab().save(snapshot));
}

void MainWindowSmokeTest::backgroundChangesRefreshOpenPages()
{
    QTemporaryDir directory;
    darkeye::settings::Paths paths(directory.path(), QDir(directory.path()).filePath(QStringLiteral("data")));
    QVERIFY(paths.ensureRuntimeDirectories());
    darkeye::SqliteConnection publicConnection, privateConnection;
    QString error;
    QVERIFY(publicConnection.open(paths.publicDatabase(), false, &error));
    QVERIFY(privateConnection.open(paths.privateDatabase(), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        publicConnection, darkeye::DatabaseKind::Public, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        privateConnection, darkeye::DatabaseKind::Private, &error));
    darkeye::PersonRepository people(publicConnection.database());
    const auto actressId = people.create(darkeye::PersonKind::Actress,
                                         QStringLiteral("Before"), QStringLiteral("Name"), &error);
    QVERIFY(actressId.has_value());
    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    darkeye::MainWindow window(themes, publicConnection.database(), privateConnection.database(), paths);
    auto *sidebar = window.findChild<darkeye::Sidebar *>();
    QVERIFY(sidebar);
    sidebar->itemClicked(QStringLiteral("actress"));
    auto *list = window.findChild<darkeye::PersonPage *>(QStringLiteral("ActressPage"));
    QVERIFY(list);
    list->detailRequested(darkeye::PersonKind::Actress, *actressId);
    auto *detail = window.findChild<darkeye::PersonDetailPage *>();
    QVERIFY(detail);
    QSqlQuery update(publicConnection.database());
    QVERIFY(update.exec(QStringLiteral("UPDATE actress_name SET cn='After'")));
    auto *sync = window.findChild<darkeye::ActressSyncService *>();
    QVERIFY(sync);
    sync->actressesChanged();
    bool displaysUpdatedName = false;
    for (QLabel *label : detail->findChildren<QLabel *>())
        displaysUpdatedName |= label->text().contains(QStringLiteral("After"));
    QVERIFY(displaysUpdatedName);
    auto *persistence = window.findChild<darkeye::CrawlerPersistenceService *>();
    QVERIFY(persistence);
    auto *works = window.findChild<darkeye::WorkPage *>();
    QVERIFY(works);
    works->initialize();
    auto *tags = works->findChild<darkeye::WorkTagSelector *>();
    QVERIFY(tags);
    tags->setSelectedIds({});
    persistence->persist(QStringLiteral("REFRESH-001"), {}, {QStringLiteral("jp_title")});
    bool displaysUpdatedCount = false;
    for (QLabel *label : works->findChildren<QLabel *>())
        displaysUpdatedCount |= label->text() == QStringLiteral("过滤总数:1");
    QVERIFY(displaysUpdatedCount);
}

void MainWindowSmokeTest::summarySearchKeyboardNavigation_data()
{
    QTest::addColumn<int>("key");
    QTest::newRow("return") << int(Qt::Key_Return);
    QTest::newRow("enter") << int(Qt::Key_Enter);
}

void MainWindowSmokeTest::summarySearchKeyboardNavigation()
{
    QFETCH(int, key);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    darkeye::SqliteConnection connection;
    QString errorMessage;
    QVERIFY2(connection.open(directory.filePath(QStringLiteral("public.db")), false, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 connection, darkeye::DatabaseKind::Public, &errorMessage), qPrintable(errorMessage));
    darkeye::WorkRepository repository(connection.database());
    for (int i = 1; i <= 3; ++i)
    {
        darkeye::Work work;
        work.serialNumber = QStringLiteral("SEARCH-00%1").arg(i);
        QVERIFY2(repository.insertComplete(work, {}, {}, {}, &errorMessage).has_value(),
                 qPrintable(errorMessage));
    }

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themes(*application);
    darkeye::SummaryQueryWidget widget(connection.database(), themes);
    auto *search = widget.findChild<QLineEdit *>();
    auto *table = widget.findChild<QTableView *>();
    QVERIFY(search != nullptr);
    QVERIFY(table != nullptr);
    QCOMPARE(table->selectionMode(), QAbstractItemView::SingleSelection);
    QPushButton *previous = nullptr;
    QPushButton *next = nullptr;
    for (auto *button : widget.findChildren<QPushButton *>())
    {
        if (button->accessibleName() == QStringLiteral("上一个搜索结果")) previous = button;
        if (button->accessibleName() == QStringLiteral("下一个搜索结果")) next = button;
    }
    QVERIFY(previous != nullptr);
    QVERIFY(next != nullptr);
    search->setText(QStringLiteral("SEARCH"));
    QCOMPARE(table->currentIndex().row(), 0);
    QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
    QTest::keyClick(search, Qt::Key(key));
    QCOMPARE(table->currentIndex().row(), 1);
    QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
    QTest::keyClick(search, Qt::Key(key), Qt::ShiftModifier);
    QCOMPARE(table->currentIndex().row(), 0);
    QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
    QTest::keyClick(search, Qt::Key(key), Qt::ShiftModifier);
    QCOMPARE(table->currentIndex().row(), 2);
    QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
    QTest::keyClick(search, Qt::Key(key));
    QCOMPARE(table->currentIndex().row(), 0);
    QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
    previous->click();
    QCOMPARE(table->currentIndex().row(), 2);
    QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
    next->click();
    QCOMPARE(table->currentIndex().row(), 0);
    QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
    QTest::keyClick(search, Qt::Key(key), Qt::ControlModifier);
    QCOMPARE(table->currentIndex().row(), 0);
    search->setText(QStringLiteral("missing"));
    QVERIFY(!previous->isEnabled());
    QVERIFY(!next->isEnabled());
    QTest::keyClick(search, Qt::Key(key));
    QTest::keyClick(search, Qt::Key(key), Qt::ShiftModifier);
    search->clear();
    QVERIFY(!previous->isEnabled());
    QVERIFY(!next->isEnabled());
}

void MainWindowSmokeTest::exposesAllPrimaryNavigationPages()
{
    // The offscreen plugin needs explicit fonts for readable preview images.
    if (!qEnvironmentVariableIsEmpty("DARKEYE_SETTINGS_PREVIEW_DIR"))
    {
        const QDir fonts(QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts")));
        for (const QString &file : {QStringLiteral("msyh.ttc"), QStringLiteral("segoeui.ttf")})
            if (QFileInfo::exists(fonts.filePath(file)))
                QVERIFY(QFontDatabase::addApplicationFont(fonts.filePath(file)) >= 0);
    }
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    const QString applicationDirectory = temporaryDirectory.path();
    const darkeye::settings::Paths paths(applicationDirectory, QDir(applicationDirectory).filePath(QStringLiteral("data")));
    QVERIFY(paths.ensureRuntimeDirectories());

    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    darkeye::ThemeService themeService(*application);
    QVERIFY(themeService.setTheme(darkeye::ThemeId::Light));
    darkeye::SqliteConnection publicConnection;
    QString errorMessage;
    QVERIFY2(publicConnection.open(paths.publicDatabase(), false, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 publicConnection, darkeye::DatabaseKind::Public, &errorMessage),
             qPrintable(errorMessage));
    darkeye::SqliteConnection privateConnection;
    QVERIFY2(privateConnection.open(paths.privateDatabase(), false, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
                 privateConnection, darkeye::DatabaseKind::Private, &errorMessage),
             qPrintable(errorMessage));
    darkeye::WorkRepository repository(publicConnection.database());
    darkeye::Work work;
    work.serialNumber = QStringLiteral("ROUTE-001");
    work.chineseTitle = QStringLiteral("详情路由测试");
    const auto workId = repository.insertComplete(work, {}, {}, {}, &errorMessage);
    QVERIFY2(workId.has_value(), qPrintable(errorMessage));
    darkeye::PersonRepository people(publicConnection.database());
    const auto actressId = people.create(darkeye::PersonKind::Actress, QStringLiteral("路由女演员"),
                                         QStringLiteral("ルート女優"), &errorMessage);
    QVERIFY2(actressId.has_value(), qPrintable(errorMessage));

    darkeye::MainWindow window(themeService, publicConnection.database(),
                               privateConnection.database(), paths);
    window.show();
    QCoreApplication::processEvents();
    auto *sidebar = window.findChild<darkeye::Sidebar *>(QStringLiteral("DesignSidebar"));
    const auto *pages = window.findChild<QStackedWidget *>(QStringLiteral("mainPages"));

    QVERIFY(sidebar != nullptr);
    QVERIFY(pages != nullptr);
    // Python's factory router only adds routes to the stack when first visited.
    QCOMPARE(pages->count(), 1);
    QCOMPARE(sidebar->selectedId(), QStringLiteral("work"));
    QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("WorkPage"));
    QVERIFY(window.findChild<darkeye::WorkPage *>(QStringLiteral("WorkPage")) !=
            nullptr);
    QCOMPARE(window.findChild<QAction *>(QStringLiteral("add_masturbation_record"))->shortcut(),
             QKeySequence(QStringLiteral("M")));
    QCOMPARE(window.findChild<QAction *>(QStringLiteral("add_quick_work"))->shortcut(),
             QKeySequence(QStringLiteral("W")));
    QCOMPARE(window.findChild<QAction *>(QStringLiteral("add_makelove_record"))->shortcut(),
             QKeySequence(QStringLiteral("L")));
    QCOMPARE(window.findChild<QAction *>(QStringLiteral("add_sexual_rousal_record"))->shortcut(),
             QKeySequence(QStringLiteral("A")));
    QCOMPARE(window.findChild<QAction *>(QStringLiteral("search"))->shortcut(),
             QKeySequence(QStringLiteral("Ctrl+F")));
    QCOMPARE(window.findChild<QAction *>(QStringLiteral("capture"))->shortcut(),
             QKeySequence(QStringLiteral("C")));
    QCOMPARE(window.findChild<QAction *>(QStringLiteral("allcapture"))->shortcut(),
             QKeySequence(QStringLiteral("Shift+C")));
    QVERIFY(findThemeSelector(&window) == nullptr);

    emit sidebar->itemClicked(QStringLiteral("chart"));
    QVERIFY(qobject_cast<darkeye::StatisticsPage *>(pages->currentWidget()) != nullptr);
    auto *statisticsPage = window.findChild<darkeye::StatisticsPage *>(
        QStringLiteral("StatisticsPage"));
    QVERIFY(statisticsPage != nullptr);
    QVERIFY(statisticsPage->isInitialized());

    emit sidebar->itemClicked(QStringLiteral("setting"));
    auto *themeSelector = findThemeSelector(&window);
    QVERIFY(themeSelector != nullptr);
    QCOMPARE(themeSelector->count(), 7);
    QCOMPARE(themeSelector->itemText(0), QStringLiteral("亮色主题"));
    QCOMPARE(themeSelector->itemText(1), QStringLiteral("暗色主题"));
    QCOMPARE(themeSelector->itemText(3), QStringLiteral("黄色"));
    QCOMPARE(themeSelector->itemText(4), QStringLiteral("绿色"));
    auto *settingsMenu =
        window.findChild<darkeye::ModernScrollMenu *>(QStringLiteral("DesignModernScrollMenu"));
    QVERIFY(settingsMenu);
    QCOMPARE(settingsMenu->sectionCount(), 8);
    auto *aboutPage = window.findChild<darkeye::AboutSettingsPage *>();
    QVERIFY(aboutPage);
    aboutPage->initialize();
    QCOMPARE(aboutPage->findChildren<darkeye::TokenLinkCard *>().size(), 8);
    auto *checkUpdate = findSettingsButton(aboutPage, QStringLiteral("检查更新"));
    auto *feedback = findSettingsButton(aboutPage, QStringLiteral("意见反馈"));
    QVERIFY(checkUpdate);
    QVERIFY(feedback);
    QVERIFY(checkUpdate->isEnabled());
    QVERIFY(feedback->isEnabled());
    QVERIFY(window.findChild<QWidget *>(QStringLiteral("PrimaryColorRow")));
    auto *shortcutPage = window.findChild<darkeye::ShortcutSettingsPage *>();
    QVERIFY(shortcutPage);
    shortcutPage->initialize();
    const auto loadedShortcutRows =
        window.findChildren<QWidget *>(QRegularExpression(QStringLiteral("^ShortcutSettingRow_")));
    QCOMPARE(loadedShortcutRows.size(), 8);
    auto *helpEditor = window.findChild<QWidget *>(QStringLiteral("ShortcutSettingRow_open_help"))
                           ->findChild<QKeySequenceEdit *>(QStringLiteral("DesignKeySequenceEdit"));
    auto *helpReset =
        findSettingsButton(window.findChild<QWidget *>(QStringLiteral("ShortcutSettingRow_open_help")),
                           QStringLiteral("恢复"));
    QVERIFY(helpEditor);
    QVERIFY(helpReset);
    QCOMPARE(helpEditor->keySequence(), QKeySequence(QStringLiteral("H")));
    helpEditor->setKeySequence(QKeySequence(QStringLiteral("F1")));
    QVERIFY(QMetaObject::invokeMethod(helpEditor, "editingFinished", Qt::DirectConnection));
    QCOMPARE(window.findChild<QAction *>(QStringLiteral("open_help"))->shortcut(),
             QKeySequence(QStringLiteral("F1")));
    helpReset->click();
    QCOMPARE(helpEditor->keySequence(), QKeySequence(QStringLiteral("H")));
    QCOMPARE(window.findChild<QAction *>(QStringLiteral("open_help"))->shortcut(),
             QKeySequence(QStringLiteral("H")));
    auto *videoPage = window.findChild<darkeye::VideoSettingsPage *>();
    QVERIFY(videoPage);
    videoPage->initialize();
    auto *player = videoPage->findChild<QLineEdit *>(QStringLiteral("DesignInput"));
    auto *videoPaths =
        window.findChild<darkeye::MultiplePathManagement *>(QStringLiteral("VideoPathManagement"));
    QVERIFY(player);
    QVERIFY(videoPaths);
    QVERIFY(findSettingsButton(videoPage, QStringLiteral("扫描本地视频提取番号并录入数据库")));
    QVERIFY(findSettingsButton(videoPage, QStringLiteral("同步作品本地视频路径")));
    QVERIFY(findSettingsButton(videoPaths, QStringLiteral("添加地址")));
    QVERIFY(findSettingsButton(videoPaths, QStringLiteral("删除地址")));
    QCOMPARE(player->objectName(), QStringLiteral("DesignInput"));
    QCOMPARE(videoPaths->table()->objectName(), QStringLiteral("DesignTableWidget"));
    QCOMPARE(helpEditor->objectName(), QStringLiteral("DesignKeySequenceEdit"));
    QCOMPARE(themeSelector->objectName(), QStringLiteral("DesignComboBox"));
    auto *translationPage = window.findChild<darkeye::TranslationSettingsPage *>();
    QVERIFY(translationPage);
    translationPage->initialize();
    const auto translationSpins = translationPage->findChildren<QSpinBox *>();
    QCOMPARE(translationSpins.size(), 9);
    for (auto *spin : translationSpins)
        QCOMPARE(spin->objectName(), QStringLiteral("DesignSpinBox"));
    const auto translationTexts = translationPage->findChildren<QPlainTextEdit *>();
    QCOMPARE(translationTexts.size(), 4);
    for (auto *text : translationTexts)
        QCOMPARE(text->objectName(), QStringLiteral("DesignPlainTextEdit"));
    QCOMPARE(translationTexts.at(0)->height(), 96);
    QCOMPARE(translationTexts.at(1)->height(), 96);
    auto *databaseSettings = window.findChild<darkeye::DatabaseSettingsPage *>();
    QVERIFY(databaseSettings);
    databaseSettings->initialize();
    const auto webDavInputs = databaseSettings->findChildren<QLineEdit *>(Qt::FindDirectChildrenOnly);
    QCOMPARE(webDavInputs.size(), 3);
    for (auto *input : webDavInputs)
        QCOMPARE(input->objectName(), QStringLiteral("DesignInput"));
    for (auto *spin : databaseSettings->findChildren<QSpinBox *>())
        QCOMPARE(spin->objectName(), QStringLiteral("DesignSpinBox"));

    const QString previewDirectory = qEnvironmentVariable("DARKEYE_SETTINGS_PREVIEW_DIR");
    if (!previewDirectory.isEmpty())
    {
        const QList<QPair<QString, QWidget *>> previews = {
            {QStringLiteral("video"), videoPage}, {QStringLiteral("translation"), translationPage},
            {QStringLiteral("database"), databaseSettings}, {QStringLiteral("shortcuts"), shortcutPage}};
        for (const auto theme : {darkeye::ThemeId::Light, darkeye::ThemeId::Dark})
        {
            QVERIFY(themeService.setTheme(theme));
            for (const auto &preview : previews)
            {
                preview.second->resize(800, preview.second->sizeHint().height());
                preview.second->layout()->activate();
                const QString suffix = theme == darkeye::ThemeId::Light ? QStringLiteral("light")
                                                                        : QStringLiteral("dark");
                QVERIFY(preview.second->grab().save(QDir(previewDirectory).filePath(
                    preview.first + u'-' + suffix + QStringLiteral(".png"))));
            }
        }
        QVERIFY(themeService.setTheme(darkeye::ThemeId::Light));
    }
    emit sidebar->itemClicked(QStringLiteral("work"));
    QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("WorkPage"));
    auto *workPage = window.findChild<darkeye::WorkPage *>(QStringLiteral("WorkPage"));
    auto *shelfPage = window.findChild<darkeye::ShelfPage *>(QStringLiteral("ShelfPage"));
    auto *actressPage = window.findChild<darkeye::PersonPage *>(QStringLiteral("ActressPage"));
    auto *actressDetail =
        window.findChild<darkeye::PersonDetailPage *>(QStringLiteral("ActressDetailPage"));
    QVERIFY(workPage != nullptr);
    // Like Python's factory router, routes that have not been visited must not
    // construct their pages during MainWindow startup.
    QVERIFY(shelfPage == nullptr);
    QVERIFY(actressPage == nullptr);
    QVERIFY(actressDetail == nullptr);
    window.preloadShelfPage();
    shelfPage = window.findChild<darkeye::ShelfPage *>(QStringLiteral("ShelfPage"));
    QVERIFY(shelfPage != nullptr);
    QVERIFY(shelfPage->isInitialized());
    // Preloading must not navigate away from the page the user is viewing.
    QCOMPARE(pages->currentWidget(), workPage);
    emit workPage->detailRequested(*workId);
    QCOMPARE(pages->currentWidget(), shelfPage);
    QCOMPARE(sidebar->selectedId(), QStringLiteral("shelf"));
    emit sidebar->backwardClicked();
    QCOMPARE(pages->currentWidget(), workPage);
    emit sidebar->forwardClicked();
    QCOMPARE(pages->currentWidget(), shelfPage);
    emit sidebar->itemClicked(QStringLiteral("actress"));
    actressPage = window.findChild<darkeye::PersonPage *>(QStringLiteral("ActressPage"));
    QVERIFY(actressPage != nullptr);
    QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("ActressPage"));
    emit sidebar->itemClicked(QStringLiteral("actor"));
    QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("ActorPage"));
    emit sidebar->itemClicked(QStringLiteral("database"));
    QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("ManagementPage"));
    auto *managementTabs =
        window.findChild<QTabWidget *>(QStringLiteral("ManagementTabs"));
    QVERIFY(managementTabs != nullptr);
    const auto loadManagementTab = [managementTabs](int index)
    {
        managementTabs->setCurrentIndex(index);
        QCoreApplication::processEvents();
        if (auto *lazy = dynamic_cast<darkeye::LazyWidget *>(managementTabs->widget(index)))
            lazy->initialize();
    };
    // Python creates lazy tab shells up front; selecting each tab performs its
    // first real load. Exercise the same lifecycle before inspecting content.
    loadManagementTab(2);
    loadManagementTab(3);
    loadManagementTab(4);
    QVERIFY(window.findChild<darkeye::MakerManagementWidget *>() != nullptr);
    QCOMPARE(window.findChildren<darkeye::LabelManagementWidget *>().size(), 2);
    loadManagementTab(8);
    loadManagementTab(9);
    loadManagementTab(5);
    const auto stateWidgets = window.findChildren<darkeye::WorkBatchStateWidget *>();
    QCOMPARE(stateWidgets.size(), 2);
    for (auto *stateWidget : stateWidgets)
    {
        auto *searchBar = stateWidget->findChild<darkeye::ModelSearchBar *>();
        QVERIFY(searchBar != nullptr);
        auto *table = stateWidget->findChild<QTableView *>();
        const int rows = table->model()->rowCount();
        searchBar->input()->setText(QStringLiteral("不存在的作品"));
        QCOMPARE(table->model()->rowCount(), rows);
        for (int row = 0; row < rows; ++row)
            QVERIFY(!table->isRowHidden(row));
        searchBar->input()->clear();
    }
    auto *maintenanceWidget = window.findChild<darkeye::WorkMaintenanceWidget *>();
    QVERIFY(maintenanceWidget != nullptr);
    auto *addWorkEditor =
        window.findChild<darkeye::ManagementPage *>(QStringLiteral("ManagementPage"))
            ->findChild<darkeye::AddWorkTabPage3 *>();
    QVERIFY(addWorkEditor != nullptr);
    addWorkEditor->beginCreate();
    auto *addSerial = findWorkControl<QLineEdit>(
        addWorkEditor, QStringLiteral("WorkSerialInput"));
    auto *addTitle = findWorkControl<QPlainTextEdit>(
        addWorkEditor, QStringLiteral("WorkChineseTitleInput"));
    auto *addSave = addWorkEditor->findChild<QPushButton *>(QStringLiteral("WorkSaveButton"));
    QVERIFY(addSerial != nullptr);
    QVERIFY(addTitle != nullptr);
    QVERIFY(addSave != nullptr);
    auto *addActresses = addWorkEditor->findChild<darkeye::PersonTransferSelector *>(
        QStringLiteral("WorkActressSelector"));
    QVERIFY(addActresses != nullptr);
    addActresses->setSelectedIds({*actressId});
    addSerial->setText(QStringLiteral("UI-ADD-001"));
    addTitle->setPlainText(QStringLiteral("界面添加作品"));
    addSave->click();
    QTRY_VERIFY_WITH_TIMEOUT(repository.existsSerial(QStringLiteral("UI-ADD-001")), 500);
    const QList<darkeye::WorkStateRecord> activeWorks = repository.listByDeletedState(false);
    QCOMPARE(activeWorks.size(), 2);
    loadManagementTab(1);
    auto *tagWidget =
        window.findChild<darkeye::TagManagementWidget *>();
    QVERIFY(tagWidget != nullptr);
    emit sidebar->itemClicked(QStringLiteral("setting"));
    QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("SettingsPage"));
    themeSelector->setCurrentIndex(1);
    QCOMPARE(themeService.current(), darkeye::ThemeId::Dark);
    emit actressPage->detailRequested(darkeye::PersonKind::Actress, *actressId);
    actressDetail = window.findChild<darkeye::PersonDetailPage *>(
        QStringLiteral("ActressDetailPage"));
    QVERIFY(actressDetail != nullptr);
    QCOMPARE(pages->currentWidget(), actressDetail);
    QCOMPARE(actressDetail->currentPersonId(), *actressId);
    QCOMPARE(sidebar->selectedId(), QStringLiteral("actress"));
    auto *actressGraph = actressDetail->findChild<darkeye::ForceDirectPage *>(
        QStringLiteral("actressRelationGraph"));
    QVERIFY(actressGraph != nullptr);
    QVERIFY(!actressGraph->isHidden());
    emit actressGraph->actressRequested(*actressId);
    QCOMPARE(actressDetail->currentPersonId(), *actressId);
    QCOMPARE(pages->currentWidget(), actressDetail);
    emit actressDetail->editRequested(darkeye::PersonKind::Actress, *actressId);
    QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("ModifyActressPage"));
    QCOMPARE(sidebar->selectedId(), QStringLiteral("actress"));
    QCOMPARE(window.findChildren<QDialog *>().size(), 0);
    QVERIFY(window.windowTitle().startsWith(QStringLiteral("暗之眼 V")));
}

QTEST_MAIN(MainWindowSmokeTest)
#include "MainWindowSmokeTest.moc"
#include <QComboBox>
