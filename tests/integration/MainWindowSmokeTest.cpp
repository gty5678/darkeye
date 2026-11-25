#include "ui/components/ActressWorkTimeline.h"
#include "ui/components/WorkspaceSearch.h"
#include "ui/components/WorkMaintenanceActions.h"
#include "ui/layouts/myads/WorkspaceWidget.h"
#include "ui/components/DvdShelfView.h"
#include <QListWidget>
#include <QMessageBox>
#include <QTabBar>
#include <QToolButton>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "settings/Paths.h"
#include "settings/Settings.h"
#include "darkeye_ui/theme/ThemeService.h"
#include "darkeye_ui/base/LazyWidget.h"
#include "darkeye_ui/components/AnimatedIndicators.h"
#include "darkeye_ui/components/ColorPicker.h"
#include "darkeye_ui/components/OctImage.h"
#include "database/SchemaManager.h"
#include "database/SqliteConnection.h"
#include "database/repositories/PersonRepository.h"
#include "database/repositories/PrivateRepository.h"
#include "database/repositories/ReferenceRepository.h"
#include "database/repositories/WorkRepository.h"
#include "dvd_scene/DvdSceneWidget.h"
#include "MainWindow.h"
#include "ui/pages/HomePage.h"
#include "ui/components/ShopSceneWidget.h"
#include "ui/pages/PersonDetailPage.h"
#include "ui/pages/PersonEditorPage.h"
#include "ui/pages/ForceDirectPage.h"
#include "graph_view/ForceViewRhiWidget.h"
#include "ui/pages/PersonPage.h"
#include "ui/pages/PersonalDataPanels.h"
#include <QScrollArea>
#include <QScrollBar>
#include <QScopeGuard>
#include <QSplitter>
#include "ui/pages/StatisticsChartPage.h"
#include "ui/pages/SettingsPage.h"
#include "ui/pages/ShelfPage.h"
#include "ui/pages/WorkPage.h"
#include "ui/components/WorkFilter.h"
#include "ui/components/IdCheckList.h"
#include "ui/components/WorkTagSelector.h"
#include "ui/components/PersonTransferSelector.h"
#include "ui/components/ModelSearchBar.h"
#include "darkeye_ui/components/Sidebar.h"
#include "ui/pages/MakerManagementWidget.h"
#include "ui/pages/LabelManagementWidget.h"
#include "darkeye_ui/components/LinkCard.h"
#include "ui/components/PathManagement.h"
#include "ui/pages/TagManagementWidget.h"
#include "darkeye_ui/components/TokenViews.h"
#include "ui/pages/WorkBatchStateWidget.h"
#include "ui/pages/AddWorkTabPage3.h"
#include "ui/pages/SummaryQueryWidget.h"
#include "ui/pages/PersonalRecordManagementWidget.h"

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QActionGroup>
#include <QDialog>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QLayout>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPainter>
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
#include <QStyleOptionButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QWheelEvent>
#include <QInputMethodEvent>
#include <QWindow>
#include <QtTest>
#include <algorithm>
#include <cmath>

#ifdef Q_OS_MACOS
void postMacShortcut(QWidget &window, unsigned short keyCode, const char *characters,
                     Qt::KeyboardModifiers modifiers);
#endif

namespace
{
bool saveEmptyWorkspace(const darkeye::settings::Paths &paths)
{
    darkeye::myads::LayoutTree tree;
    tree.addPaneToRoot(QStringLiteral("pane_1"));
    return darkeye::myads::saveLayoutAtomic(
        QDir(paths.dataDirectory()).filePath(QStringLiteral("workspace-v2.json")), tree.toJson());
}

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
    void homeAdaptsRandomRowsToHeight();
    void homePreservesFiltersWhenWorkIsIncluded();
    void exposesAllPrimaryNavigationPages();
    void navigationBarOpensPages();
    void navigationBarSettingsRememberVisibility();
    void settingsDialogClosesWithoutTitleBar();
    void workFilterUsesSavedStartupOptions();
    void settingsThemeSwitchPreservesCustomPrimary();
    void workspaceSearchShortcutsOpenBothModes();
    void settingsCommandsOpenExistingOperations();
    void restoresBrowsingWorkspacePreset();
    void sidebarButtonsAndVisibilitySettings();
    void sidebarShortcutsAndAutoCollapseSetting();
#ifdef Q_OS_MACOS
    void nativeCommandShortcuts();
#endif
    void paneTabShortcutsCanBeCustomized();
    void paneShortcutsFollowLayoutOrder();
    void restoredTabsDeferInitialization();
    void workspaceSearchAndSessionLifecycle();
    void actressDetailOpenModes();
    void actressPartialCaptureSavesCard();
    void localRelationshipGraphFollowsActiveContent();
    void timelineFollowsActressAndFilteredWorks();
    void openingPagesLeavesFocusUnset();
    void statisticsPagesOpenIndependentlyAndScroll();
    void managementWorkFormScrollsInShortWindow();
    void backgroundChangesRefreshOpenPages();
    void workspaceChangesRefreshAllShelvesAndGraph();
    void summarySearchKeyboardNavigation_data();
    void summarySearchKeyboardNavigation();
    void captureOpenedUrl(const QUrl &url) { m_openedUrl = url; }

private:
    QUrl m_openedUrl;
};

void MainWindowSmokeTest::resourceUsageAppearsInWindowTitle()
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    darkeye::MainWindow window(themes);
    const auto titleText = [&window] { return window.windowTitle(); };
#ifdef Q_OS_MACOS
    const QRegularExpression metrics(QStringLiteral(
        "CPU +([0-9]+\\.[0-9]+)% \\| 内存占用 +([0-9]+\\.[0-9]+) MiB \\| 驻留内存 +([0-9]+\\.[0-9]+) MiB"));
#else
    const QRegularExpression metrics(QStringLiteral(
        "CPU ([0-9]+\\.[0-9]+)% \\| 公共内存 ([0-9]+\\.[0-9]+) MiB \\| 私有内存 ([0-9]+\\.[0-9]+) MiB"));
#endif
    QTRY_VERIFY_WITH_TIMEOUT(metrics.match(titleText()).hasMatch(), 3000);
    auto sample = metrics.match(titleText());
    QVERIFY(sample.captured(2).toDouble() + sample.captured(3).toDouble() > 0.0);

    // Accumulate CPU time between timer samples to catch unit conversion and
    // sampling bugs, rather than accepting a permanently hard-coded zero.
    QElapsedTimer work;
    work.start();
    while (work.elapsed() < 250) {}
    QTRY_VERIFY_WITH_TIMEOUT(
        metrics.match(titleText()).captured(1).toDouble() > 0.0, 3000);
    sample = metrics.match(titleText());
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
    widget.initialize();
    auto *table = widget.findChild<QTableWidget *>();
    QLineEdit *search = nullptr;
    for (auto *edit : widget.findChildren<QLineEdit *>())
        if (edit->property("referenceControlId").toString() == QStringLiteral("search")) search = edit;
    const auto iconButton = [&widget](const QString &label) -> QPushButton * {
        for (auto *button : widget.findChildren<QPushButton *>())
            if (button->accessibleName() == label && button->toolTip() == label &&
                button->text().isEmpty() && !button->icon().isNull())
                return button;
        return nullptr;
    };
    auto *add = iconButton(QStringLiteral("新增行"));
    auto *remove = iconButton(QStringLiteral("删除行"));
    auto *save = iconButton(QStringLiteral("保存修改"));
    auto *revert = iconButton(QStringLiteral("撤销修改"));
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
    for (auto *edit : search->parentWidget()->findChildren<QLineEdit *>(Qt::FindDirectChildrenOnly))
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
    widget.initialize();
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
    widget.initialize();
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
    darkeye::StatisticsChartPage page(connection.database(), {}, themes);
    page.resize(1200, 700);
    page.initialize();
    const auto findSelector = [&page](const QString &name) -> QComboBox * {
        for (auto *selector : page.findChildren<QComboBox *>())
            if (selector->accessibleName() == name) return selector;
        return nullptr;
    };
    auto *category = findSelector(QStringLiteral("分类"));
    auto *metric = findSelector(QStringLiteral("统计项"));
    auto *scope = findSelector(QStringLiteral("统计范围"));
    QVERIFY(category && metric && scope);
    category->setCurrentText(QStringLiteral("女优"));
    const int heightMetric = metric->findText(QStringLiteral("身高分布"));
    QVERIFY(heightMetric >= 0);
    metric->setCurrentIndex(heightMetric);
    scope->setCurrentText(QStringLiteral("公共数据库"));
    page.show();
    QTest::qWait(50);
    QWidget *canvas = page.captureContent();
    QVERIFY(canvas != nullptr);
    const QImage image = canvas->grab().toImage();
    const QString snapshot = qEnvironmentVariable("DARKEYE_PERCENTILE_SNAPSHOT");
    if (!snapshot.isEmpty()) QVERIFY(image.save(snapshot));
    const qreal dpr = image.devicePixelRatio();
    const QRect plot = canvas->rect().adjusted(88, 85, -48, -68);
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

void MainWindowSmokeTest::homeAdaptsRandomRowsToHeight()
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
    page.resize(1100, 250);
    page.show();
    QTRY_COMPARE(view->coverPaths().size(), 6);
    const auto firstRow = view->coverPaths();
    const auto scene = canvas->scene();
    for (const auto &[height, rows] : {std::pair{450, 2}, {650, 3}, {850, 4}, {250, 1}, {650, 3}}) {
        page.resize(1100, height);
        QTRY_COMPARE(view->coverPaths().size(), rows * 6);
        QCOMPARE(view->coverPaths().mid(0, 6), firstRow);
        int visibleCovers = 0;
        for (const auto &item : scene->renderItems()) {
            if (!item.material || item.material->name != QStringLiteral("pic")) continue;
            const auto center = canvas->camera().project(item.world.map(QVector3D()), canvas->size());
            QVERIFY(center);
            QVERIFY(QRectF(canvas->rect()).contains(*center));
            ++visibleCovers;
        }
        QCOMPARE(visibleCovers, rows * 6);
    }
    QCOMPARE(canvas->sampleCount(), 4);
    QTRY_COMPARE(canvas->statistics().samples, 4);
    const auto initial = view->coverPaths();
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
    const QPoint clickPosition(440,320);
    const auto sendContextMenu = [&](QContextMenuEvent::Reason reason) {
        QContextMenuEvent context(reason, clickPosition, canvas->mapToGlobal(clickPosition));
        QApplication::sendEvent(canvas, &context);
    };
    QVERIFY(!QApplication::activePopupWidget());
    // Simulate the platform context-menu event arriving at both possible times.
    QTest::mousePress(canvas, Qt::RightButton, Qt::NoModifier, clickPosition);
    sendContextMenu(QContextMenuEvent::Mouse);
    QVERIFY(!QApplication::activePopupWidget());
    QTest::mouseRelease(canvas, Qt::RightButton, Qt::NoModifier, clickPosition);
    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    QVERIFY(menu);
    sendContextMenu(QContextMenuEvent::Mouse);
    QCOMPARE(QApplication::activePopupWidget(), menu);
    auto *statisticsAction = canvas->statisticsAction();
    QCOMPARE(menu->actions(), QList<QAction *>{statisticsAction});
    QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                      menu->actionGeometry(statisticsAction).center());
    QVERIFY(canvas->statisticsVisible());
    canvas->setStatisticsVisible(false);
    // Moving back to the press position still counts as a camera drag.
    QTest::mousePress(canvas, Qt::RightButton, Qt::NoModifier, QPoint(440,320));
    for (const QPoint position : {QPoint(441,320), QPoint(440,320)}) {
        QMouseEvent move(QEvent::MouseMove, position, canvas->mapToGlobal(position),
                         Qt::NoButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &move);
    }
    QTest::mouseRelease(canvas, Qt::RightButton, Qt::NoModifier, QPoint(440,320));
    sendContextMenu(QContextMenuEvent::Mouse);
    QVERIFY(!QApplication::activePopupWidget());
    bool keyboardMenuAppeared = false;
    QTimer::singleShot(0, canvas, [&] {
        if (auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
            keyboardMenuAppeared = true;
            popup->close();
        }
    });
    sendContextMenu(QContextMenuEvent::Keyboard);
    QVERIFY(keyboardMenuAppeared);
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
    QVERIFY(saveEmptyWorkspace(paths));
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
    auto *search = window.findChild<darkeye::WorkspaceSearch *>();
    QVERIFY(search);
    search->openRequested(QStringLiteral("mutiwork"));
    search->openRequested(QStringLiteral("actress"));
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
    const auto displaysUpdatedName = [detail] {
        const auto labels = detail->findChildren<QLabel *>();
        return std::any_of(labels.cbegin(), labels.cend(), [](const QLabel *label) {
            return label->text().contains(QStringLiteral("After"));
        });
    };
    QTRY_VERIFY(displaysUpdatedName());
    auto *persistence = window.findChild<darkeye::CrawlerPersistenceService *>();
    QVERIFY(persistence);
    auto *works = window.findChild<darkeye::WorkPage *>();
    QVERIFY(works);
    works->initialize();
    auto *tags = window.findChild<darkeye::WorkFilter *>()->findChild<darkeye::WorkTagSelector *>();
    QVERIFY(tags);
    tags->setSelectedIds({});
    const QString tagName = QStringLiteral("New crawler tag");
    const QJsonObject payload{{QStringLiteral("tag_list"), QJsonArray{tagName}}};
    persistence->persist(QStringLiteral("REFRESH-001"), payload, {QStringLiteral("tag")});
    QTRY_COMPARE(works->filteredWorks().size(), 1);
    const auto tagOptions = darkeye::WorkRepository(publicConnection.database()).tagOptions();
    const auto newTag = std::find_if(tagOptions.cbegin(), tagOptions.cend(), [&tagName](const auto &tag) {
        return tag.name == tagName;
    });
    QVERIFY(newTag != tagOptions.cend());
    tags->setSelectedIds({newTag->id});
    QCOMPARE(tags->selectedIds(), QList<qint64>{newTag->id});

    auto *workspace = window.findChild<darkeye::myads::WorkspaceWidget *>();
    const QString detailRoute = QStringLiteral("actress_detail/%1").arg(*actressId);
    QVERIFY(workspace && workspace->findPaneByContentId(detailRoute));
    search->openRequested(QStringLiteral("modify_actress/%1").arg(*actressId));
    auto *editor = window.findChild<darkeye::PersonEditorPage *>();
    QVERIFY(editor);
    QVERIFY(people.deletePerson(darkeye::PersonKind::Actress, *actressId, &error));
    editor->personDeleted(darkeye::PersonKind::Actress, *actressId);
    QVERIFY(!workspace->findPaneByContentId(detailRoute));
}

void MainWindowSmokeTest::workspaceChangesRefreshAllShelvesAndGraph()
{
    QTemporaryDir directory;
    darkeye::settings::Paths paths(directory.path(), directory.filePath(QStringLiteral("data")));
    QVERIFY(paths.ensureRuntimeDirectories());
    QVERIFY(saveEmptyWorkspace(paths));
    darkeye::SqliteConnection publicDatabase, privateDatabase;
    QString error;
    QVERIFY(publicDatabase.open(paths.publicDatabase(), false, &error));
    QVERIFY(privateDatabase.open(paths.privateDatabase(), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        publicDatabase, darkeye::DatabaseKind::Public, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        privateDatabase, darkeye::DatabaseKind::Private, &error));

    darkeye::ReferenceRepository references(publicDatabase.database());
    const auto sourceTag = references.createTag(QStringLiteral("Source"), {}, {}, {}, &error);
    const auto targetTag = references.createTag(QStringLiteral("Target"), {}, {}, {}, &error);
    QVERIFY(sourceTag && targetTag);
    darkeye::WorkRepository works(publicDatabase.database());
    darkeye::Work first, second, untagged;
    first.serialNumber = QStringLiteral("REFRESH-101");
    second.serialNumber = QStringLiteral("REFRESH-102");
    untagged.serialNumber = QStringLiteral("REFRESH-103");
    const auto firstId = works.insertComplete(first, {}, {}, {*sourceTag}, &error);
    const auto secondId = works.insertComplete(second, {}, {}, {*targetTag}, &error);
    const auto untaggedId = works.insertComplete(untagged, {}, {}, {}, &error);
    QVERIFY(firstId && secondId && untaggedId);

    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    darkeye::MainWindow window(themes, publicDatabase.database(), privateDatabase.database(), paths);
    auto *search = window.findChild<darkeye::WorkspaceSearch *>();
    auto *workspace = window.findChild<darkeye::myads::WorkspaceWidget *>();
    auto *graph = window.findChild<darkeye::graph::GraphManager *>();
    QVERIFY(search && workspace && graph);
    QVERIFY(graph->initialize(&error));
    auto *pane = workspace->rootPane();
    search->openRequested(QStringLiteral("shelf"));
    auto *shelf = qobject_cast<darkeye::ShelfPage *>(pane->contentWidget(QStringLiteral("shelf")));
    const QString categoryRoute = QStringLiteral("tag/%1").arg(*sourceTag);
    search->openRequested(categoryRoute);
    auto *category = qobject_cast<darkeye::ShelfPage *>(pane->contentWidget(categoryRoute));
    QVERIFY(shelf && category);
    shelf->initialize();
    auto *shelfTags = window.findChild<darkeye::WorkFilter *>()->findChild<darkeye::WorkTagSelector *>();
    QVERIFY(shelfTags);
    shelfTags->clearSelection();
    QTRY_COMPARE(shelf->filteredWorkIds(), (QSet<qint64>{*firstId, *secondId, *untaggedId}));
    QCOMPARE(category->filteredWorkIds(), QSet<qint64>{*firstId});

    window.resize(1280, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *view = category->findChild<darkeye::DvdShelfView *>();
    QVERIFY(view);
    auto *scene = view->findChild<darkeye::dvd_scene::DvdSceneWidget *>();
    auto *heart = view->findChild<QToolButton *>(QStringLiteral("ShelfCoverFavorite"));
    QVERIFY(scene && heart);
    QVERIFY(view->openWork(*firstId));
    QTRY_VERIFY(scene->isFullyExpanded());
    QSignalSpy stateChanges(scene, &darkeye::dvd_scene::DvdSceneWidget::stateChanged);
    QSignalSpy favoriteUpdates(graph, &darkeye::graph::GraphManager::graphChanged);
    darkeye::PrivateRepository favorites(privateDatabase.database());
    for (const bool favorite : {true, false}) {
        heart->click();
        QTRY_VERIFY(!favoriteUpdates.isEmpty());
        favoriteUpdates.clear();
        QCOMPARE(favorites.isFavoriteWork(*firstId, &error), favorite);
        QCOMPARE(view->expandedWorkFavorited(), favorite);
        QCOMPARE(view->currentWorkId(), *firstId);
        QVERIFY(scene->isFullyExpanded());
        QVERIFY(stateChanges.isEmpty());
    }

    search->openRequested(QStringLiteral("manage_tags"));
    auto *tags = qobject_cast<darkeye::TagManagementWidget *>(pane->contentWidget(QStringLiteral("manage_tags")));
    QVERIFY(tags);
    QSignalSpy graphUpdates(graph, &darkeye::graph::GraphManager::graphChanged);
    QVERIFY(references.redirectTag(*sourceTag, *targetTag, &error));
    tags->tagsChanged();
    QTRY_COMPARE(category->filteredWorkIds(), (QSet<qint64>{*firstId, *secondId}));
    QCOMPARE(graphUpdates.size(), 1);

    // A delete originating from a category tab also reaches the normal shelf and the graph.
    QVERIFY(works.setDeleted(*firstId, true, &error));
    category->workDeleted(*firstId);
    QTRY_COMPARE(category->filteredWorkIds(), QSet<qint64>{*secondId});
    QCOMPARE(shelf->filteredWorkIds(), (QSet<qint64>{*secondId, *untaggedId}));
    QTRY_VERIFY(!graph->store().containsNode(QStringLiteral("w%1").arg(*firstId)));
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
    widget.initialize();
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

void MainWindowSmokeTest::settingsDialogClosesWithoutTitleBar()
{
    darkeye::ThemeService themes(*qApp);
    QVERIFY(themes.setTheme(darkeye::ThemeId::Dark));
    QWidget parent;
    QActionGroup shortcuts(&parent);
    darkeye::SettingsDialog dialog(themes, shortcuts, {}, {}, {}, &parent);
    dialog.open();

    QVERIFY(dialog.windowFlags().testFlag(Qt::FramelessWindowHint));
    QImage frame(dialog.size(), QImage::Format_ARGB32_Premultiplied);
    frame.fill(Qt::transparent);
    dialog.render(&frame, {}, {}, QWidget::DrawChildren);
    for (const auto &corner : {frame.rect().topLeft(), frame.rect().topRight(),
                               frame.rect().bottomLeft(), frame.rect().bottomRight()})
        QCOMPARE(frame.pixelColor(corner).alpha(), 0);
    QCOMPARE(frame.pixelColor(frame.width() / 2, 0), QColor(themes.currentTokens().border));
    QCOMPARE(frame.pixelColor(frame.width() / 2, 8), QColor(themes.currentTokens().background));
    auto *closeButton = dialog.findChild<QPushButton *>(QStringLiteral("DesignIconPushButton"),
                                                      Qt::FindDirectChildrenOnly);
    QVERIFY(closeButton && closeButton->isVisible());
    QVERIFY(!closeButton->icon().isNull());
    QTest::mouseClick(closeButton, Qt::LeftButton);
    QVERIFY(dialog.isHidden());

    dialog.open();
    QVERIFY(dialog.isVisible());
    QTest::keyClick(&dialog, Qt::Key_Escape);
    QVERIFY(dialog.isHidden());
}

void MainWindowSmokeTest::workFilterUsesSavedStartupOptions()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const darkeye::settings::Paths paths(directory.path(), directory.filePath(QStringLiteral("data")),
                                         directory.path());
    darkeye::SqliteConnection database;
    QString error;
    QVERIFY2(database.open(directory.filePath(QStringLiteral("works.db")), false, &error), qPrintable(error));
    QVERIFY2(darkeye::SchemaManager::initializeEmptyDatabase(
        database, darkeye::DatabaseKind::Public, &error), qPrintable(error));
    QList<qint64> greenCoverTags;
    for (const auto &tag : darkeye::WorkRepository(database.database()).tagOptions())
        if (tag.name == QStringLiteral("绿色封面")) greenCoverTags.append(tag.id);
    QVERIFY(!greenCoverTags.isEmpty());

    QSettings legacy(paths.settingsFile(), QSettings::IniFormat);
    legacy.setValue(QStringLiteral("WorkFilter/AddGreenCover"), true);
    legacy.sync();

    darkeye::ThemeService themes(*qApp);
    darkeye::WorkFilter initialFilter(database.database(), {}, themes, nullptr, paths.settingsFile());
    initialFilter.initialize();
    QCOMPARE(initialFilter.searchRequest().scope, darkeye::WorkFilter::Scope::Public);
    QCOMPARE(initialFilter.search().order, darkeye::WorkSortOrder::CreatedDescending);
    QCOMPARE(initialFilter.search().tagIds, greenCoverTags);

    QWidget parent;
    QActionGroup shortcuts(&parent);
    darkeye::SettingsDialog dialog(themes, shortcuts, database.database(), {}, paths, &parent);
    dialog.open();
    auto *menu = dialog.findChild<QListWidget *>();
    auto *search = dialog.findChild<QLineEdit *>();
    QVERIFY(menu && search);
    search->setText(QStringLiteral("初始排序"));
    QCOMPARE(menu->count(), 1);
    QCOMPARE(menu->item(0)->text(), QStringLiteral("初始排序 · 作品过滤器"));
    QTest::keyClick(search, Qt::Key_Return);

    auto *scope = dialog.findChild<QComboBox *>(QStringLiteral("workFilterInitialScope"));
    auto *sort = dialog.findChild<QComboBox *>(QStringLiteral("workFilterInitialSortOrder"));
    auto *greenCover = dialog.findChild<darkeye::ToggleSwitch *>(QStringLiteral("workFilterAddGreenCover"));
    QVERIFY(scope && sort && greenCover);
    QCOMPARE(scope->currentText(), QStringLiteral("公共库范围"));
    QCOMPARE(sort->currentText(), QStringLiteral("添加逆序"));
    QVERIFY(greenCover->isChecked());

    scope->setCurrentIndex(scope->findText(QStringLiteral("本地有视频")));
    sort->setCurrentIndex(sort->findText(QStringLiteral("番号顺序")));
    greenCover->setChecked(false);
    QCOMPARE(initialFilter.searchRequest().scope, darkeye::WorkFilter::Scope::Public);
    QCOMPARE(initialFilter.search().order, darkeye::WorkSortOrder::CreatedDescending);
    QCOMPARE(initialFilter.search().tagIds, greenCoverTags);

    darkeye::WorkFilter nextFilter(database.database(), {}, themes, nullptr, paths.settingsFile());
    nextFilter.initialize();
    QCOMPARE(nextFilter.searchRequest().scope, darkeye::WorkFilter::Scope::LocalVideo);
    QVERIFY(nextFilter.search().requireLocalVideo);
    QCOMPARE(nextFilter.search().order, darkeye::WorkSortOrder::SerialAscending);
    QVERIFY(nextFilter.search().tagIds.isEmpty());
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
    QVERIFY(saveEmptyWorkspace(paths));

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
    work.imageUrl = QStringLiteral("ROUTE-001.jpg");
    work.videoUrl = QStringLiteral("/videos/ROUTE-001.mp4");
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
    auto *search = window.findChild<darkeye::WorkspaceSearch *>();
    auto *workspace = window.findChild<darkeye::myads::WorkspaceWidget *>(QStringLiteral("mainWorkspace"));
    QVERIFY(search);
    QVERIFY(workspace);
    QVERIFY(!window.findChild<darkeye::Sidebar *>());
    auto currentWidget = [workspace] { auto *pane = workspace->activePane(); return pane->contentWidget(pane->currentContentId()); };
    QCOMPARE(workspace->rootPane()->contentCount(), 0);
    search->openRequested(QStringLiteral("mutiwork"));
    QCOMPARE(currentWidget()->objectName(), QStringLiteral("WorkPage"));
    QCOMPARE(window.findChild<QAction *>(QStringLiteral("add_masturbation_record"))->shortcut(),
             QKeySequence(QStringLiteral("M")));
    QCOMPARE(window.findChild<QAction *>(QStringLiteral("add_quick_work"))->shortcut(),
             QKeySequence(QStringLiteral("W")));
    QCOMPARE(window.findChild<QAction *>(QStringLiteral("add_makelove_record"))->shortcut(),
             QKeySequence(QStringLiteral("L")));
    QCOMPARE(window.findChild<QAction *>(QStringLiteral("add_sexual_rousal_record"))->shortcut(),
             QKeySequence(QStringLiteral("A")));
    QCOMPARE(window.findChild<QAction *>(QStringLiteral("capture"))->shortcut(),
             QKeySequence(QStringLiteral("C")));
    QCOMPARE(window.findChild<QAction *>(QStringLiteral("allcapture"))->shortcut(),
             QKeySequence(QStringLiteral("Shift+C")));
    QVERIFY(findThemeSelector(&window) == nullptr);

    search->openRequested(QStringLiteral("chart"));
    auto *statisticsPage = qobject_cast<darkeye::StatisticsChartPage *>(currentWidget());
    QVERIFY(statisticsPage != nullptr);
    QVERIFY(statisticsPage->isInitialized());

    search->openRequested(QStringLiteral("setting"));
    auto *themeSelector = findThemeSelector(&window);
    QVERIFY(themeSelector != nullptr);
    QCOMPARE(themeSelector->count(), 7);
    QCOMPARE(themeSelector->itemText(0), QStringLiteral("亮色主题"));
    QCOMPARE(themeSelector->itemText(1), QStringLiteral("暗色主题"));
    QCOMPARE(themeSelector->itemText(3), QStringLiteral("黄色"));
    QCOMPARE(themeSelector->itemText(4), QStringLiteral("绿色"));
    auto *settingsDialog = window.findChild<darkeye::SettingsDialog *>();
    QVERIFY(settingsDialog && settingsDialog->isVisible());
    auto *commonPage = settingsDialog->findChild<darkeye::CommonSettingsPage *>();
    QVERIFY(commonPage);
    auto *primaryColor = commonPage->findChild<darkeye::ColorPicker *>();
    QVERIFY(primaryColor);
    const auto rightEdge = [commonPage](const QWidget *widget) {
        return widget->mapTo(commonPage, widget->rect().topRight()).x();
    };
    QTRY_COMPARE(rightEdge(primaryColor), rightEdge(themeSelector));
    QVERIFY(commonPage->findChildren<darkeye::ToggleSwitch *>().isEmpty());
    QCOMPARE(settingsDialog->minimumWidth(), settingsDialog->maximumWidth());
    QVERIFY(!workspace->findPaneByContentId(QStringLiteral("setting")));
    QCOMPARE(currentWidget(), statisticsPage);
    auto *settingsMenu = settingsDialog->findChild<QListWidget *>();
    auto *settingsSearch = settingsDialog->findChild<QLineEdit *>();
    auto *settingsStack = settingsDialog->findChild<QStackedWidget *>();
    QVERIFY(settingsMenu && settingsSearch && settingsStack);
    QCOMPARE(settingsMenu->count(), 11);
    const int settingsWidth = settingsDialog->width();
    settingsDialog->setFixedWidth(qMin(settingsWidth, 560));
    const auto crawlerItems = settingsMenu->findItems(QStringLiteral("外部信息服务"), Qt::MatchExactly);
    QCOMPARE(crawlerItems.size(), 1);
    QTest::mouseClick(settingsMenu->viewport(), Qt::LeftButton, Qt::NoModifier,
                      settingsMenu->visualItemRect(crawlerItems.front()).center());
    auto *crawlerArea = qobject_cast<QScrollArea *>(settingsStack->currentWidget());
    QVERIFY(crawlerArea);
    auto *crawlerPage = crawlerArea->findChild<darkeye::CrawlerSettingsPage *>();
    QVERIFY(crawlerPage && crawlerPage->isVisible());
    QCOMPARE(crawlerArea->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    QTRY_COMPARE(crawlerArea->horizontalScrollBar()->maximum(), 0);
    const auto crawlerLabels = crawlerArea->findChildren<QLabel *>();
    const auto explanation = std::ranges::find_if(crawlerLabels, [](const QLabel *label) {
        return label->text().startsWith(QStringLiteral("接口地址使用 HTTP 或 HTTPS"));
    });
    QVERIFY(explanation != crawlerLabels.end());
    QVERIFY((*explanation)->wordWrap());
    QTRY_VERIFY((*explanation)->height() > (*explanation)->fontMetrics().height());
    settingsDialog->setFixedWidth(settingsWidth);

    const auto videoItems = settingsMenu->findItems(QStringLiteral("视频"), Qt::MatchExactly);
    QCOMPARE(videoItems.size(), 1);
    QTest::mouseClick(settingsMenu->viewport(), Qt::LeftButton, Qt::NoModifier,
                      settingsMenu->visualItemRect(videoItems.front()).center());
    QVERIFY(window.findChild<darkeye::VideoSettingsPage *>()->isVisible());
    auto *translation = window.findChild<darkeye::TranslationSettingsPage *>();
    QVERIFY(translation && !translation->isInitialized());
    settingsSearch->setText(QStringLiteral("不存在的设置"));
    QCOMPARE(settingsMenu->count(), 0);
    settingsSearch->setText(QStringLiteral("日志输出"));
    QCOMPARE(settingsMenu->count(), 1);
    QVERIFY(!translation->isInitialized());
    QTest::keyClick(settingsSearch, Qt::Key_Return);
    QVERIFY(translation->isVisible());
    auto *translationArea = qobject_cast<QScrollArea *>(settingsStack->currentWidget());
    QVERIFY(translationArea);
    QTRY_VERIFY(translationArea->verticalScrollBar()->value() > 0);
    settingsSearch->clear();
    QCOMPARE(settingsMenu->count(), 11);
    QCOMPARE(settingsMenu->currentItem()->text(), QStringLiteral("外部翻译服务"));
    QTest::keyClick(settingsSearch, Qt::Key_Escape);
    QVERIFY(settingsDialog->isHidden());
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    QCOMPARE(currentWidget(), statisticsPage);
    auto *aboutPage = window.findChild<darkeye::AboutSettingsPage *>();
    QVERIFY(aboutPage);
    aboutPage->initialize();
    const auto updateSwitches = aboutPage->findChildren<darkeye::ToggleSwitch *>();
    QCOMPARE(updateSwitches.size(), 1);
    auto *updateSwitch = updateSwitches.front();
    QCOMPARE(updateSwitch->accessibleName(), QStringLiteral("自动检查更新并提醒（每周一次）"));
    QVERIFY(!updateSwitch->isChecked());
    updateSwitch->click();
    QVERIFY(darkeye::settings::app(paths.settingsFile()).update.checkAndNotify);
    updateSwitch->click();
    QVERIFY(!darkeye::settings::app(paths.settingsFile()).update.checkAndNotify);
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
    QCOMPARE(loadedShortcutRows.size(), 17);
    QVERIFY(!shortcutPage->findChild<QWidget *>(QStringLiteral("ShortcutSettingRow_toggle_shelf_filter_bar")));
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
    auto *commandsRow = shortcutPage->findChild<QWidget *>(QStringLiteral("ShortcutSettingRow_workspace_commands"));
    QVERIFY(commandsRow);
    auto *commandsEditor = commandsRow->findChild<QKeySequenceEdit *>();
    QVERIFY(commandsEditor);
    const auto defaultCommandsKey = commandsEditor->keySequence();
    commandsEditor->setKeySequence(QKeySequence(Qt::Key_F8));
    QVERIFY(QMetaObject::invokeMethod(commandsEditor, "editingFinished", Qt::DirectConnection));
    auto *commandsAction = window.findChild<QAction *>(QStringLiteral("workspace_commands"));
    QVERIFY(commandsAction);
    QCOMPARE(commandsAction->shortcut(), QKeySequence(Qt::Key_F8));
    commandsAction->trigger();
    QTRY_VERIFY(search->isVisible());
    QTest::keyClick(search->findChild<QLineEdit *>(), Qt::Key_Escape);
    auto *commandsReset = findSettingsButton(commandsRow, QStringLiteral("恢复"));
    QVERIFY(commandsReset);
    commandsReset->click();
    QCOMPARE(commandsEditor->keySequence(), defaultCommandsKey);
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
    settingsDialog->close();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    search->openRequested(QStringLiteral("mutiwork"));
    QCOMPARE(currentWidget()->objectName(), QStringLiteral("WorkPage"));
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
    auto *workFilter = window.findChild<darkeye::WorkFilter *>();
    QVERIFY(workFilter);
    QVERIFY(!workPage->findChild<darkeye::WorkFilter *>());
    auto *viewButton = findWorkControl<QPushButton>(workPage, QStringLiteral("WorkCoverViewButton"));
    QVERIFY(viewButton && viewButton->isVisible());
    QVERIFY(viewButton->mapTo(workPage, QPoint()).x() > workPage->width() / 2);
    const QString originalViewTip = viewButton->toolTip();
    viewButton->click();
    QVERIFY(viewButton->toolTip() != originalViewTip);
    viewButton->click();
    workFilter->findChild<darkeye::WorkTagSelector *>()->clearSelection();
    emit workPage->detailRequested(*workId);
    shelfPage = window.findChild<darkeye::ShelfPage *>(QStringLiteral("ShelfPage"));
    QVERIFY(shelfPage != nullptr);
    QVERIFY(shelfPage->isInitialized());
    QCOMPARE(currentWidget(), shelfPage);
    auto *shelfView = shelfPage->findChild<darkeye::DvdShelfView *>();
    QVERIFY(shelfView);
    QTRY_COMPARE(shelfView->currentWorkId(), *workId);
    QVERIFY(!shelfPage->findChild<darkeye::WorkFilter *>());
    QTest::keyClick(window.windowHandle(), Qt::Key_F);
    QCOMPARE(currentWidget(), shelfPage);
    QVERIFY(!workspace->findPaneByContentId(QStringLiteral("work_filters")));

    commandsAction->trigger();
    auto *commandInput = search->findChild<QLineEdit *>();
    QVERIFY(commandInput && search->isVisible());
    commandInput->setText(QStringLiteral("作品过滤器"));
    QTest::keyClick(commandInput, Qt::Key_Return);
    QCOMPARE(currentWidget(), workFilter);
    QCOMPARE(workspace->activePane()->contentTitle(QStringLiteral("work_filters")),
             QStringLiteral("作品过滤器"));
    auto *tagSelector = workFilter->findChild<darkeye::WorkTagSelector *>();
    QVERIFY(tagSelector && tagSelector->isVisible());
    QVERIFY(!findWorkControl<QPushButton>(workFilter, QStringLiteral("WorkTagPanelButton")));
    QTest::keyClick(window.windowHandle(), Qt::Key_F);
    QCOMPARE(currentWidget(), workFilter);
    search->openRequested(QStringLiteral("actress"));
    actressPage = window.findChild<darkeye::PersonPage *>(QStringLiteral("ActressPage"));
    QVERIFY(actressPage != nullptr);
    QCOMPARE(currentWidget()->objectName(), QStringLiteral("ActressPage"));
    search->openRequested(QStringLiteral("actor"));
    QCOMPARE(currentWidget()->objectName(), QStringLiteral("ActorPage"));
    struct ManagementShortcut
    {
        QString title;
        QString route;
        const QMetaObject *pageType;
    };
    const QList<ManagementShortcut> managementShortcuts{
        {QStringLiteral("标签管理"), QStringLiteral("manage_tags"), &darkeye::TagManagementWidget::staticMetaObject},
        {QStringLiteral("片商管理"), QStringLiteral("manage_makers"), &darkeye::MakerManagementWidget::staticMetaObject},
        {QStringLiteral("厂牌管理"), QStringLiteral("manage_labels"), &darkeye::LabelManagementWidget::staticMetaObject},
        {QStringLiteral("系列管理"), QStringLiteral("manage_series"), &darkeye::LabelManagementWidget::staticMetaObject},
        {QStringLiteral("资料汇总"), QStringLiteral("manage_summary"), &darkeye::SummaryQueryWidget::staticMetaObject},
        {QStringLiteral("个人记录"), QStringLiteral("manage_records"), &darkeye::PersonalRecordManagementWidget::staticMetaObject},
        {QStringLiteral("批量移入回收站"), QStringLiteral("manage_deleted"), &darkeye::WorkBatchStateWidget::staticMetaObject},
        {QStringLiteral("回收站"), QStringLiteral("manage_trash"), &darkeye::WorkBatchStateWidget::staticMetaObject},
    };
    for (const auto &[title, route, pageType] : managementShortcuts)
    {
        commandsAction->trigger();
        QTRY_VERIFY(search->isVisible());
        auto *input = search->findChild<QLineEdit *>(QStringLiteral("workspaceSearchInput"));
        input->setText(title);
        QTest::keyClick(input, Qt::Key_Return);
        QCOMPARE(workspace->activePane()->currentContentId(), route);
        QCOMPARE(workspace->activePane()->contentTitle(route), title);
        QCOMPARE(currentWidget()->metaObject(), pageType);
        QVERIFY(currentWidget()->findChild<QScrollArea *>());
        QTRY_VERIFY(QApplication::focusWidget() == nullptr);
    }
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    commandsAction->trigger();
    QTRY_VERIFY(search->isVisible());
    auto *paletteInput = search->findChild<QLineEdit *>(QStringLiteral("workspaceSearchInput"));
    auto *paletteResults = search->findChild<QListWidget *>();
    QVERIFY(paletteInput->text().isEmpty());
    QVERIFY(paletteResults->count() > 9);
    const auto firstRoute = paletteResults->item(0)->data(Qt::UserRole).toString();
    const auto secondRoute = paletteResults->item(1)->data(Qt::UserRole).toString();

    QSet<QString> routes;
    bool reachedCommands = false;
    for (int index = 0; index < paletteResults->count(); ++index) {
        const auto *item = paletteResults->item(index);
        const auto route = item->data(Qt::UserRole).toString();
        QVERIFY(!item->text().contains(QStringLiteral("最近 · ")));
        QVERIFY(!route.contains(u'/'));
        if (item->text().startsWith(QStringLiteral("命令 · "))) {
            reachedCommands = true;
        } else {
            QVERIFY(item->text().startsWith(QStringLiteral("页面 · ")));
            QVERIFY(!reachedCommands);
        }
        routes.insert(route);
    }
    QCOMPARE(routes.size(), paletteResults->count());
    for (const auto &page : managementShortcuts)
        QVERIFY(routes.contains(page.route));
    for (const QString &route : {QStringLiteral("home"),
                                 QStringLiteral("mutiwork"),
                                 QStringLiteral("actress"), QStringLiteral("actor"),
                                 QStringLiteral("chart"), QStringLiteral("library_overview"),
                                 QStringLiteral("top_actresses"), QStringLiteral("record_heatmap"),
                                 QStringLiteral("graph"), QStringLiteral("shelf"),
                                 QStringLiteral("av"), QStringLiteral("inbox")})
        QVERIFY2(routes.contains(route), qPrintable(route));
    QVERIFY(routes.contains(QStringLiteral("setting")));
    QVERIFY(routes.contains(QStringLiteral("open_help")));
    QVERIFY(routes.contains(QStringLiteral("translate_overwrite")));
    QVERIFY(!routes.contains(QStringLiteral("information")));
    QVERIFY(!routes.contains(QStringLiteral("test_page")));
    QVERIFY(!routes.contains(QStringLiteral("fanart")));
    QVERIFY(!routes.contains(QStringLiteral("manage_work")));
    QVERIFY(!routes.contains(QStringLiteral("manage_batch")));

    paletteInput->setText(QStringLiteral("批量"));
    QCOMPARE(paletteResults->count(), 8);
    QCOMPARE(paletteResults->item(0)->data(Qt::UserRole).toString(), QStringLiteral("manage_deleted"));
    QCOMPARE(paletteResults->item(1)->data(Qt::UserRole).toString(), QStringLiteral("add_quick_work"));
    for (int index = 2; index < paletteResults->count(); ++index)
        QVERIFY(paletteResults->item(index)->text().startsWith(QStringLiteral("命令 · 批量操作 · ")));
    paletteInput->setText(QStringLiteral("覆盖已有中文"));
    QCOMPARE(paletteResults->count(), 1);
    QCOMPARE(paletteResults->item(0)->data(Qt::UserRole).toString(), QStringLiteral("translate_overwrite"));
    const auto pageBeforeBatch = workspace->activePane()->currentContentId();
    bool confirmationShown = false;
    QTimer::singleShot(0, &window, [&] {
        if (auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            confirmationShown = true;
            dialog->button(QMessageBox::No)->click();
        }
    });
    QTest::keyClick(paletteInput, Qt::Key_Return);
    QVERIFY(confirmationShown);
    QVERIFY(search->isHidden());
    QCOMPARE(workspace->activePane()->currentContentId(), pageBeforeBatch);
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    commandsAction->trigger();
    QTRY_VERIFY(search->isVisible());

    auto *maintenance = window.findChild<darkeye::WorkMaintenanceActions *>();
    QVERIFY(maintenance);
    QSignalSpy maintenanceCompleted(maintenance, &darkeye::WorkMaintenanceActions::worksChanged);
    paletteInput->setText(QStringLiteral("补译缺失"));
    QCOMPARE(paletteResults->count(), 1);
    QTest::keyClick(paletteInput, Qt::Key_Return);
    QTRY_COMPARE(maintenanceCompleted.size(), 1);
    QVERIFY(search->isHidden());
    QCOMPARE(workspace->activePane()->currentContentId(), pageBeforeBatch);
    commandsAction->trigger();
    QTRY_VERIFY(search->isVisible());

    paletteInput->setText(QStringLiteral("剧照浏览"));
    QCOMPARE(paletteResults->count(), 1);
    QVERIFY(paletteResults->item(0)->data(Qt::UserRole).toString().isEmpty());

    paletteInput->setText(QStringLiteral("管理"));
    QCOMPARE(paletteResults->count(), managementShortcuts.size());
    for (int index = 0; index < paletteResults->count(); ++index)
        QVERIFY(paletteResults->item(index)->data(Qt::UserRole).toString().startsWith(QStringLiteral("manage_")));

    paletteInput->clear();
    QCOMPARE(paletteResults->count(), routes.size());
    QTest::keyClick(paletteInput, Qt::Key_Down);
    QTest::keyClick(paletteInput, Qt::Key_Return);
    QCOMPARE(workspace->activePane()->currentContentId(), secondRoute);
    commandsAction->trigger();
    QTRY_VERIFY(search->isVisible());
    QCOMPARE(paletteResults->count(), routes.size());
    QCOMPARE(paletteResults->item(0)->data(Qt::UserRole).toString(), firstRoute);
    QCOMPARE(paletteResults->item(1)->data(Qt::UserRole).toString(), secondRoute);
    paletteInput->setText(QStringLiteral("标签管理"));
    QTRY_COMPARE(paletteResults->item(0)->data(Qt::UserRole).toString(), QStringLiteral("manage_tags"));
    QTest::keyClick(paletteInput, Qt::Key_Escape);
    commandsAction->trigger();
    QTRY_VERIFY(search->isVisible());
    QVERIFY(paletteInput->text().isEmpty());
    QCOMPARE(paletteResults->count(), routes.size());
    QCOMPARE(paletteResults->item(0)->data(Qt::UserRole).toString(), firstRoute);
    QTest::keyClick(paletteInput, Qt::Key_Escape);

    QVERIFY(!workspace->findPaneByContentId(QStringLiteral("database")));
    search->openRequested(QStringLiteral("manage_records"));
    QVERIFY(qobject_cast<darkeye::PersonalRecordManagementWidget *>(currentWidget()));
    search->openRequested(QStringLiteral("manage_summary"));
    auto *summary = qobject_cast<darkeye::SummaryQueryWidget *>(currentWidget());
    QVERIFY(summary);
    emit summary->workRequested(*workId);
    QCOMPARE(workspace->activePane()->currentContentId(), QStringLiteral("work_edit/%1").arg(*workId));
    auto *workEditor = qobject_cast<darkeye::AddWorkTabPage3 *>(currentWidget());
    QVERIFY(workEditor);
    auto *serialInput = findWorkControl<QLineEdit>(workEditor, QStringLiteral("WorkSerialInput"));
    auto *titleInput = findWorkControl<QPlainTextEdit>(workEditor, QStringLiteral("WorkChineseTitleInput"));
    auto *saveButton = workEditor->findChild<QPushButton *>(QStringLiteral("WorkSaveButton"));
    QVERIFY(serialInput && titleInput && saveButton);
    QTRY_COMPARE(serialInput->text(), work.serialNumber);
    const auto editedTitle = QStringLiteral("界面修改作品");
    titleInput->setPlainText(editedTitle);
    QCOMPARE(saveButton->text(), QStringLiteral("修改"));
    QVERIFY(saveButton->isEnabled());
    saveButton->click();
    QTRY_COMPARE(repository.findById(*workId).value_or(darkeye::Work{}).chineseTitle, editedTitle);
    const auto savedWork = repository.findById(*workId);
    QVERIFY(savedWork);
    QCOMPARE(savedWork->imageUrl, work.imageUrl);
    QCOMPARE(savedWork->videoUrl, work.videoUrl);
    serialInput->setText(QStringLiteral("UI-ADD-001"));
    QTest::keyClick(serialInput, Qt::Key_Return);
    QVERIFY(!saveButton->isEnabled());
    QVERIFY(!repository.existsSerial(serialInput->text()));
    search->openRequested(QStringLiteral("manage_summary"));
    QCOMPARE(currentWidget(), summary);
    QVERIFY(window.findChild<darkeye::MakerManagementWidget *>());
    QCOMPARE(window.findChildren<darkeye::LabelManagementWidget *>().size(), 2);
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
    search->openRequested(QStringLiteral("manage_work"));
    QVERIFY(!workspace->findPaneByContentId(QStringLiteral("manage_work")));
    search->openRequested(QStringLiteral("manage_tags"));
    auto *tagWidget =
        window.findChild<darkeye::TagManagementWidget *>();
    QVERIFY(tagWidget != nullptr);
    auto *pageBeforeSettings = currentWidget();
    search->openRequested(QStringLiteral("setting"));
    QVERIFY(settingsDialog->isVisible());
    QCOMPARE(currentWidget(), pageBeforeSettings);
    themeSelector->setCurrentIndex(1);
    QCOMPARE(themeService.current(), darkeye::ThemeId::Dark);
    settingsDialog->close();
    emit actressPage->detailRequested(darkeye::PersonKind::Actress, *actressId);
    actressDetail = window.findChild<darkeye::PersonDetailPage *>(
        QStringLiteral("ActressDetailPage"));
    QVERIFY(actressDetail != nullptr);
    QCOMPARE(currentWidget(), actressDetail);
    QCOMPARE(actressDetail->currentPersonId(), *actressId);
    QVERIFY(!actressDetail->findChild<ForceViewRhiWidget *>());
    emit actressDetail->editRequested(darkeye::PersonKind::Actress, *actressId);
    QCOMPARE(currentWidget()->objectName(), QStringLiteral("ModifyActressPage"));
    const auto dialogs = window.findChildren<QDialog *>();
    QVERIFY(std::ranges::none_of(dialogs, [](const QDialog *dialog) { return dialog->isVisible(); }));
    QVERIFY(window.windowTitle().startsWith(QStringLiteral("暗之眼 V")));
}


void MainWindowSmokeTest::actressPartialCaptureSavesCard()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    darkeye::settings::Paths paths(directory.path(), directory.filePath(QStringLiteral("data")));
    QVERIFY(paths.ensureRuntimeDirectories());
    QVERIFY(saveEmptyWorkspace(paths));
    darkeye::SqliteConnection publicDatabase, privateDatabase;
    QString error;
    QVERIFY(publicDatabase.open(paths.publicDatabase(), false, &error));
    QVERIFY(privateDatabase.open(paths.privateDatabase(), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        publicDatabase, darkeye::DatabaseKind::Public, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(
        privateDatabase, darkeye::DatabaseKind::Private, &error));
    const auto id = darkeye::PersonRepository(publicDatabase.database()).create(
        darkeye::PersonKind::Actress, QStringLiteral("截图测试"), {}, &error);
    QVERIFY(id);

    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    darkeye::MainWindow window(themes, publicDatabase.database(), privateDatabase.database(), paths);
    window.resize(1100, 480);
    window.show();
    auto *search = window.findChild<darkeye::WorkspaceSearch *>();
    QVERIFY(search);
    search->openRequested(QStringLiteral("actress_detail/%1").arg(*id));
    auto *detail = window.findChild<darkeye::PersonDetailPage *>();
    QVERIFY(detail);
    auto *card = detail->findChild<QWidget *>(QStringLiteral("PersonInfoPanel"));
    QVERIFY(card);
    QTRY_VERIFY(card->isVisible());
    QVERIFY(card->width() < detail->width());
    const QSize cardImageSize = card->grab().size();
    QVERIFY(!cardImageSize.isEmpty());
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    const QString path = directory.filePath(QStringLiteral("card.png"));
    const bool nativeDialogsDisabled = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    const auto restoreDialogs = qScopeGuard([nativeDialogsDisabled] {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDialogsDisabled);
    });
    QString selectedPath;
    QTimer selectFile;
    connect(&selectFile, &QTimer::timeout, &window, [&] {
        if (auto *dialog = window.findChild<QFileDialog *>(); dialog && dialog->isVisible()) {
            selectFile.stop();
            dialog->setDirectory(directory.path());
            if (auto *fileName = dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit")))
                fileName->setText(QStringLiteral("card.png"));
            selectedPath = dialog->selectedFiles().value(0);
            QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
        }
    });
    selectFile.start(20);
    auto *capture = window.findChild<QAction *>(QStringLiteral("capture"));
    QVERIFY(capture);
    QCOMPARE(capture->shortcut(), QKeySequence(Qt::Key_C));
    capture->trigger();
    selectFile.stop();
    QCOMPARE(selectedPath, path);
    QCOMPARE(QImage(path).size(), cardImageSize);
}

void MainWindowSmokeTest::homePreservesFiltersWhenWorkIsIncluded()
{
    QTemporaryDir directory;
    darkeye::settings::Paths paths(directory.path(), directory.filePath(QStringLiteral("data")));
    QVERIFY(paths.ensureRuntimeDirectories());
    QVERIFY(saveEmptyWorkspace(paths));
    darkeye::SqliteConnection publicDatabase, privateDatabase;
    QString error;
    QVERIFY(publicDatabase.open(paths.publicDatabase(), false, &error));
    QVERIFY(privateDatabase.open(paths.privateDatabase(), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(publicDatabase, darkeye::DatabaseKind::Public, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(privateDatabase, darkeye::DatabaseKind::Private, &error));
    darkeye::Work work;
    work.serialNumber = QStringLiteral("HOME-001");
    const auto workId = darkeye::WorkRepository(publicDatabase.database()).insertComplete(work, {}, {}, {}, &error);
    QVERIFY(workId);

    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    darkeye::MainWindow window(themes, publicDatabase.database(), privateDatabase.database(), paths);
    window.resize(1400, 700);
    window.show();
    auto *search = window.findChild<darkeye::WorkspaceSearch *>();
    auto *workspace = window.findChild<darkeye::myads::WorkspaceWidget *>(QStringLiteral("mainWorkspace"));
    auto *filter = window.findChild<darkeye::WorkFilter *>();
    QVERIFY(search && workspace && filter);
    filter->initialize();
    auto *serial = findWorkControl<QLineEdit>(filter, QStringLiteral("WorkSerialFilter"));
    auto *scope = findWorkControl<QComboBox>(filter, QStringLiteral("WorkScopeSelector"));
    auto *sort = findWorkControl<QComboBox>(filter, QStringLiteral("WorkSortSelector"));
    QVERIFY(serial && scope && sort);
    serial->setText(QStringLiteral("OTHER-001"));
    scope->setCurrentIndex(scope->findData(static_cast<int>(darkeye::WorkFilter::Scope::Favorites)));
    sort->setCurrentIndex(sort->findData(static_cast<int>(darkeye::WorkSortOrder::SerialAscending)));

    search->openRequested(QStringLiteral("home"));
    auto *pane = workspace->rootPane();
    auto *home = qobject_cast<darkeye::HomePage *>(pane->contentWidget(QStringLiteral("home")));
    QVERIFY(home);
    emit home->detailRequested(*workId);

    QCOMPARE(pane->currentContentId(), QStringLiteral("shelf"));
    QVERIFY(serial->text().isEmpty());
    QCOMPARE(scope->currentData().toInt(), static_cast<int>(darkeye::WorkFilter::Scope::Public));
    QCOMPARE(sort->currentData().toInt(), static_cast<int>(darkeye::WorkSortOrder::Random));
    auto *shelf = qobject_cast<darkeye::ShelfPage *>(pane->contentWidget(QStringLiteral("shelf")));
    QVERIFY(shelf);
    QCOMPARE(shelf->filteredWorkIds(), QSet<qint64>{*workId});
    QCOMPARE(shelf->currentWorkId(), *workId);
    QTRY_COMPARE(shelf->expandedWorkId(), *workId);

    serial->setText(work.serialNumber);
    sort->setCurrentIndex(sort->findData(static_cast<int>(darkeye::WorkSortOrder::SerialAscending)));
    QVERIFY(pane->setCurrentContentId(QStringLiteral("home")));
    emit home->detailRequested(*workId);
    QCOMPARE(pane->currentContentId(), QStringLiteral("shelf"));
    QCOMPARE(serial->text(), work.serialNumber);
    QCOMPARE(sort->currentData().toInt(), static_cast<int>(darkeye::WorkSortOrder::SerialAscending));
    QTRY_COMPARE(shelf->expandedWorkId(), *workId);
}

void MainWindowSmokeTest::timelineFollowsActressAndFilteredWorks()
{
    QTemporaryDir directory;
    darkeye::settings::Paths paths(directory.path(), directory.filePath(QStringLiteral("data")));
    QVERIFY(paths.ensureRuntimeDirectories());
    QVERIFY(saveEmptyWorkspace(paths));
    darkeye::SqliteConnection publicDatabase, privateDatabase;
    QString error;
    QVERIFY(publicDatabase.open(paths.publicDatabase(), false, &error));
    QVERIFY(privateDatabase.open(paths.privateDatabase(), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(publicDatabase, darkeye::DatabaseKind::Public, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(privateDatabase, darkeye::DatabaseKind::Private, &error));
    darkeye::PersonRepository people(publicDatabase.database());
    const auto actress = people.create(darkeye::PersonKind::Actress, QStringLiteral("人物甲"), QStringLiteral("Test A"), &error);
    const auto coStar = people.create(darkeye::PersonKind::Actress, QStringLiteral("人物乙"), {}, &error);
    QVERIFY(actress && coStar);
    auto actressDetails = people.findDetails(darkeye::PersonKind::Actress, *actress);
    QVERIFY(actressDetails);
    actressDetails->imagePath = QStringLiteral("timeline-actress.png");
    const auto portraitPath = QDir(paths.actressImageDirectory()).filePath(actressDetails->imagePath);
    QImage portrait(64, 64, QImage::Format_RGB32);
    portrait.fill(Qt::magenta);
    QVERIFY(portrait.save(portraitPath));
    QVERIFY(people.updateDetails(*actressDetails, &error));
    darkeye::WorkRepository works(publicDatabase.database());
    darkeye::Work linked, unrelated, shared;
    linked.serialNumber = QStringLiteral("TIME-001");
    linked.releaseDate = QStringLiteral("2020-01-01");
    unrelated.serialNumber = QStringLiteral("TIME-002");
    unrelated.releaseDate = QStringLiteral("2021-01-01");
    shared.serialNumber = QStringLiteral("TIME-003");
    shared.releaseDate = QStringLiteral("2022-01-01");
    const auto sharedId = works.insertComplete(shared, {*actress, *coStar}, {}, {}, &error);
    const auto linkedId = works.insertComplete(linked, {*actress}, {}, {}, &error);
    const auto unrelatedId = works.insertComplete(unrelated, {}, {}, {}, &error);
    QVERIFY(linkedId && unrelatedId && sharedId);
    const auto sharedDetails = works.findDetailsById(*sharedId);
    QVERIFY(sharedDetails && sharedDetails->actresses.size() == 2);

    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    darkeye::MainWindow window(themes, publicDatabase.database(), privateDatabase.database(), paths);
    window.resize(1400, 500);
    window.show();
    auto *search = window.findChild<darkeye::WorkspaceSearch *>();
    auto *workspace = window.findChild<darkeye::myads::WorkspaceWidget *>(QStringLiteral("mainWorkspace"));
    QVERIFY(search && workspace);

    auto *sourcePane = workspace->rootPane();
    const auto detailRoute = QStringLiteral("actress_detail/%1").arg(*actress);
    search->openRequested(detailRoute);
    auto *detail = sourcePane->contentWidget(detailRoute);
    QVERIFY(detail);
    QVERIFY(!detail->findChild<darkeye::ActressWorkTimeline *>());

    const auto switchRoute = QStringLiteral("actress_detail/switch");
    search->openRequested(QStringLiteral("actress_detail"));
    auto *switchDetail = qobject_cast<darkeye::PersonDetailPage *>(sourcePane->contentWidget(switchRoute));
    QVERIFY(switchDetail);
    QVERIFY(switchDetail->showPerson(sharedDetails->actresses.last().id));
    QVERIFY(sourcePane->setCurrentContentId(detailRoute));

    const auto timelineRoute = QStringLiteral("timeline");
    search->openRequested(timelineRoute);
    auto *timeline = qobject_cast<darkeye::ActressWorkTimeline *>(sourcePane->contentWidget(timelineRoute));
    QVERIFY(timeline);
    const auto thumbnails = [timeline] {
        return timeline->findChildren<darkeye::OctImage *>(QStringLiteral("TimelineActressThumbnail"));
    };
    auto *thumbnail = thumbnails().value(0);
    QVERIFY(thumbnail);
    QTRY_VERIFY(thumbnail->isVisible());
    QCOMPARE(thumbnail->source(), portraitPath);
    QTRY_VERIFY(!thumbnail->pixmap().isNull());
    QCOMPARE(thumbnail->pixmap().toImage().pixelColor(0, 0), QColor(Qt::magenta));
    const auto timelineIds = [timeline] {
        QSet<qint64> ids;
        for (const auto &work : timeline->works()) ids.insert(work.id);
        return ids;
    };
    const QSet<qint64> actressWorks{*linkedId, *sharedId};
    const QSet<qint64> unrelatedOnly{*unrelatedId};
    const QSet<qint64> allWorks{*linkedId, *unrelatedId, *sharedId};
    QCOMPARE(timelineIds(), actressWorks);
    QVERIFY(workspace->moveContent(sourcePane->paneId(), timelineRoute, sourcePane->paneId(),
                                   darkeye::myads::DropZone::Right));
    auto *timelinePane = workspace->findPaneByContentId(timelineRoute);
    QVERIFY(timelinePane && timelinePane != sourcePane);
    QCOMPARE(timelinePane->contentWidget(timelineRoute), timeline);

    workspace->setActivePane(sourcePane);
    search->openRequested(QStringLiteral("mutiwork"));
    auto *workList = qobject_cast<darkeye::WorkPage *>(sourcePane->contentWidget(QStringLiteral("mutiwork")));
    QVERIFY(workList);
    auto *serial = findWorkControl<QLineEdit>(window.findChild<darkeye::WorkFilter *>(), QStringLiteral("WorkSerialFilter"));
    QVERIFY(serial);
    auto *tags = window.findChild<darkeye::WorkFilter *>()->findChild<darkeye::WorkTagSelector *>();
    QVERIFY(tags);
    tags->clearSelection();
    QTRY_COMPARE(timelineIds(), allWorks);
    serial->setText(unrelated.serialNumber);
    QTRY_COMPARE(timelineIds(), unrelatedOnly);
    QVERIFY(thumbnails().isEmpty());
    workspace->setActivePane(timelinePane);
    QCOMPARE(timelineIds(), unrelatedOnly);

    workspace->setActivePane(sourcePane);
    search->openRequested(QStringLiteral("modify_actress/%1").arg(*actress));
    QCOMPARE(timelineIds(), actressWorks);
    QCOMPARE(thumbnails().size(), 1);
    search->openRequested(QStringLiteral("shelf"));
    auto *shelf = qobject_cast<darkeye::ShelfPage *>(sourcePane->contentWidget(QStringLiteral("shelf")));
    QVERIFY(shelf);
    auto *scene = shelf->findChild<darkeye::dvd_scene::DvdSceneWidget *>();
    QVERIFY(scene);
    QCOMPARE(shelf->filteredWorkIds(), unrelatedOnly);
    emit workList->detailRequested(*sharedId);
    QCOMPARE(shelf->filteredWorkIds(), unrelatedOnly);
    QCOMPARE(shelf->currentWorkId(), 0);
    serial->clear();
    QTRY_COMPARE(shelf->filteredWorkIds(), allWorks);
    scene->renderWidget()->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *splitter = qobject_cast<QSplitter *>(sourcePane->parentWidget());
    QVERIFY(splitter);
    splitter->setSizes({1050, 350});
    QTRY_VERIFY(scene->canExpand());
    QTRY_COMPARE(timelineIds(), allWorks);
    QVERIFY(scene->focusItem(QString::number(*sharedId)));
    QCOMPARE(scene->state(), darkeye::dvd_scene::DvdInteractionController::State::Focusing);
    QCOMPARE(shelf->focusedWorkId(), 0);
    QCOMPARE(timelineIds(), allWorks);
    QCOMPARE(switchDetail->currentPersonId(), sharedDetails->actresses.last().id);
    QTRY_COMPARE(shelf->focusedWorkId(), *sharedId);
    QCOMPARE(shelf->expandedWorkId(), 0);
    QTRY_COMPARE(switchDetail->currentPersonId(), sharedDetails->actresses.first().id);
    QCOMPARE(sourcePane->currentContentId(), QStringLiteral("shelf"));
    QTRY_COMPARE(thumbnails().size(), 2);
    QCOMPARE(timelineIds(), actressWorks);
    QCOMPARE(timeline->markerCount(), 3);
    QVERIFY(thumbnails().at(0)->y() < thumbnails().at(1)->y());

    search->openRequested(QStringLiteral("work_detail/%1").arg(*sharedId));
    QTRY_COMPARE(shelf->expandedWorkId(), *sharedId);
    QTRY_VERIFY(scene->isFullyExpanded());
    scene->toggleOpen();
    QTRY_COMPARE(shelf->expandedWorkId(), 0);
    QCOMPARE(shelf->focusedWorkId(), *sharedId);
    QCOMPARE(timelineIds(), actressWorks);
    scene->returnToShelf();
    QTRY_COMPARE(shelf->focusedWorkId(), 0);
    QTRY_VERIFY(thumbnails().isEmpty());
    QTRY_COMPARE(timelineIds(), allWorks);

    search->openRequested(QStringLiteral("work_detail/%1").arg(*unrelatedId));
    QTRY_COMPARE(shelf->expandedWorkId(), *unrelatedId);
    QTRY_COMPARE(switchDetail->currentPersonId(), 0);
    QTRY_VERIFY(timelineIds().isEmpty());
    QVERIFY(thumbnails().isEmpty());
    shelf->filterByTag(999);
    QTRY_VERIFY(timelineIds().isEmpty());

    search->openRequested(QStringLiteral("mutiwork"));
    serial->setText(unrelated.serialNumber);
    tags->clearSelection();
    QTRY_COMPARE(workList->filteredWorkIds(), unrelatedOnly);
    QTRY_COMPARE(shelf->filteredWorkIds(), unrelatedOnly);
    QCOMPARE(shelf->filteredWorks().first().id, workList->filteredWorks().first().id);
    emit workList->detailRequested(*unrelatedId);
    QCOMPARE(sourcePane->currentContentId(), QStringLiteral("shelf"));
    QCOMPARE(shelf->currentWorkId(), *unrelatedId);
    QCOMPARE(serial->text(), unrelated.serialNumber);

    const auto filterRoute = QStringLiteral("work_filters");
    search->openRequested(filterRoute);
    auto *filterPane = workspace->findPaneByContentId(filterRoute);
    QVERIFY(filterPane);
    auto *filter = filterPane->contentWidget(filterRoute);
    QVERIFY(filter && filter->isAncestorOf(tags));
    QVERIFY(workspace->moveContent(filterPane->paneId(), filterRoute, filterPane->paneId(),
                                   darkeye::myads::DropZone::Right));
    auto *movedPane = workspace->findPaneByContentId(filterRoute);
    QVERIFY(movedPane && movedPane != filterPane);
    QCOMPARE(movedPane->contentWidget(filterRoute), filter);
    QVERIFY(movedPane->removeContent(filterRoute));
    search->openRequested(filterRoute);
    QCOMPARE(workspace->findPaneByContentId(filterRoute)->contentWidget(filterRoute), filter);
    QCOMPARE(serial->text(), unrelated.serialNumber);
    QCOMPARE(shelf->filteredWorkIds(), unrelatedOnly);

    QVERIFY(workspace->findPaneByContentId(filterRoute)->removeContent(filterRoute));
    serial->clear();
    QTRY_COMPARE(shelf->filteredWorkIds(), allWorks);
    workspace->setActivePane(sourcePane);
    splitter = qobject_cast<QSplitter *>(sourcePane->parentWidget());
    QVERIFY(splitter);
    splitter->setSizes({1050, 350});
    QTRY_VERIFY(scene->canExpand());
    search->openRequested(QStringLiteral("work_detail/%1").arg(*sharedId));
    QTRY_COMPARE(shelf->expandedWorkId(), *sharedId);
    QTRY_VERIFY(scene->isFullyExpanded());

    QVERIFY(workspace->moveContent(sourcePane->paneId(), switchRoute, timelinePane->paneId(),
                                   darkeye::myads::DropZone::Center));
    workspace->setActivePane(sourcePane);
    QVERIFY(switchDetail->showPerson(*actress));
    QVERIFY(timelinePane->setCurrentContentId(switchRoute));
    QVERIFY(timelinePane->setCurrentContentId(timelineRoute));
    QTRY_COMPARE(thumbnails().size(), 1);
    QCOMPARE(timelineIds(), actressWorks);
    auto *actressButton = shelf->findChild<QToolButton *>(QStringLiteral("actress%1").arg(*coStar));
    QVERIFY(actressButton && actressButton->isVisible());
    QTest::mousePress(actressButton, Qt::LeftButton);
    QApplication::processEvents();
    QCOMPARE(thumbnails().size(), 1);
    QCOMPARE(timelineIds(), actressWorks);
    QTest::mouseRelease(actressButton, Qt::LeftButton, Qt::NoModifier, QPoint(-1, -1));
    QCOMPARE(switchDetail->currentPersonId(), *actress);
    QCOMPARE(timelineIds(), actressWorks);
    QTest::mouseClick(actressButton, Qt::LeftButton);
    QCOMPARE(switchDetail->currentPersonId(), *coStar);
    QCOMPARE(timelineIds(), QSet<qint64>{*sharedId});
    QCOMPARE(thumbnails().size(), 1);

    serial->setText(shared.serialNumber);
    QTRY_COMPARE(shelf->filteredWorkIds(), QSet<qint64>{*sharedId});
    search->openRequested(QStringLiteral("work_detail/%1").arg(*sharedId));
    QTRY_VERIFY(scene->isFullyExpanded());
    QTRY_COMPARE(timelineIds(), actressWorks);
    emit timeline->workRequested(*linkedId);
    QVERIFY(serial->text().isEmpty());
    QCOMPARE(shelf->filteredWorkIds(), allWorks);
    QTRY_COMPARE(shelf->expandedWorkId(), *linkedId);
}

void MainWindowSmokeTest::localRelationshipGraphFollowsActiveContent()
{
    QTemporaryDir directory;
    darkeye::settings::Paths paths(directory.path(), directory.filePath(QStringLiteral("data")));
    QVERIFY(paths.ensureRuntimeDirectories());
    QVERIFY(saveEmptyWorkspace(paths));
    darkeye::SqliteConnection publicDatabase, privateDatabase;
    QString error;
    QVERIFY(publicDatabase.open(paths.publicDatabase(), false, &error));
    QVERIFY(privateDatabase.open(paths.privateDatabase(), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(publicDatabase, darkeye::DatabaseKind::Public, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(privateDatabase, darkeye::DatabaseKind::Private, &error));
    darkeye::PersonRepository people(publicDatabase.database());
    const auto actress = people.create(darkeye::PersonKind::Actress, QStringLiteral("人物甲"), QStringLiteral("Test A"), &error);
    const auto coStar = people.create(darkeye::PersonKind::Actress, QStringLiteral("人物乙"), QStringLiteral("Test B"), &error);
    QVERIFY(actress && coStar);
    QSqlQuery cupUpdate(publicDatabase.database());
    QVERIFY(cupUpdate.prepare(QStringLiteral("UPDATE actress SET cup='C' WHERE actress_id=?")));
    cupUpdate.addBindValue(*actress);
    QVERIFY(cupUpdate.exec());
    darkeye::PrivateRepository privateData(privateDatabase.database());
    QVERIFY(privateData.addFavoriteActress(*actress, QStringLiteral("Test A"), &error));
    darkeye::WorkRepository works(publicDatabase.database());
    darkeye::Work first, second;
    first.serialNumber = QStringLiteral("LOCAL-001");
    second.serialNumber = QStringLiteral("LOCAL-002");
    const auto firstId = works.insertComplete(first, {*actress, *coStar}, {}, {}, &error);
    const auto secondId = works.insertComplete(second, {}, {}, {}, &error);
    QVERIFY(firstId && secondId);

    const QString actressNode = QStringLiteral("a%1").arg(*actress);
    const QString coStarNode = QStringLiteral("a%1").arg(*coStar);
    const QString firstNode = QStringLiteral("w%1").arg(*firstId);
    const QString secondNode = QStringLiteral("w%1").arg(*secondId);
    const QStringList linkedNodes{actressNode, firstNode};
    const QStringList sharedWorkNodes{actressNode, coStarNode, firstNode};
    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    darkeye::MainWindow window(themes, publicDatabase.database(), privateDatabase.database(), paths);
    window.resize(1200, 800);
    window.show();
    auto *search = window.findChild<darkeye::WorkspaceSearch *>();
    auto *workspace = window.findChild<darkeye::myads::WorkspaceWidget *>(QStringLiteral("mainWorkspace"));
    QVERIFY(search && workspace);
    auto *sourcePane = workspace->rootPane();
    auto *graphPane = workspace->split(sourcePane, darkeye::myads::Placement::Right);
    QVERIFY(graphPane);
    workspace->setActivePane(graphPane);
    search->openRequested(QStringLiteral("graph"));
    auto *globalPage = graphPane->contentWidget(QStringLiteral("graph"));
    QVERIFY(globalPage);
    auto *globalGraph = globalPage->findChild<ForceViewRhiWidget *>();
    QVERIFY(globalGraph);
    QTRY_COMPARE(globalGraph->getNodeIds().size(), 4);
    search->openRequested(QStringLiteral("local_graph"));
    auto *localPage = graphPane->contentWidget(QStringLiteral("local_graph"));
    QVERIFY(localPage);
    auto *localGraph = localPage->findChild<ForceViewRhiWidget *>();
    QVERIFY(localGraph);
    QCOMPARE(localGraph->getNodeIds().size(), 0);

    workspace->setActivePane(sourcePane);
    const QString actressRoute = QStringLiteral("actress_detail/%1").arg(*actress);
    search->openRequested(actressRoute);
    QTRY_COMPARE(localGraph->getNodeIds(), sharedWorkNodes);
    search->openRequested(QStringLiteral("work_edit/%1").arg(*secondId));
    QTRY_COMPARE(localGraph->getNodeIds(), QStringList{secondNode});
    auto *editor = sourcePane->contentWidget(sourcePane->currentContentId());
    QVERIFY(editor);
    QVERIFY(!editor->findChild<ForceViewRhiWidget *>());
    search->openRequested(QStringLiteral("modify_actress/%1").arg(*actress));
    QTRY_COMPARE(localGraph->getNodeIds(), sharedWorkNodes);
    search->openRequested(QStringLiteral("shelf"));
    auto *shelf = qobject_cast<darkeye::ShelfPage *>(sourcePane->contentWidget(QStringLiteral("shelf")));
    QVERIFY(shelf);
    auto *scene = shelf->findChild<darkeye::dvd_scene::DvdSceneWidget *>();
    auto *splitter = qobject_cast<QSplitter *>(sourcePane->parentWidget());
    QVERIFY(scene && splitter);
    window.resize(1400, 650);
    splitter->setSizes({1050, 350});
    auto *filter = window.findChild<darkeye::WorkFilter *>();
    QVERIFY(filter);
    filter->findChild<darkeye::WorkTagSelector *>()->clearSelection();
    auto *serialFilter = findWorkControl<QLineEdit>(filter, QStringLiteral("WorkSerialFilter"));
    QVERIFY(serialFilter);
    serialFilter->setText(second.serialNumber);
    QTRY_COMPARE(shelf->filteredWorkIds(), QSet<qint64>{*secondId});
    QTRY_VERIFY(scene->canExpand());
    search->openRequested(QStringLiteral("work_detail/%1").arg(*secondId));
    QTRY_COMPARE(shelf->currentWorkId(), *secondId);
    QTRY_COMPARE(shelf->focusedWorkId(), *secondId);
    QTRY_COMPARE(shelf->expandedWorkId(), *secondId);
    QTRY_COMPARE(localGraph->getNodeIds(), QStringList{secondNode});
    QVERIFY(graphPane->setCurrentContentId(QStringLiteral("graph")));
    workspace->setActivePane(graphPane);
    emit globalGraph->nodeLeftClicked(firstNode);
    QTRY_COMPARE(shelf->expandedWorkId(), *firstId);
    QCOMPARE(workspace->activePane(), sourcePane);
    QCOMPARE(graphPane->currentContentId(), QStringLiteral("graph"));
    QVERIFY(serialFilter->text().isEmpty());
    emit globalGraph->nodeLeftClicked(secondNode);
    QTRY_COMPARE(shelf->expandedWorkId(), *secondId);
    QVERIFY(graphPane->setCurrentContentId(QStringLiteral("local_graph")));

    // Switching tabs in the same pane updates the graph as well.
    QVERIFY(sourcePane->setCurrentContentId(actressRoute));
    QTRY_COMPARE(localGraph->getNodeIds(), sharedWorkNodes);
    search->openRequested(QStringLiteral("mutiwork"));
    auto *serial = findWorkControl<QLineEdit>(window.findChild<darkeye::WorkFilter *>(), QStringLiteral("WorkSerialFilter"));
    QVERIFY(serial);
    auto *tags = window.findChild<darkeye::WorkFilter *>()->findChild<darkeye::WorkTagSelector *>();
    QVERIFY(tags);
    tags->clearSelection();
    QTRY_COMPARE(localGraph->getNodeIds().size(), 4);
    serial->setText(first.serialNumber);
    QTRY_COMPARE(localGraph->getNodeIds(), sharedWorkNodes);
    serial->setText(second.serialNumber);
    QTRY_COMPARE(localGraph->getNodeIds(), QStringList{secondNode});
    serial->setText(QStringLiteral("NO-MATCH"));
    QTRY_COMPARE(localGraph->getNodeIds().size(), 0);

    search->openRequested(QStringLiteral("actress"));
    auto *actressPage = sourcePane->contentWidget(QStringLiteral("actress"));
    QVERIFY(actressPage);
    auto *nameFilter = actressPage->findChild<QLineEdit *>();
    QVERIFY(nameFilter);
    QComboBox *scope = nullptr;
    QComboBox *cup = nullptr;
    for (auto *combo : actressPage->findChildren<QComboBox *>()) {
        if (combo->findText(QStringLiteral("收藏库范围")) >= 0) scope = combo;
        if (combo->findText(QStringLiteral("C")) >= 0) cup = combo;
    }
    QVERIFY(scope && cup);
    QTRY_COMPARE(localGraph->getNodeIds(), sharedWorkNodes);
    cup->setCurrentIndex(cup->findText(QStringLiteral("C")));
    QTRY_COMPARE(localGraph->getNodeIds(), linkedNodes);
    cup->setCurrentIndex(0);
    QTRY_COMPARE(localGraph->getNodeIds(), sharedWorkNodes);
    nameFilter->setText(QStringLiteral("人物乙"));
    const QStringList coStarNodes{coStarNode, firstNode};
    QTRY_COMPARE(localGraph->getNodeIds(), coStarNodes);
    scope->setCurrentIndex(scope->findText(QStringLiteral("收藏库范围")));
    QTRY_VERIFY(localGraph->getNodeIds().isEmpty());
    nameFilter->clear();
    QTRY_COMPARE(localGraph->getNodeIds(), linkedNodes);
    QVERIFY(sourcePane->setCurrentContentId(QStringLiteral("mutiwork")));
    QTRY_VERIFY(localGraph->getNodeIds().isEmpty());
    QVERIFY(sourcePane->setCurrentContentId(QStringLiteral("actress")));
    QTRY_COMPARE(localGraph->getNodeIds(), linkedNodes);

    workspace->setActivePane(graphPane);
    QVERIFY(graphPane->setCurrentContentId(QStringLiteral("graph")));
    QTRY_COMPARE(globalGraph->getNodeIds().size(), 4);
}

void MainWindowSmokeTest::workspaceSearchShortcutsOpenBothModes()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const darkeye::settings::Paths paths(QCoreApplication::applicationDirPath(), temp.path());
    QVERIFY(paths.ensureRuntimeDirectories());
    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    darkeye::MainWindow window(themes, {}, {}, paths);
    window.resize(1200, 800);
    window.show();
    window.raise();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());

#ifdef Q_OS_MACOS
    auto *menu = window.findChild<QMenu *>(QStringLiteral("workspaceMenu"));
    QVERIFY(menu);
    QCOMPARE(menu->actions(), (QList<QAction *>{
        window.findChild<QAction *>(QStringLiteral("workspace_search")),
        window.findChild<QAction *>(QStringLiteral("workspace_commands")),
        window.findChild<QAction *>(QStringLiteral("open_settings")),
        window.findChild<QAction *>(QStringLiteral("close_tab"))}));
    QVERIFY(menu->toNSMenu());
#endif
    auto *search = window.findChild<darkeye::WorkspaceSearch *>();
    QVERIFY(search && !search->isVisible());
    auto *input = search->findChild<QLineEdit *>(QStringLiteral("workspaceSearchInput"));
    auto *results = search->findChild<QListWidget *>(QStringLiteral("workspaceSearchResults"));
    QVERIFY(input && results);

    QTest::keyClick(window.windowHandle(), Qt::Key_O, Qt::ControlModifier);
    QTRY_VERIFY(input->hasFocus());
    QVERIFY(results->isHidden());
    QCOMPARE(search->y(), window.centralWidget()->height() / 6);
    QTest::keyClick(window.windowHandle(), Qt::Key_P, Qt::ControlModifier);
    QTRY_VERIFY(input->hasFocus() && results->isVisible());
    QVERIFY(results->count() > 0);
    QCOMPARE(search->y(), window.centralWidget()->height() / 6);
    QVERIFY(search->y() + search->height() <= window.centralWidget()->height() - search->y());

    window.resize(1200, 600);
    QTRY_COMPARE(search->y(), window.centralWidget()->height() / 6);
    QCOMPARE(search->y() + search->height(), window.centralWidget()->height() - search->y());
    QTRY_VERIFY(results->verticalScrollBar()->maximum() > 0);

    auto *viewport = results->viewport();
    const auto rowPoint = [results](int row) {
        const QRect rect = results->visualItemRect(results->item(row));
        return QPoint(rect.right() - 16, rect.center().y());
    };
    const auto rowColor = [&window, viewport, &rowPoint](int row) {
        const auto snapshot = window.grab();
        const QPoint pixel = viewport->mapTo(&window, rowPoint(row)) * snapshot.devicePixelRatio();
        return snapshot.toImage().pixelColor(pixel);
    };
    const int selectedRow = results->currentRow();
    QVERIFY(results->count() >= 3);
    const int firstHoverRow = selectedRow == 1 ? 0 : 1;
    const int secondHoverRow = selectedRow == 2 ? 0 : 2;
    const auto moveTo = [&window](QWidget *widget, const QPoint &point) {
        QTest::mouseMove(window.windowHandle(), widget->mapTo(&window, point));
    };
    for (const auto theme : {darkeye::ThemeId::Dark, darkeye::ThemeId::Light}) {
        QVERIFY(themes.setTheme(theme));
        const auto palette = results->palette();
        moveTo(viewport, rowPoint(firstHoverRow));
        QTRY_COMPARE(rowColor(firstHoverRow), palette.color(QPalette::AlternateBase));
        QCOMPARE(results->currentRow(), selectedRow);
        moveTo(viewport, rowPoint(secondHoverRow));
        QTRY_COMPARE(rowColor(secondHoverRow), palette.color(QPalette::AlternateBase));
        QCOMPARE(rowColor(firstHoverRow), palette.color(QPalette::Base));
        moveTo(input, input->rect().center());
        QTRY_COMPARE(rowColor(secondHoverRow), palette.color(QPalette::Base));
    }

    auto *helpAction = window.findChild<QAction *>(QStringLiteral("open_help"));
    QVERIFY(helpAction);
    helpAction->disconnect(&window); // Verify dispatch without launching the browser.
    QSignalSpy helpTriggered(helpAction, &QAction::triggered);
    input->setText(QStringLiteral("帮助"));
    QTRY_COMPARE(results->count(), 1);
    QCOMPARE(results->item(0)->text(), QStringLiteral("命令 · 帮助"));
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(helpTriggered.size(), 1);
    QVERIFY(search->isHidden());

    QTest::keyClick(window.windowHandle(), Qt::Key_P, Qt::ControlModifier);
    QTRY_VERIFY(input->hasFocus() && results->isVisible());
    input->setText(QStringLiteral("github"));
    QTRY_VERIFY(results->count() == 1 &&
                results->item(0)->text() == QStringLiteral("命令 · GitHub 项目仓库"));
    QDesktopServices::setUrlHandler(QStringLiteral("https"), this, "captureOpenedUrl");
    QTest::mouseClick(results->viewport(), Qt::LeftButton, Qt::NoModifier,
                      results->visualItemRect(results->item(0)).center());
    QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
    QCOMPARE(m_openedUrl, QUrl(QStringLiteral("https://github.com/gty5678/darkeye")));
    QVERIFY(search->isHidden());
}

void MainWindowSmokeTest::settingsThemeSwitchPreservesCustomPrimary()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const darkeye::settings::Paths paths(QCoreApplication::applicationDirPath(), temp.path());
    QVERIFY(paths.ensureRuntimeDirectories());
    QVERIFY(saveEmptyWorkspace(paths));
    darkeye::ThemeService themes(*qApp);
    QVERIFY(themes.setTheme(darkeye::ThemeId::Light));
    darkeye::MainWindow window(themes, {}, {}, paths);
    auto *openSettings = window.findChild<QAction *>(QStringLiteral("open_settings"));
    QVERIFY(openSettings);
    openSettings->trigger();
    auto *selector = findThemeSelector(&window);
    auto *picker = window.findChild<darkeye::ColorPicker *>();
    QVERIFY(selector && picker);

    const darkeye::OklchColor color{0.55f, 0.15f, 245.0f};
    const QString expected = color.hex(darkeye::ThemeService::colorMode()).toLower();
    emit picker->oklchConfirmed(color);
    QCOMPARE(themes.customPrimary(), expected);
    for (const auto theme : darkeye::ThemeService::availableThemes())
    {
        if (theme == darkeye::ThemeId::Light) continue;
        const int index = selector->findData(static_cast<int>(theme));
        QVERIFY(index >= 0);
        selector->setCurrentIndex(index);
        QCOMPARE(themes.current(), theme);
        selector->setCurrentIndex(selector->findData(static_cast<int>(darkeye::ThemeId::Light)));
        QCOMPARE(themes.current(), darkeye::ThemeId::Light);
        QCOMPARE(themes.customPrimary(), expected);
        QCOMPARE(picker->color(), expected);
    }
    const auto saved = darkeye::settings::app(paths.settingsFile());
    QCOMPARE(saved.customPrimaryOklch, color.toString());
    QCOMPARE(saved.customPrimary, color.hex(darkeye::ThemeTokens::defaultColorSpace));
}

void MainWindowSmokeTest::settingsCommandsOpenExistingOperations()
{
    const bool usedQtDialogs = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    const auto restoreDialogPreference = qScopeGuard([usedQtDialogs] {
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, usedQtDialogs);
    });
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const darkeye::settings::Paths paths(QCoreApplication::applicationDirPath(), temp.path());
    QVERIFY(paths.ensureRuntimeDirectories());
    darkeye::ThemeService themes(*qApp);
    darkeye::MainWindow window(themes, {}, {}, paths);
    window.resize(1200, 800);
    window.show();

    auto *commands = window.findChild<QAction *>(QStringLiteral("workspace_commands"));
    auto *search = window.findChild<darkeye::WorkspaceSearch *>();
    QVERIFY(commands && search);
    auto *input = search->findChild<QLineEdit *>(QStringLiteral("workspaceSearchInput"));
    auto *results = search->findChild<QListWidget *>(QStringLiteral("workspaceSearchResults"));
    QVERIFY(input && results);
    commands->trigger();
    QTRY_VERIFY(results->isVisible());
    input->setText(QStringLiteral("数据库运维"));
    QTRY_COMPARE(results->count(), 13);
    const auto cleanup = results->findItems(
        QStringLiteral("命令 · 数据库运维 · 数据库清理碎片"), Qt::MatchExactly);
    QCOMPARE(cleanup.size(), 1);
    results->scrollToItem(cleanup.first());
    class ConfirmationObserver final : public QObject
    {
    public:
        bool shown = false;
        QWidget *parent = nullptr;

    protected:
        bool eventFilter(QObject *object, QEvent *event) override
        {
            if (event->type() != QEvent::Show) return false;
            auto *message = qobject_cast<QMessageBox *>(object);
            if (!message || !message->text().contains(QStringLiteral("先创建本地备份"))) return false;
            shown = true;
            parent = message->parentWidget();
            QTimer::singleShot(0, message, [message] { message->done(QMessageBox::No); });
            return false;
        }
    } confirmation;
    qApp->installEventFilter(&confirmation);

    QTest::mouseClick(results->viewport(), Qt::LeftButton, Qt::NoModifier,
                      results->visualItemRect(cleanup.first()).center());
    auto *settings = window.findChild<darkeye::SettingsDialog *>();
    QVERIFY(settings && settings->isHidden());
    QTRY_VERIFY_WITH_TIMEOUT(confirmation.shown, 2000);
    QCOMPARE(confirmation.parent, &window);
    QVERIFY(settings->isHidden());
    QVERIFY(search->isHidden());

    auto *openSettings = window.findChild<QAction *>(QStringLiteral("open_settings"));
    QVERIFY(openSettings);
    openSettings->trigger();
    QVERIFY(settings->isVisible());
    auto *themeSelector = findThemeSelector(settings);
    QVERIFY(themeSelector && themeSelector->isVisible());
}

void MainWindowSmokeTest::restoresBrowsingWorkspacePreset()
{
    using darkeye::myads::SidebarSide;
    using darkeye::myads::WorkspaceWidget;

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const darkeye::settings::Paths paths(QCoreApplication::applicationDirPath(), temp.path());
    QVERIFY(paths.ensureRuntimeDirectories());
    darkeye::ThemeService themes(*qApp);
    darkeye::MainWindow window(themes, {}, {}, paths);
    window.resize(1500, 950);
    window.show();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());

    auto *workspace = window.findChild<WorkspaceWidget *>(QStringLiteral("mainWorkspace"));
    auto *commands = window.findChild<QAction *>(QStringLiteral("workspace_commands"));
    auto *search = window.findChild<darkeye::WorkspaceSearch *>();
    QVERIFY(workspace && commands && search);
    auto *input = search->findChild<QLineEdit *>(QStringLiteral("workspaceSearchInput"));
    auto *results = search->findChild<QListWidget *>(QStringLiteral("workspaceSearchResults"));
    QVERIFY(input && results);
    const auto runCommand = [&](const QString &query) {
        commands->trigger();
        input->setText(query);
        QTRY_COMPARE(results->count(), 1);
        results->setCurrentRow(0);
        QTest::keyClick(input, Qt::Key_Return);
    };
    QCOMPARE(workspace->panes().size(), 2);
    QCOMPARE(workspace->activePane()->currentContentId(), QStringLiteral("shelf"));
    QVERIFY(workspace->isSidebarOpen(SidebarSide::Left));
    QVERIFY(workspace->isSidebarOpen(SidebarSide::Right));
    auto *shelf = workspace->rootPane()->contentWidget(QStringLiteral("shelf"));
    QVERIFY(shelf);
    runCommand(QStringLiteral("作品展厅"));
    QVERIFY(workspace->findPaneByContentId(QStringLiteral("home")));

    runCommand(QStringLiteral("恢复预设工作区布局"));
    const auto panes = workspace->panes();
    QCOMPARE(panes.size(), 2);
    QCOMPARE(panes.first()->contentIds(), (QStringList{
        QStringLiteral("shelf"), QStringLiteral("mutiwork"), QStringLiteral("actress"), QStringLiteral("actor")}));
    QCOMPARE(panes.first()->currentContentId(), QStringLiteral("shelf"));
    QCOMPARE(panes.first()->contentWidget(QStringLiteral("shelf")), shelf);
    QCOMPARE(workspace->activePane(), panes.first());
    QCOMPARE(panes.last()->currentContentId(), QStringLiteral("timeline"));
    QVERIFY(!workspace->findPaneByContentId(QStringLiteral("home")));
    QVERIFY(workspace->isSidebarOpen(SidebarSide::Left));
    QVERIFY(workspace->isSidebarOpen(SidebarSide::Right));
    QCOMPARE(workspace->sidebarPane(SidebarSide::Left)->contentIds(), QStringList{QStringLiteral("work_filters")});
    auto *graph = workspace->sidebarPane(SidebarSide::Right);
    QCOMPARE(graph->contentIds(),
             (QStringList{QStringLiteral("local_graph"), QStringLiteral("inbox")}));
    QCOMPARE(graph->currentContentId(), QStringLiteral("local_graph"));
    auto *detail = workspace->findPaneByContentId(QStringLiteral("actress_detail/switch"));
    QVERIFY(detail);
    QVERIFY(workspace->isSidebar(detail));
    QCOMPARE(graph->parentWidget(), detail->parentWidget());
    QVERIFY(graph->y() < detail->y());
    const auto lowerFraction = [](QWidget *upper, QWidget *lower) {
        return double(lower->height()) / (upper->height() + lower->height());
    };
    QTRY_VERIFY(std::abs(lowerFraction(panes.first(), panes.last()) - 0.2) < 0.03);
    QTRY_VERIFY(std::abs(lowerFraction(graph, detail) - 2.0 / 3) < 0.03);
}

#ifdef Q_OS_MACOS
void MainWindowSmokeTest::nativeCommandShortcuts()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const darkeye::settings::Paths paths(QCoreApplication::applicationDirPath(), temp.path());
    QVERIFY(paths.ensureRuntimeDirectories());
    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    darkeye::MainWindow window(themes, {}, {}, paths);
    window.show();
    window.raise();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());

    struct Shortcut {
        const char *action;
        unsigned short keyCode;
        const char *characters;
        Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    };
    // macOS virtual key codes and characters emitted by a US keyboard.
    const Shortcut shortcuts[] = {
        {"workspace_search", 31, "o"},
        {"workspace_commands", 35, "p"},
        {"toggle_left_sidebar", 18, "!", Qt::ShiftModifier},
        {"toggle_right_sidebar", 19, "@", Qt::ShiftModifier},
        {"next_tab", 30, "}", Qt::ShiftModifier},
        {"previous_tab", 33, "{", Qt::ShiftModifier},
        {"next_pane", 124, "\uF703", Qt::AltModifier | Qt::KeypadModifier},
        {"previous_pane", 123, "\uF702", Qt::AltModifier | Qt::KeypadModifier},
        {"close_tab", 13, "w"},
        {"open_settings", 43, ","},
    };
    for (const auto &shortcut : shortcuts) {
        auto *action = window.findChild<QAction *>(QString::fromLatin1(shortcut.action));
        QVERIFY(action);
        // Observe activation without opening or closing UI during the sequence.
        action->disconnect(&window);
        QSignalSpy triggered(action, &QAction::triggered);
        postMacShortcut(window, shortcut.keyCode, shortcut.characters, shortcut.modifiers);
        QVERIFY2(triggered.wait(500), shortcut.action);
        QCOMPARE(triggered.size(), 1);
    }

    auto *sidebar = window.findChild<QAction *>(QStringLiteral("toggle_left_sidebar"));
    sidebar->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_7));
    QSignalSpy triggered(sidebar, &QAction::triggered);
    postMacShortcut(window, 18, "!", Qt::ShiftModifier);
    QVERIFY(!triggered.wait(150));
    postMacShortcut(window, 26, "&", Qt::ShiftModifier);
    QVERIFY(triggered.wait(500));
    QCOMPARE(triggered.size(), 1);
}
#endif

void MainWindowSmokeTest::navigationBarOpensPages()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const darkeye::settings::Paths paths(QCoreApplication::applicationDirPath(), temp.path());
    QVERIFY(paths.ensureRuntimeDirectories());
    QVERIFY(saveEmptyWorkspace(paths));
    darkeye::ThemeService themes(*qApp);
    QVERIFY(themes.setTheme(darkeye::ThemeId::Light));
    darkeye::MainWindow window(themes, {}, {}, paths);
    window.resize(900, 700);
    window.show();

    const auto barName = QStringLiteral("pageNavigationBar");
    auto *bar = window.findChild<QWidget *>(barName);
    QVERIFY(bar && bar->isVisible());
    const auto tokens = themes.currentTokens();
    QCOMPARE(bar->grab(QRect(2, 2, 1, 1)).toImage().pixelColor(0, 0), QColor(tokens.pageBackground));
    QMap<QString, QPushButton *> buttons;
    for (auto *button : bar->findChildren<QPushButton *>()) {
        QVERIFY(!button->icon().isNull());
        QVERIFY(!button->isCheckable());
        QVERIFY(!button->accessibleName().isEmpty());
        auto *label = button->findChild<QLabel *>(QStringLiteral("pageNavigationLabel"));
        QVERIFY(label && !label->isHidden());
        QCOMPARE(label->text(), button->accessibleName());
        buttons.insert(button->property("navigationId").toString(), button);
    }
    const QStringList pageRoutes{
        QStringLiteral("home"), QStringLiteral("mutiwork"), QStringLiteral("shelf"),
        QStringLiteral("actress"), QStringLiteral("actor"), QStringLiteral("chart"),
        QStringLiteral("graph"), QStringLiteral("av"), QStringLiteral("inbox"),
        QStringLiteral("actress_detail"),
        darkeye::WorkspaceSearch::independentActressCommand,
        QStringLiteral("modify_actress"), QStringLiteral("modify_actor"), QStringLiteral("work_edit"),
        QStringLiteral("library_overview"), QStringLiteral("top_actresses"), QStringLiteral("record_heatmap"),
        QStringLiteral("local_graph"), QStringLiteral("timeline"), QStringLiteral("work_filters"),
        QStringLiteral("manage_tags"), QStringLiteral("manage_makers"), QStringLiteral("manage_labels"),
        QStringLiteral("manage_series"), QStringLiteral("manage_summary"), QStringLiteral("manage_records"),
        QStringLiteral("manage_deleted"), QStringLiteral("manage_trash")
    };
    for (const auto &route : pageRoutes)
        QVERIFY2(buttons.contains(route), qPrintable(route));
    QVERIFY(!buttons.contains(QStringLiteral("fanart")));

    auto *navigationToggle = buttons.value(QStringLiteral("toggle_navigation"));
    QVERIFY(navigationToggle && navigationToggle->isVisible());
    const int buttonLeft = navigationToggle->mapTo(bar, QPoint()).x();
    for (auto *button : std::as_const(buttons))
        QCOMPARE(button->mapTo(bar, QPoint()).x(), buttonLeft);
    auto *home = buttons.value(QStringLiteral("home"));
    const QRect iconArea(0, 0, 29, home->height());
    const auto expandedIcon = home->grab(iconArea).toImage();
    const auto expandedWidth = bar->width();
    constexpr int collapsedWidth = 56;
    QTest::mouseClick(navigationToggle, Qt::LeftButton);
    QCOMPARE(bar->width(), expandedWidth);
    QTRY_VERIFY(bar->width() > collapsedWidth && bar->width() < expandedWidth);
    for (auto *button : std::as_const(buttons))
        QCOMPARE(button->mapTo(bar, QPoint()).x(), buttonLeft);
    QTRY_COMPARE(bar->width(), collapsedWidth);
    for (auto *button : std::as_const(buttons)) {
        QTRY_VERIFY(button->findChild<QLabel *>(QStringLiteral("pageNavigationLabel"))->isHidden());
        QTRY_COMPARE(button->mapTo(bar, QPoint()).x(), buttonLeft);
        QTRY_COMPARE(button->mapTo(bar, QPoint()).x(), (bar->width() - button->width()) / 2);
    }
    QCOMPARE(home->grab(iconArea).toImage(), expandedIcon);
    {
        darkeye::MainWindow reopened(themes, {}, {}, paths);
        auto *reopenedBar = reopened.findChild<QWidget *>(barName);
        QVERIFY(reopenedBar);
        QCOMPARE(reopenedBar->width(), bar->width());
    }
    QTest::mouseClick(navigationToggle, Qt::LeftButton);
    QCOMPARE(bar->width(), collapsedWidth);
    QTRY_VERIFY(bar->width() > collapsedWidth && bar->width() < expandedWidth);
    for (auto *button : std::as_const(buttons))
        QCOMPARE(button->mapTo(bar, QPoint()).x(), buttonLeft);
    QTRY_COMPARE(bar->width(), expandedWidth);
    for (auto *button : std::as_const(buttons))
        QTRY_COMPARE(button->mapTo(bar, QPoint()).x(), buttonLeft);

    auto *workspace = window.findChild<darkeye::myads::WorkspaceWidget *>();
    const auto inboxRoute = QStringLiteral("inbox");
    auto *inbox = buttons.value(inboxRoute);
    QVERIFY(workspace && inbox && inbox->isVisible());
    auto *scroll = bar->findChild<QScrollArea *>();
    QVERIFY(scroll);
    const int initialScrollValue = scroll->verticalScrollBar()->value();
    scroll->ensureWidgetVisible(inbox, 0, 0);
    const QPoint center = inbox->rect().center();
    QVERIFY(scroll->viewport()->rect().contains(inbox->mapTo(scroll->viewport(), center)));
    const auto backgroundAt = [&](bool hovered) {
        QStyleOptionButton option;
        option.initFrom(inbox);
        option.features = QStyleOptionButton::Flat;
        option.state.setFlag(QStyle::State_MouseOver, hovered);
        QImage image(inbox->size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(tokens.pageBackground));
        QPainter painter(&image);
        inbox->style()->drawControl(QStyle::CE_PushButton, &option, &painter, inbox);
        painter.end();
        return image.pixelColor(inbox->width() / 2, 4);
    };
    QCOMPARE(backgroundAt(false), QColor(tokens.pageBackground));
    QCOMPARE(backgroundAt(true), QColor(tokens.inputBackground));
    QTest::mouseClick(inbox, Qt::LeftButton);
    QCOMPARE(workspace->activePane(), workspace->sidebarPane(darkeye::myads::SidebarSide::Right));
    QVERIFY(workspace->isSidebarOpen(darkeye::myads::SidebarSide::Right));
    QCOMPARE(workspace->activePane()->currentContentId(), inboxRoute);
    QTest::mouseClick(inbox, Qt::LeftButton);
    QCOMPARE(workspace->activePane()->contentCount(), 1);
    QVERIFY(!inbox->isChecked());

    auto *sidebar = workspace->sidebarPane(darkeye::myads::SidebarSide::Right);
    QVERIFY(sidebar->removeContent(inboxRoute));
    workspace->setSidebarOpen(darkeye::myads::SidebarSide::Right, false);
    auto *commands = window.findChild<QAction *>(QStringLiteral("workspace_commands"));
    auto *search = window.findChild<darkeye::WorkspaceSearch *>();
    QVERIFY(commands && search);
    commands->trigger();
    QTRY_VERIFY(search->isVisible());
    auto *input = search->findChild<QLineEdit *>(QStringLiteral("workspaceSearchInput"));
    QVERIFY(input);
    input->setText(QStringLiteral("采集任务"));
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(workspace->findPaneByContentId(inboxRoute), sidebar);
    QVERIFY(workspace->isSidebarOpen(darkeye::myads::SidebarSide::Right));

    scroll->verticalScrollBar()->setValue(initialScrollValue);
    QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
    QVERIFY(!scroll->verticalScrollBar()->isVisible());
    auto *searchButton = buttons.value(QStringLiteral("workspace_search"));
    auto *commandsButton = buttons.value(QStringLiteral("workspace_commands"));
    QVERIFY(searchButton && searchButton->isVisible());
    QVERIFY(commandsButton && commandsButton->isVisible());
    const auto searchPosition = searchButton->mapTo(bar, QPoint());
    const auto commandsPosition = commandsButton->mapTo(bar, QPoint());
    QVERIFY(searchPosition.y() + searchButton->height() <= scroll->mapTo(bar, QPoint()).y());
    QVERIFY(commandsPosition.y() >= scroll->mapTo(bar, QPoint(0, scroll->height())).y());
    const auto scrollPoint = scroll->viewport()->rect().center();
    QWheelEvent wheel(scrollPoint, scroll->viewport()->mapToGlobal(scrollPoint), {},
                      QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(scroll->viewport(), &wheel);
    QVERIFY(scroll->verticalScrollBar()->value() > 0);
    QCOMPARE(searchButton->mapTo(bar, QPoint()), searchPosition);
    QCOMPARE(commandsButton->mapTo(bar, QPoint()), commandsPosition);
    const auto filterRoute = QStringLiteral("work_filters");
    auto *filter = buttons.value(filterRoute);
    scroll->ensureWidgetVisible(filter, 0, 0);
    QVERIFY(scroll->viewport()->rect().contains(filter->mapTo(scroll->viewport(), filter->rect().center())));
    QTest::mouseClick(filter, Qt::LeftButton);
    QVERIFY(workspace->findPaneByContentId(filterRoute));

    auto *edit = buttons.value(QStringLiteral("work_edit"));
    scroll->ensureWidgetVisible(edit, 0, 0);
    QTest::mouseClick(edit, Qt::LeftButton);
    QTRY_VERIFY(search->isVisible());
    QVERIFY(input->placeholderText().contains(QStringLiteral("编辑")));
    search->hide();

    auto *results = search->findChild<QListWidget *>(QStringLiteral("workspaceSearchResults"));
    QVERIFY(results);
    struct DialogEntry {
        QString id;
        QString query;
        QString windowTitle;
    };
    const DialogEntry dialogs[] = {
        {QStringLiteral("add_sexual_rousal_record"), QStringLiteral("晨勃"),
         QStringLiteral("添加无意识性器官唤醒记录")},
        {QStringLiteral("add_quick_work"), QStringLiteral("添加作品"),
         QStringLiteral("批量录入与补全作品")},
        {QStringLiteral("add_makelove_record"), QStringLiteral("做爱"), QStringLiteral("添加做爱记录")},
        {QStringLiteral("add_masturbation_record"), QStringLiteral("自慰"), QStringLiteral("添加自慰记录")},
    };
    const auto openedDialogTitle = [&window](auto activate) {
        QString title;
        QTimer timer;
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, &window, [&title] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                title = dialog->windowTitle();
                dialog->reject();
            }
        });
        timer.start(0);
        activate();
        return title;
    };
    for (const auto &[id, query, windowTitle] : dialogs) {
        auto *button = buttons.value(id);
        QVERIFY(button);
        scroll->ensureWidgetVisible(button, 0, 0);
        QCOMPARE(openedDialogTitle([button] { QTest::mouseClick(button, Qt::LeftButton); }), windowTitle);

        commands->trigger();
        QTRY_VERIFY(search->isVisible());
        input->setText(query);
        QTRY_VERIFY(results->count() == 1 && results->item(0)->data(Qt::UserRole).toString() == id);
        QCOMPARE(openedDialogTitle([input] { QTest::keyClick(input, Qt::Key_Return); }), windowTitle);
        QVERIFY(search->isHidden());
    }

    auto *help = buttons.value(QStringLiteral("open_help"));
    auto *settingsButton = buttons.value(QStringLiteral("open_settings"));
    QVERIFY(help && settingsButton && help->isVisible() && settingsButton->isVisible());
    QVERIFY(help->mapTo(bar, QPoint()).y() < settingsButton->mapTo(bar, QPoint()).y());
}

void MainWindowSmokeTest::navigationBarSettingsRememberVisibility()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const darkeye::settings::Paths paths(QCoreApplication::applicationDirPath(), temp.path());
    QVERIFY(paths.ensureRuntimeDirectories());
    QVERIFY(saveEmptyWorkspace(paths));
    darkeye::ThemeService themes(*qApp);
    darkeye::MainWindow window(themes, {}, {}, paths);
    window.resize(900, 700);
    window.show();

    const auto barName = QStringLiteral("pageNavigationBar");
    auto *bar = window.findChild<QWidget *>(barName);
    QVERIFY(bar && bar->isVisible());
    QMap<QString, QPushButton *> buttons;
    for (auto *button : bar->findChildren<QPushButton *>())
        buttons.insert(button->property("navigationId").toString(), button);
    auto *help = buttons.value(QStringLiteral("open_help"));
    auto *settingsButton = buttons.value(QStringLiteral("open_settings"));
    QVERIFY(help && settingsButton && help->isVisible() && settingsButton->isVisible());
    QTest::mouseClick(settingsButton, Qt::LeftButton);
    auto *settings = window.findChild<darkeye::SettingsDialog *>();
    QVERIFY(settings && settings->isVisible());
    auto *settingsMenu = settings->findChild<QListWidget *>(QStringLiteral("DesignListWidget"));
    QVERIFY(settingsMenu);
    const auto navigationSections = settingsMenu->findItems(QStringLiteral("导航栏"), Qt::MatchExactly);
    QCOMPARE(navigationSections.size(), 1);
    QTest::mouseClick(settingsMenu->viewport(), Qt::LeftButton, Qt::NoModifier,
                      settingsMenu->visualItemRect(navigationSections.first()).center());
    auto *toggle = settings->findChild<darkeye::ToggleSwitch *>(QStringLiteral("navigationBarVisible"));
    QVERIFY(toggle && toggle->isChecked());

    QSet<QString> configurableItems;
    darkeye::ToggleSwitch *helpToggle = nullptr;
    for (auto *item : settings->findChildren<darkeye::ToggleSwitch *>()) {
        const auto id = item->property("navigationId").toString();
        if (id.isEmpty()) continue;
        QVERIFY(item->isChecked());
        configurableItems.insert(id);
        if (id == QStringLiteral("open_help"))
            helpToggle = item;
    }
    QCOMPARE(configurableItems.size(), buttons.size() - 1);
    QVERIFY(helpToggle);
    helpToggle->click();
    QVERIFY(help->isHidden());
    QVERIFY(settingsButton->isVisible());
    QCOMPARE(darkeye::settings::app(paths.settingsFile()).hiddenNavigationItems,
             QStringList{QStringLiteral("open_help")});

    toggle->click();
    QVERIFY(bar->isHidden());
    QVERIFY(!darkeye::settings::app(paths.settingsFile()).navigationBarVisible);
    {
        darkeye::MainWindow reopened(themes, {}, {}, paths);
        auto *reopenedBar = reopened.findChild<QWidget *>(barName);
        QVERIFY(reopenedBar && reopenedBar->isHidden());
        const auto reopenedButtons = reopenedBar->findChildren<QPushButton *>();
        const auto reopenedHelp = std::ranges::find_if(reopenedButtons, [](const auto *button) {
            return button->property("navigationId").toString() == QStringLiteral("open_help");
        });
        QVERIFY(reopenedHelp != reopenedButtons.end() && (*reopenedHelp)->isHidden());
    }

    settings->hide();
    auto *settingsAction = window.findChild<QAction *>(QStringLiteral("open_settings"));
    QVERIFY(settingsAction);
    settingsAction->trigger();
    QVERIFY(settings->isVisible());
    toggle->click();
    QVERIFY(bar->isVisible());
    QVERIFY(darkeye::settings::app(paths.settingsFile()).navigationBarVisible);
    QVERIFY(help->isHidden());
    helpToggle->click();
    QVERIFY(help->isVisible());
    QVERIFY(darkeye::settings::app(paths.settingsFile()).hiddenNavigationItems.isEmpty());
}

void MainWindowSmokeTest::sidebarButtonsAndVisibilitySettings()
{
    using darkeye::myads::Placement;
    using darkeye::myads::SidebarSide;
    using darkeye::myads::WorkspaceWidget;

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const darkeye::settings::Paths paths(QCoreApplication::applicationDirPath(), temp.path());
    QVERIFY(paths.ensureRuntimeDirectories());
    QVERIFY(saveEmptyWorkspace(paths));
    darkeye::ThemeService themes(*qApp);
    darkeye::MainWindow window(themes, {}, {}, paths);
    window.resize(1000, 700);
    window.show();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());

    const auto leftName = QStringLiteral("MyAdsLeftSidebarToggle");
    const auto rightName = QStringLiteral("MyAdsRightSidebarToggle");
    auto *workspace = window.findChild<WorkspaceWidget *>(QStringLiteral("mainWorkspace"));
    QVERIFY(workspace);
    auto *left = workspace->findChild<QToolButton *>(leftName);
    auto *right = workspace->findChild<QToolButton *>(rightName);
    QVERIFY(left && left->isVisible());
    QVERIFY(right && right->isVisible());

    workspace->resetToSingleEmptyPane();
    auto *center = workspace->rootPane();
    QCOMPARE(left->parentWidget(), center);
    QCOMPARE(right->parentWidget(), center);
    QTRY_COMPARE(left->x(), 0);
    QTRY_COMPARE(right->geometry().right(), center->width() - 1);
    QCOMPARE(left->y(), right->y());

    auto *rightPane = workspace->split(center, Placement::Right);
    QVERIFY(rightPane);
    QCOMPARE(left->parentWidget(), center);
    QCOMPARE(right->parentWidget(), rightPane);

    const auto addTab = [workspace](darkeye::myads::PaneWidget *pane, const QString &title) {
        QVERIFY(workspace->fillPane(pane, workspace->createContentConfig().setWindowTitle(title)));
    };
    addTab(center, QStringLiteral("Center left"));
    addTab(rightPane, QStringLiteral("Center right"));
    auto *leftSidebar = workspace->sidebarPane(SidebarSide::Left);
    auto *rightSidebar = workspace->sidebarPane(SidebarSide::Right);
    auto *leftEditor = new QLineEdit;
    auto *rightEditor = new QLineEdit;
    QVERIFY(workspace->fillPane(leftSidebar, workspace->createContentConfig().setWidget(leftEditor)));
    QVERIFY(workspace->fillPane(rightSidebar, workspace->createContentConfig().setWidget(rightEditor)));
    auto *leftTabs = leftSidebar->findChild<QTabBar *>();
    auto *rightTabs = rightSidebar->findChild<QTabBar *>();
    auto *centerTabs = center->findChild<QTabBar *>();
    QVERIFY(leftTabs && rightTabs && centerTabs);

    QCOMPARE(left->contentsRect().size(), QSize(26, 26));
    QCOMPARE(right->contentsRect().size(), QSize(26, 26));
    QCOMPARE(left->contentsMargins(), QMargins(6, 0, 0, 0));
    QCOMPARE(right->contentsMargins(), QMargins(0, 0, 6, 0));
    const auto verifyStripLine = [workspace, left, right] {
        const auto *pane = qobject_cast<darkeye::myads::PaneWidget *>(left->parentWidget());
        const int y = pane->mapTo(workspace, QPoint(0, darkeye::myads::DockTabBarHeight - 1)).y();
        const auto snapshot = workspace->grab();
        const auto image = snapshot.toImage();
        QList<QRect> selectedTabs;
        for (const QString &id : workspace->visiblePaneIds()) {
            auto *bar = workspace->pane(id)->findChild<QTabBar *>();
            const QRect tab = bar->tabRect(bar->currentIndex());
            selectedTabs.append(QRect(bar->mapTo(workspace, tab.topLeft()), tab.size()));
        }
        const int firstX = left->mapTo(workspace, QPoint()).x();
        const int lastX = right->mapTo(workspace, right->rect().topRight()).x();
        for (int x = firstX; x <= lastX; ++x) {
            const QPoint pixel = QPoint(x, y) * snapshot.devicePixelRatio();
            // The beveled feet overlap the neighboring tab and blend into the baseline.
            if (std::ranges::any_of(selectedTabs, [x](const QRect &tab) {
                    return qAbs(x - tab.left()) <= 12 || qAbs(x - tab.right()) <= 12;
                })) continue;
            const bool selected = std::ranges::any_of(selectedTabs, [x](const QRect &tab) {
                return x >= tab.left() && x <= tab.right();
            });
            const QColor expected = selected ? workspace->theme().tabSelected : workspace->theme().border;
            QVERIFY2(image.pixelColor(pixel) == expected,
                     qPrintable(QStringLiteral("Unexpected tab strip color at x=%1").arg(x)));
        }
    };
    const auto clickAt = [&window](const QPoint &point) {
        QTest::mouseClick(window.windowHandle(), Qt::LeftButton, Qt::NoModifier, point);
    };
    const QPoint leftClick = left->mapTo(&window, left->contentsRect().center());
    const QPoint rightClick = right->mapTo(&window, right->contentsRect().center());
    verifyStripLine();
    clickAt(left->mapTo(&window, QPoint(2, left->height() / 2)));
    QVERIFY(!workspace->isSidebarOpen(SidebarSide::Left));
    clickAt(right->mapTo(&window, QPoint(right->width() - 3, right->height() / 2)));
    QVERIFY(!workspace->isSidebarOpen(SidebarSide::Right));
    clickAt(leftClick);
    QTRY_VERIFY(workspace->isSidebarOpen(SidebarSide::Left));
    QCOMPARE(left->parentWidget(), workspace->sidebarPane(SidebarSide::Left));
    QTRY_COMPARE(left->mapTo(&window, left->contentsRect().center()), leftClick);
    verifyStripLine();
    QTRY_VERIFY(leftTabs->hasFocus());
    leftEditor->setFocus();
    QTRY_VERIFY(leftEditor->hasFocus());
    clickAt(rightClick);
    QTRY_VERIFY(workspace->isSidebarOpen(SidebarSide::Right));
    QCOMPARE(right->parentWidget(), workspace->sidebarPane(SidebarSide::Right));
    QTRY_COMPARE(right->mapTo(&window, right->contentsRect().center()), rightClick);
    QCoreApplication::processEvents();
    QTRY_VERIFY(rightTabs->hasFocus());
    QCOMPARE(workspace->activePane(), rightSidebar);
    rightEditor->setFocus();
    QTRY_VERIFY(rightEditor->hasFocus());
    clickAt(rightClick);
    QTRY_VERIFY(!workspace->isSidebarOpen(SidebarSide::Right));
    QTRY_VERIFY(centerTabs->hasFocus());
    QVERIFY(workspace->isSidebarOpen(SidebarSide::Left));
    clickAt(rightClick);
    QTRY_VERIFY(workspace->isSidebarOpen(SidebarSide::Right));
    QTRY_VERIFY(rightTabs->hasFocus());
    verifyStripLine();
    clickAt(leftClick);
    QTRY_VERIFY(!workspace->isSidebarOpen(SidebarSide::Left));
    QVERIFY(workspace->isSidebarOpen(SidebarSide::Right));
    clickAt(rightClick);
    QTRY_VERIFY(!workspace->isSidebarOpen(SidebarSide::Right));
    clickAt(rightClick);
    QTRY_VERIFY(workspace->isSidebarOpen(SidebarSide::Right));

    auto *settingsAction = window.findChild<QAction *>(QStringLiteral("open_settings"));
    QVERIFY(settingsAction);
    settingsAction->trigger();
    auto *settings = window.findChild<darkeye::SettingsDialog *>();
    QVERIFY(settings && settings->isVisible());
    auto *sidebarSettings = settings->findChild<darkeye::SidebarSettingsPage *>();
    QVERIFY(sidebarSettings);
    sidebarSettings->initialize();
    auto *leftSetting = sidebarSettings->findChild<darkeye::ToggleSwitch *>(QStringLiteral("leftSidebarToggleVisible"));
    auto *rightSetting = sidebarSettings->findChild<darkeye::ToggleSwitch *>(QStringLiteral("rightSidebarToggleVisible"));
    QVERIFY(leftSetting && leftSetting->isChecked());
    QVERIFY(rightSetting && rightSetting->isChecked());
    leftSetting->click();
    QVERIFY(left->isHidden());
    QVERIFY(!right->isHidden());
    rightSetting->click();
    leftSetting->click();
    QVERIFY(!left->isHidden());
    QVERIFY(right->isHidden());
    QVERIFY(workspace->isSidebarOpen(SidebarSide::Right));

    darkeye::MainWindow reopened(themes, {}, {}, paths);
    auto *reopenedLeft = reopened.findChild<QToolButton *>(leftName);
    auto *reopenedRight = reopened.findChild<QToolButton *>(rightName);
    QVERIFY(reopenedLeft && !reopenedLeft->isHidden());
    QVERIFY(reopenedRight && reopenedRight->isHidden());
}

void MainWindowSmokeTest::sidebarShortcutsAndAutoCollapseSetting()
{
    using darkeye::myads::SidebarSide;
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const darkeye::settings::Paths paths(QCoreApplication::applicationDirPath(), temp.path());
    QVERIFY(paths.ensureRuntimeDirectories());
    QVERIFY(saveEmptyWorkspace(paths));
    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    darkeye::MainWindow window(themes, {}, {}, paths);
    window.show();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());

    auto *workspace = window.findChild<darkeye::myads::WorkspaceWidget *>(QStringLiteral("mainWorkspace"));
    QVERIFY(workspace);
    QVERIFY(!workspace->isSidebarOpen(SidebarSide::Left));
    QVERIFY(!workspace->isSidebarOpen(SidebarSide::Right));
    const auto modifiers = Qt::ControlModifier | Qt::ShiftModifier;
    QTest::keyClick(window.windowHandle(), Qt::Key_1, modifiers);
    QTRY_VERIFY(workspace->isSidebarOpen(SidebarSide::Left));
    QTest::keyClick(window.windowHandle(), Qt::Key_2, modifiers);
    QTRY_VERIFY(workspace->isSidebarOpen(SidebarSide::Right));
    QVERIFY(workspace->isSidebarOpen(SidebarSide::Left));
    QTest::keyClick(window.windowHandle(), Qt::Key_2, modifiers);
    QTRY_VERIFY(!workspace->isSidebarOpen(SidebarSide::Right));

    QTest::keyClick(window.windowHandle(), Qt::Key_Comma, Qt::ControlModifier);
    auto *settings = window.findChild<darkeye::SettingsDialog *>();
    QVERIFY(settings && settings->isVisible());
    auto *settingsMenu = settings->findChild<QListWidget *>(QStringLiteral("DesignListWidget"));
    QVERIFY(settingsMenu);
    const auto sidebarSections = settingsMenu->findItems(QStringLiteral("边栏"), Qt::MatchExactly);
    QCOMPARE(sidebarSections.size(), 1);
    QTest::mouseClick(settingsMenu->viewport(), Qt::LeftButton, Qt::NoModifier,
                      settingsMenu->visualItemRect(sidebarSections.first()).center());
    auto *sidebarSettings = settings->findChild<darkeye::SidebarSettingsPage *>();
    QVERIFY(sidebarSettings && sidebarSettings->isVisible());
    auto *leftAutoCollapse =
        sidebarSettings->findChild<darkeye::ToggleSwitch *>(QStringLiteral("leftSidebarAutoCollapse"));
    auto *rightAutoCollapse =
        sidebarSettings->findChild<darkeye::ToggleSwitch *>(QStringLiteral("rightSidebarAutoCollapse"));
    QVERIFY(leftAutoCollapse && !leftAutoCollapse->isChecked());
    QVERIFY(rightAutoCollapse && !rightAutoCollapse->isChecked());
    leftAutoCollapse->click();
    QVERIFY(darkeye::settings::app(paths.settingsFile()).leftSidebarAutoCollapse);
    QVERIFY(!darkeye::settings::app(paths.settingsFile()).rightSidebarAutoCollapse);
    QTRY_VERIFY(!workspace->isSidebarOpen(SidebarSide::Left));
    settings->hide();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());

    QTest::keyClick(window.windowHandle(), Qt::Key_1, modifiers);
    QTRY_VERIFY(workspace->isSidebarOpen(SidebarSide::Left));
    workspace->rootPane()->focusTabBar();
    QTRY_VERIFY(!workspace->isSidebarOpen(SidebarSide::Left));
    QTest::keyClick(window.windowHandle(), Qt::Key_2, modifiers);
    QTRY_VERIFY(workspace->isSidebarOpen(SidebarSide::Right));
    workspace->rootPane()->focusTabBar();
    QCoreApplication::processEvents();
    QVERIFY(workspace->isSidebarOpen(SidebarSide::Right));

    const auto legacyFile = temp.filePath(QStringLiteral("legacy.ini"));
    QSettings legacy(legacyFile, QSettings::IniFormat);
    legacy.setValue(QStringLiteral("Workspace/SidebarAutoCollapse"), true);
    legacy.sync();
    auto migrated = darkeye::settings::app(legacyFile);
    QVERIFY(migrated.leftSidebarAutoCollapse);
    QVERIFY(migrated.rightSidebarAutoCollapse);
    migrated.rightSidebarAutoCollapse = false;
    darkeye::settings::saveApp(migrated, legacyFile);
    const auto saved = darkeye::settings::app(legacyFile);
    QVERIFY(saved.leftSidebarAutoCollapse);
    QVERIFY(!saved.rightSidebarAutoCollapse);
}

void MainWindowSmokeTest::paneTabShortcutsCanBeCustomized()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const darkeye::settings::Paths paths(QCoreApplication::applicationDirPath(), temp.path());
    QVERIFY(paths.ensureRuntimeDirectories());
    QVERIFY(saveEmptyWorkspace(paths));
    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    darkeye::MainWindow window(themes, {}, {}, paths);
    window.show();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());

#ifdef Q_OS_MACOS
    constexpr auto nextKey = Qt::Key_BracketRight;
    constexpr auto nextModifiers = Qt::ControlModifier | Qt::ShiftModifier;
    constexpr auto previousKey = Qt::Key_BracketLeft;
#else
    constexpr auto nextKey = Qt::Key_Tab;
    constexpr auto nextModifiers = Qt::ControlModifier;
    constexpr auto previousKey = Qt::Key_Tab;
#endif
    constexpr auto previousModifiers = Qt::ControlModifier | Qt::ShiftModifier;

    auto *workspace = window.findChild<darkeye::myads::WorkspaceWidget *>(QStringLiteral("mainWorkspace"));
    QVERIFY(workspace);
    auto *left = workspace->rootPane();
    auto *right = workspace->split(left, darkeye::myads::Placement::Right);
    QVERIFY(right);
    const auto first = QStringLiteral("first");
    const auto second = QStringLiteral("second");
    const auto third = QStringLiteral("third");
    const auto fourth = QStringLiteral("fourth");
    const auto fifth = QStringLiteral("fifth");
    auto *leftInput = new QLineEdit;
    auto *rightInput = new QLineEdit;
    QVERIFY(left->addContent(first, first, leftInput));
    QVERIFY(left->addContent(second, second, new QWidget));
    QVERIFY(left->addContent(third, third, new QWidget));
    QVERIFY(right->addContent(fourth, fourth, rightInput));
    QVERIFY(right->addContent(fifth, fifth, new QWidget));
    QVERIFY(right->setCurrentContentId(fourth));
    QVERIFY(left->setCurrentContentId(first));
    leftInput->setFocus();
    QCOMPARE(workspace->activePane(), left);
    QTest::keyClick(window.windowHandle(), nextKey, nextModifiers);
    QCOMPARE(left->currentContentId(), second);
    QTest::keyClick(window.windowHandle(), previousKey, previousModifiers);
    QCOMPARE(left->currentContentId(), first);
    QTest::keyClick(window.windowHandle(), previousKey, previousModifiers);
    QCOMPARE(left->currentContentId(), third);
    QCOMPARE(right->currentContentId(), fourth);
    rightInput->setFocus();
    QCOMPARE(workspace->activePane(), right);
    QTest::keyClick(window.windowHandle(), nextKey, nextModifiers);
    QCOMPARE(right->currentContentId(), fifth);
    QCOMPARE(left->currentContentId(), third);

    QTest::keyClick(window.windowHandle(), Qt::Key_Comma, Qt::ControlModifier);
    auto *settings = window.findChild<darkeye::SettingsDialog *>();
    QVERIFY(settings && settings->isVisible());
    auto *settingsSearch = settings->findChild<QLineEdit *>();
    QVERIFY(settingsSearch);
    settingsSearch->setText(QStringLiteral("下一个标签页"));
    QTest::keyClick(settingsSearch, Qt::Key_Return);
    auto *page = settings->findChild<darkeye::ShortcutSettingsPage *>();
    QVERIFY(page && page->isVisible());
    auto *nextRow = page->findChild<QWidget *>(QStringLiteral("ShortcutSettingRow_next_tab"));
    auto *previousRow = page->findChild<QWidget *>(QStringLiteral("ShortcutSettingRow_previous_tab"));
    QVERIFY(nextRow && previousRow);
    auto *nextEditor = nextRow->findChild<QKeySequenceEdit *>();
    auto *previousEditor = previousRow->findChild<QKeySequenceEdit *>();
    auto *reset = findSettingsButton(nextRow, QStringLiteral("恢复"));
    QVERIFY(nextEditor && previousEditor && reset);
    const QKeySequence nextDefault(nextModifiers | nextKey);
    QCOMPARE(nextEditor->keySequence(), nextDefault);
    QCOMPARE(previousEditor->keySequence(), QKeySequence(previousModifiers | previousKey));

    nextEditor->setFocus();
    QTest::keyClick(nextEditor, Qt::Key_F8, Qt::ControlModifier);
    QTRY_VERIFY(QFile::exists(paths.shortcutsFile()));
    QFile shortcutFile(paths.shortcutsFile());
    QVERIFY(shortcutFile.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(shortcutFile.readAll()).object().value(QStringLiteral("next_tab")).toString(),
             QStringLiteral("Ctrl+F8"));
    shortcutFile.close();
    auto *nextAction = window.findChild<QAction *>(QStringLiteral("next_tab"));
    QVERIFY(nextAction);
    const auto customNext = QKeySequence(Qt::CTRL | Qt::Key_F8);
    QCOMPARE(nextAction->shortcut(), customNext);
    settings->hide();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    workspace->setActivePane(right);
    QTest::keyClick(window.windowHandle(), Qt::Key_F8, Qt::ControlModifier);
    QCOMPARE(right->currentContentId(), fourth);
    reset->click();
    QCOMPARE(nextEditor->keySequence(), nextDefault);
    QCOMPARE(nextAction->shortcut(), nextDefault);
    QTest::keyClick(window.windowHandle(), Qt::Key_F8, Qt::ControlModifier);
    QCOMPARE(right->currentContentId(), fourth);
    QTest::keyClick(window.windowHandle(), nextKey, nextModifiers);
    QCOMPARE(right->currentContentId(), fifth);
    QCOMPARE(left->currentContentId(), third);
}

void MainWindowSmokeTest::paneShortcutsFollowLayoutOrder()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const darkeye::settings::Paths paths(QCoreApplication::applicationDirPath(), temp.path());
    QVERIFY(paths.ensureRuntimeDirectories());
    QVERIFY(saveEmptyWorkspace(paths));
    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    darkeye::MainWindow window(themes, {}, {}, paths);
    window.show();
    window.activateWindow();
    QTRY_VERIFY(window.isActiveWindow());
    auto *workspace = window.findChild<darkeye::myads::WorkspaceWidget *>(QStringLiteral("mainWorkspace"));
    QVERIFY(workspace);
    auto *left = workspace->rootPane();
    auto *input = new QLineEdit;
    QVERIFY(left->addContent(QStringLiteral("left"), QStringLiteral("Left"), input));
    constexpr auto modifiers = Qt::ControlModifier | Qt::AltModifier;
    QTest::keyClick(window.windowHandle(), Qt::Key_Right, modifiers);
    QCOMPARE(workspace->activePane(), left);

    auto *bottom = workspace->split(left, darkeye::myads::Placement::Bottom);
    QVERIFY(bottom);
    auto *top = workspace->split(left, darkeye::myads::Placement::Top);
    QVERIFY(top);
    QVERIFY(top->addContent(QStringLiteral("top"), QStringLiteral("Top"), new QWidget));
    QVERIFY(bottom->addContent(QStringLiteral("bottom"), QStringLiteral("Bottom"), new QWidget));
    QTRY_VERIFY(input->isVisible());
    input->setFocus();
    QTRY_VERIFY(input->hasFocus());
    QCOMPARE(workspace->activePane(), left);

    QTest::keyClick(window.windowHandle(), Qt::Key_Right, modifiers);
    QCOMPARE(workspace->activePane(), bottom);
    QCOMPARE(QApplication::focusWidget(), bottom->findChild<QTabBar *>());
    QTest::keyClick(window.windowHandle(), Qt::Key_Right, modifiers);
    QCOMPARE(workspace->activePane(), top);
    QTest::keyClick(window.windowHandle(), Qt::Key_Left, modifiers);
    QCOMPARE(workspace->activePane(), bottom);
    QTest::keyClick(window.windowHandle(), Qt::Key_Left, modifiers);
    QCOMPARE(workspace->activePane(), left);
    QCOMPARE(left->contentWidget(left->currentContentId()), input);

    QTest::keyClick(window.windowHandle(), Qt::Key_Comma, Qt::ControlModifier);
    auto *settings = window.findChild<darkeye::SettingsDialog *>();
    QVERIFY(settings && settings->isVisible());
    auto *search = settings->findChild<QLineEdit *>();
    QVERIFY(search);
    search->setText(QStringLiteral("下一个窗格"));
    QTest::keyClick(search, Qt::Key_Return);
    auto *page = settings->findChild<darkeye::ShortcutSettingsPage *>();
    QVERIFY(page && page->isVisible());
    auto *next = page->findChild<QWidget *>(QStringLiteral("ShortcutSettingRow_next_pane"));
    auto *previous = page->findChild<QWidget *>(QStringLiteral("ShortcutSettingRow_previous_pane"));
    QVERIFY(next && previous);
}

void MainWindowSmokeTest::restoredTabsDeferInitialization()
{
    QTemporaryDir directory;
    darkeye::settings::Paths paths(directory.path(), directory.filePath(QStringLiteral("data")));
    QVERIFY(paths.ensureRuntimeDirectories());
    darkeye::SqliteConnection database, privateDatabase;
    QString error;
    QVERIFY(database.open(paths.publicDatabase(), false, &error));
    QVERIFY(privateDatabase.open(paths.privateDatabase(), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(database, darkeye::DatabaseKind::Public, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(privateDatabase, darkeye::DatabaseKind::Private, &error));

    darkeye::PersonRepository people(database.database());
    const auto actress = people.create(darkeye::PersonKind::Actress, QStringLiteral("Test Actress"), {});
    const auto actor = people.create(darkeye::PersonKind::Actor, QStringLiteral("Test Actor"), {});
    QVERIFY(actress && actor);
    const auto actressRoute = QStringLiteral("modify_actress/%1").arg(*actress);
    const auto actorRoute = QStringLiteral("modify_actor/%1").arg(*actor);
    const auto searchRoute = QStringLiteral("search/missing");
    const auto activeRoute = QStringLiteral("inbox");
    const QStringList lazyRoutes{
        QStringLiteral("manage_tags"), QStringLiteral("manage_makers"), QStringLiteral("manage_labels"),
        QStringLiteral("manage_series"), QStringLiteral("manage_summary"), QStringLiteral("manage_records"),
        QStringLiteral("manage_deleted"), QStringLiteral("manage_trash"), QStringLiteral("timeline"),
        actressRoute, actorRoute,
    };
    {
        darkeye::myads::WorkspaceWidget saved;
        auto *pane = saved.rootPane();
        for (const auto &route : lazyRoutes + QStringList{searchRoute, activeRoute})
            QVERIFY(saved.fillPane(pane, darkeye::myads::ContentConfig(route)));
        QVERIFY(saved.saveLayout(QDir(paths.dataDirectory()).filePath(QStringLiteral("workspace-v2.json")),
                                 [](const auto *, const QString &id) {
                                     return QJsonObject{{QStringLiteral("content_id"), id}};
                                 }, {}, &error));
    }

    darkeye::ThemeService themes(*qApp);
    darkeye::MainWindow window(themes, database.database(), privateDatabase.database(), paths);
    window.show();
    auto *workspace = window.findChild<darkeye::myads::WorkspaceWidget *>(QStringLiteral("mainWorkspace"));
    QVERIFY(workspace);
    auto *pane = workspace->findPaneByContentId(activeRoute);
    QVERIFY(pane);
    QCOMPARE(pane->currentContentId(), activeRoute);
    auto *search = pane->contentWidget(searchRoute);
    QVERIFY(search);
    QVERIFY(!search->findChild<QListWidget *>());
    for (const auto &route : lazyRoutes) {
        auto *page = dynamic_cast<darkeye::LazyWidget *>(pane->contentWidget(route));
        QVERIFY2(page, qPrintable(route));
        QVERIFY2(!page->isInitialized(), qPrintable(route));
    }
    for (const auto &route : lazyRoutes) {
        QVERIFY(pane->setCurrentContentId(route));
        auto *page = dynamic_cast<darkeye::LazyWidget *>(pane->contentWidget(route));
        QVERIFY2(page->isInitialized(), qPrintable(route));
    }
    QCOMPARE(pane->contentTitle(actressRoute), QStringLiteral("编辑 · Test Actress"));
    QCOMPARE(qobject_cast<darkeye::PersonEditorPage *>(pane->contentWidget(actressRoute))->personId(), *actress);
    QCOMPARE(qobject_cast<darkeye::PersonEditorPage *>(pane->contentWidget(actorRoute))->personId(), *actor);
    QVERIFY(pane->setCurrentContentId(searchRoute));
    const auto *results = search->findChild<QListWidget *>();
    QVERIFY(results);
    QCOMPARE(results->count(), 1);
    QCOMPARE(results->item(0)->text(), QStringLiteral("没有匹配结果"));
}

void MainWindowSmokeTest::workspaceSearchAndSessionLifecycle()
{
    QTemporaryDir directory;
    darkeye::settings::Paths paths(directory.path(), directory.filePath(QStringLiteral("data")));
    QVERIFY(paths.ensureRuntimeDirectories());
    QVERIFY(saveEmptyWorkspace(paths));
    darkeye::SqliteConnection database, privateDatabase;
    QString error;
    QVERIFY(database.open(paths.publicDatabase(), false, &error));
    QVERIFY(privateDatabase.open(paths.privateDatabase(), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(database, darkeye::DatabaseKind::Public, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(privateDatabase, darkeye::DatabaseKind::Private, &error));
    darkeye::PersonRepository people(database.database());
    const auto first = people.create(darkeye::PersonKind::Actress, QStringLiteral("测试人物甲"), QStringLiteral("Test A"), &error);
    const auto second = people.create(darkeye::PersonKind::Actress, QStringLiteral("测试人物乙"), QStringLiteral("Test B"), &error);
    QVERIFY(first && second);
    const auto actor = people.create(darkeye::PersonKind::Actor, QStringLiteral("测试男优"), QStringLiteral("Test Actor"), &error);
    const auto similarActor = people.create(darkeye::PersonKind::Actor, QStringLiteral("测试男优二"), QStringLiteral("Other Actor"), &error);
    QVERIFY2(actor && similarActor, qPrintable(error));
    const auto legacyActorRoute = QStringLiteral("actor_detail/%1").arg(*actor);
    darkeye::WorkRepository works(database.database());
    darkeye::ReferenceRepository references(database.database());
    const auto maker = references.create(darkeye::ReferenceKind::Maker, QStringLiteral("测试片商"), &error);
    const auto label = references.create(darkeye::ReferenceKind::Label, QStringLiteral("测试厂牌"), &error);
    const auto series = references.create(darkeye::ReferenceKind::Series, QStringLiteral("测试系列"), &error);
    QVERIFY2(maker && label && series, qPrintable(error));
    darkeye::Work work; work.serialNumber = QStringLiteral("SEARCH-001"); work.chineseTitle = QStringLiteral("测试作品");
    work.makerId = maker;
    work.seriesId = series;
    const auto workId = works.insertComplete(work, {*first}, {*actor}, {}, &error);
    QVERIFY(workId);
    const auto firstRoute = QStringLiteral("actress_detail/") + QString::number(*first);
    const auto secondRoute = QStringLiteral("actress_detail/") + QString::number(*second);
    const auto workRoute = QStringLiteral("work_detail/") + QString::number(*workId);
    const auto shelfRoute = QStringLiteral("shelf");
    darkeye::Work otherWork;
    otherWork.serialNumber = QStringLiteral("OTHER-002");
    otherWork.labelId = label;
    otherWork.director = QStringLiteral("测试导演/甲");
    const auto otherWorkId = works.insertComplete(otherWork, {}, {*similarActor}, {}, &error);
    QVERIFY2(otherWorkId, qPrintable(error));
    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    QVERIFY(themes.setTheme(darkeye::ThemeId::Light));
    {
        darkeye::MainWindow window(themes, database.database(), privateDatabase.database(), paths);
        window.resize(1200, 800);
        window.show();
        window.raise();
        window.activateWindow();
        QTRY_VERIFY(window.isActiveWindow());
        auto *search = window.findChild<darkeye::WorkspaceSearch *>();
        auto *input = search->findChild<QLineEdit *>(QStringLiteral("workspaceSearchInput"));
        auto *results = search->findChild<QListWidget *>();
        auto *workspace = window.findChild<darkeye::myads::WorkspaceWidget *>(QStringLiteral("mainWorkspace"));
        QVERIFY(input && results && workspace);
        QVERIFY(!search->isVisible());
        auto *startupPane = workspace->rootPane();
        for (auto *button : workspace->findChildren<QPushButton *>())
            QVERIFY(!button->isVisible());
        auto *emptyHint = window.findChild<QLabel *>(QStringLiteral("workspaceEmptyHint"));
        QVERIFY(emptyHint && emptyHint->isVisible());
        const auto openSearch = [&](Qt::Key key = Qt::Key_O) {
            QCoreApplication::processEvents();
            QTest::keyClick(window.windowHandle(), key, Qt::ControlModifier);
        };
        openSearch();
        QTRY_VERIFY(search->isVisible() && input->hasFocus());
        QVERIFY(results->isHidden());
        QCOMPARE(window.windowHandle()->focusObject(), input);
        input->setText(QStringLiteral("统计"));
        QTRY_VERIFY(results->count() == 1 && results->item(0)->data(Qt::UserRole).toString().startsWith(QStringLiteral("search/")));
        QTest::keyClick(input, Qt::Key_P, Qt::ControlModifier);
        QTRY_VERIFY(results->isVisible() && results->count() > 9);
        QVERIFY(input->text().isEmpty());
        QCOMPARE(startupPane->contentCount(), 0);
        const auto unopenedRoute = QStringLiteral("manage_tags");
        QVERIFY(!workspace->findPaneByContentId(unopenedRoute));
        bool listsUnopenedPage = false;
        for (int row = 0; row < results->count(); ++row) {
            const auto *item = results->item(row);
            if (item->data(Qt::UserRole).toString() == unopenedRoute)
                listsUnopenedPage = item->text() == QStringLiteral("页面 · 标签管理");
        }
        QVERIFY(listsUnopenedPage);
        for (int row = 1; row < results->count(); ++row)
            QTest::keyClick(input, Qt::Key_Down);
        QCOMPARE(results->currentRow(), results->count() - 1);
        QTRY_VERIFY(results->viewport()->rect().intersects(results->visualItemRect(results->currentItem())));
        openSearch();
        QVERIFY(results->isHidden());
        QTest::mouseClick(startupPane, Qt::LeftButton, Qt::NoModifier,
                          startupPane->rect().bottomLeft() + QPoint(5, -5));
        QTRY_VERIFY(!search->isVisible());
        openSearch();
        QTRY_VERIFY(search->isVisible() && input->hasFocus());
        startupPane->setFocusPolicy(Qt::ClickFocus);
        startupPane->setFocus();
        QTRY_VERIFY(!search->isVisible());
        openSearch();
        QTRY_VERIFY(search->isVisible());
        QTest::keyClick(input, Qt::Key_Escape);
        QTRY_VERIFY(!search->isVisible());

        openSearch();
        input->setText(QStringLiteral("统计"));
        QTest::keyClick(input, Qt::Key_Return);
        const auto fullResultsRoute = startupPane->currentContentId();
        QVERIFY(fullResultsRoute.startsWith(QStringLiteral("search/")));
        auto *fullResultsPage = startupPane->contentWidget(fullResultsRoute);
        QVERIFY(fullResultsPage);
        QVERIFY(!fullResultsPage->findChild<QLineEdit *>(QStringLiteral("workspaceSearchInput")));
        QVERIFY(search->findChildren<QPushButton *>().isEmpty());
        const auto *fullResults = fullResultsPage->findChild<QListWidget *>();
        QVERIFY(fullResults);
        QCOMPARE(fullResults->count(), 1);
        QVERIFY(fullResults->item(0)->data(Qt::UserRole).toString().isEmpty());
        QVERIFY(startupPane->removeContent(fullResultsRoute));

        const auto previousRoute = workspace->activePane()->currentContentId();
        QTest::keyClick(&window, Qt::Key_Comma, Qt::ControlModifier);
        QTRY_VERIFY(window.findChild<darkeye::SettingsDialog *>());
        auto *settings = window.findChild<darkeye::SettingsDialog *>();
        QVERIFY(settings->isVisible());
        QCOMPARE(workspace->activePane()->currentContentId(), previousRoute);
        QVERIFY(!workspace->findPaneByContentId(QStringLiteral("setting")));
        QVERIFY(!search->isVisible());
        settings->close();
        window.activateWindow();

        openSearch(Qt::Key_P);
        QTRY_VERIFY(search->isVisible());
        QInputMethodEvent preedit(QStringLiteral("zuopin"), {});
        QCoreApplication::sendEvent(window.windowHandle()->focusObject(), &preedit);
        QVERIFY(search->isVisible() && input->hasFocus());
        QVERIFY(input->text().isEmpty());
        QInputMethodEvent commit;
        commit.setCommitString(QStringLiteral("作品"));
        QCoreApplication::sendEvent(window.windowHandle()->focusObject(), &commit);
        QCOMPARE(input->text(), QStringLiteral("作品"));
        QTest::keyClick(input, Qt::Key_Return);
        auto *worksPane = workspace->findPaneByContentId(QStringLiteral("mutiwork"));
        QVERIFY(worksPane);
        QCOMPARE(worksPane->contentTitle(QStringLiteral("mutiwork")), QStringLiteral("作品库"));
        window.findChild<darkeye::WorkFilter *>()->findChild<darkeye::WorkTagSelector *>()->clearSelection();
        QVERIFY(!emptyHint->isVisible());
        QVERIFY(!search->isVisible());
        openSearch(Qt::Key_P);
        QTRY_VERIFY(search->isVisible());
        input->setText(QStringLiteral("商店"));
        QCOMPARE(results->item(0)->text(), QStringLiteral("页面 · 作品展厅"));
        QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), QStringLiteral("home"));
        input->setText(QStringLiteral("软删除"));
        QCOMPARE(results->item(0)->text(), QStringLiteral("页面 · 批量移入回收站"));
        QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), QStringLiteral("manage_deleted"));
        input->setText(QStringLiteral("女优"));
        QTRY_VERIFY(results->isVisible() && results->count() > 0
                    && results->item(0)->data(Qt::UserRole).toString() == QStringLiteral("actress"));
        QTest::mouseClick(results->viewport(), Qt::LeftButton, Qt::NoModifier,
                          results->visualItemRect(results->item(0)).center());
        QVERIFY(workspace->findPaneByContentId(QStringLiteral("actress")));
        QVERIFY(!search->isVisible());
        openSearch();
        QTRY_VERIFY(search->isVisible());
        input->setText(QStringLiteral("测试人物甲"));
        QTest::keyClick(input, Qt::Key_Return);
        auto *pane = workspace->findPaneByContentId(firstRoute);
        QVERIFY(pane);
        auto *firstPage = qobject_cast<darkeye::PersonDetailPage *>(pane->contentWidget(firstRoute));
        QVERIFY(firstPage);
        search->openRequested(secondRoute);
        auto *secondPage = qobject_cast<darkeye::PersonDetailPage *>(pane->contentWidget(secondRoute));
        QVERIFY(secondPage && firstPage != secondPage);
        QCOMPARE(firstPage->currentPersonId(), *first);
        QCOMPARE(secondPage->currentPersonId(), *second);
        const int count = pane->contentCount();
        search->openRequested(firstRoute);
        QCOMPARE(pane->contentCount(), count + 1);
        QVERIFY(pane->currentContentId().startsWith(firstRoute + u'/'));
        QVERIFY(pane->removeContent(pane->currentContentId()));
        auto *filter = window.findChild<darkeye::WorkFilter *>();
        QVERIFY(filter);
        auto *serial = findWorkControl<QLineEdit>(filter, QStringLiteral("WorkSerialFilter"));
        auto *scope = findWorkControl<QComboBox>(filter, QStringLiteral("WorkScopeSelector"));
        auto *sort = findWorkControl<QComboBox>(filter, QStringLiteral("WorkSortSelector"));
        QVERIFY(serial && scope && sort);
        serial->setText(otherWork.serialNumber);
        scope->setCurrentIndex(scope->findData(static_cast<int>(darkeye::WorkFilter::Scope::Favorites)));
        sort->setCurrentIndex(sort->findData(static_cast<int>(darkeye::WorkSortOrder::SerialAscending)));
        openSearch();
        QTRY_VERIFY(search->isVisible());
        QTest::keyClicks(input, QStringLiteral("search-001"));
        QCOMPARE(input->text(), QStringLiteral("search-001"));
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(serial->text().isEmpty());
        QCOMPARE(scope->currentData().toInt(), static_cast<int>(darkeye::WorkFilter::Scope::Public));
        QCOMPARE(sort->currentData().toInt(), static_cast<int>(darkeye::WorkSortOrder::Random));
        QVERIFY(!workspace->findPaneByContentId(workRoute));
        QCOMPARE(workspace->findPaneByContentId(shelfRoute), pane);
        auto *shelfPage = qobject_cast<darkeye::ShelfPage *>(pane->contentWidget(shelfRoute));
        QVERIFY(shelfPage);
        auto *shelfView = shelfPage->findChild<darkeye::DvdShelfView *>();
        QVERIFY(shelfView);
        QTRY_COMPARE(shelfView->expandedWorkCode(), work.serialNumber);
        QVERIFY(workspace->moveContent(pane->paneId(), shelfRoute, pane->paneId(), darkeye::myads::DropZone::Right));
        QCOMPARE(workspace->panes().size(), 2);
        auto *workPane = workspace->findPaneByContentId(shelfRoute);
        QVERIFY(workPane && workPane != pane);
        workspace->setActivePane(pane);
        QVERIFY(pane->setCurrentContentId(firstRoute));
        QCOMPARE(workspace->activePane(), pane);
        serial->setText(otherWork.serialNumber);
        sort->setCurrentIndex(sort->findData(static_cast<int>(darkeye::WorkSortOrder::SerialDescending)));
        openSearch();
        QTRY_VERIFY(search->isVisible());
        input->setText(otherWork.serialNumber);
        QTest::keyClick(input, Qt::Key_Return);
        QCOMPARE(workspace->activePane(), workPane);
        QCOMPARE(workPane->contentWidget(shelfRoute), shelfPage);
        QCOMPARE(serial->text(), otherWork.serialNumber);
        QCOMPARE(sort->currentData().toInt(), static_cast<int>(darkeye::WorkSortOrder::SerialDescending));
        QCOMPARE(shelfPage->filteredWorkIds(), QSet<qint64>{*otherWorkId});
        QTRY_COMPARE(shelfView->expandedWorkCode(), otherWork.serialNumber);

        const QList<QPair<QString, qint64>> shelfSearches{
            {QStringLiteral("片商 · 测试片商"), *workId},
            {QStringLiteral("厂牌 · 测试厂牌"), *otherWorkId},
            {QStringLiteral("系列 · 测试系列"), *workId},
            {QStringLiteral("男演员 · 测试男优"), *workId},
            {QStringLiteral("导演 · 测试导演/甲"), *otherWorkId},
        };
        for (const auto &[resultText, expectedWorkId] : shelfSearches) {
            workspace->setActivePane(pane);
            QVERIFY(pane->setCurrentContentId(firstRoute));
            openSearch();
            QTRY_VERIFY(search->isVisible());
            input->setText(resultText.section(QStringLiteral(" · "), 1));
            QTRY_COMPARE(results->findItems(resultText, Qt::MatchExactly).size(), 1);
            results->setCurrentItem(results->findItems(resultText, Qt::MatchExactly).first());
            const auto filterRoute = results->currentItem()->data(Qt::UserRole).toString();
            QTest::keyClick(input, Qt::Key_Return);
            QCOMPARE(workspace->activePane(), workPane);
            QCOMPARE(workPane->currentContentId(), shelfRoute);
            QCOMPARE(workPane->contentWidget(shelfRoute), shelfPage);
            QVERIFY(!workspace->findPaneByContentId(filterRoute));
            QCOMPARE(shelfPage->filteredWorkIds(), QSet<qint64>{expectedWorkId});
        }
        auto *clearFilter = findWorkControl<QPushButton>(filter, QStringLiteral("WorkClearButton"));
        QVERIFY(clearFilter);
        clearFilter->click();
        // Reopen before deleteLater is processed: the old object must never be reused.
        QPointer<QWidget> oldPage = shelfPage;
        QVERIFY(workPane->removeContent(shelfRoute));
        search->openRequested(workRoute);
        auto *source = workspace->findPaneByContentId(shelfRoute);
        QVERIFY(source);
        QVERIFY(source->contentWidget(shelfRoute) != oldPage.data());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(oldPage.isNull());
        // Recreate a split and persist its order, contents and active tab.
        QVERIFY(workspace->moveContent(source->paneId(), shelfRoute, source->paneId(), darkeye::myads::DropZone::Right));
        const auto editorRoute = QStringLiteral("modify_actress/") + QString::number(*first);
        search->openRequested(editorRoute);
        auto *editorPane = workspace->findPaneByContentId(editorRoute);
        auto *editor = editorPane->contentWidget(editorRoute);
        QLineEdit *birthday = nullptr;
        for (auto *edit : editor->findChildren<QLineEdit *>())
            if (edit->property("testId").toString() == QStringLiteral("PersonBirthdayInput")) birthday = edit;
        QVERIFY(birthday);
        birthday->setText(QStringLiteral("1990-01-01"));
        auto answer = [](QMessageBox::StandardButton choice) {
            QTimer::singleShot(0, [choice] {
                auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                if (dialog) dialog->button(choice)->click();
            });
        };
        answer(QMessageBox::Cancel);
        QVERIFY(!editorPane->removeContent(editorRoute));
        QVERIFY(workspace->findPaneByContentId(editorRoute));
        answer(QMessageBox::Save);
        QVERIFY(editorPane->removeContent(editorRoute));
        QCOMPARE(people.findDetails(darkeye::PersonKind::Actress, *first)->birthday, QStringLiteral("1990-01-01"));
        search->openRequested(workRoute);
        const auto preview = qEnvironmentVariable("DARKEYE_WORKSPACE_PREVIEW");
        if (!preview.isEmpty()) { QTest::qWait(100); QVERIFY(window.grab().save(preview)); }
        search->openRequested(QStringLiteral("work_filters"));
        QVERIFY(workspace->findPaneByContentId(QStringLiteral("work_filters")));
        search->openRequested(workRoute);
        // Simulate a saved layout containing a pane dedicated to the old person graph.
        auto *legacyPane = workspace->split(workspace->activePane(), darkeye::myads::Placement::Right);
        QVERIFY(legacyPane);
        const auto legacyRoute = QStringLiteral("person_graph/%1").arg(*first);
        QVERIFY(workspace->fillPane(legacyPane, darkeye::myads::ContentConfig(legacyRoute)
                                                   .setWidget(new QWidget(&window))));
        QVERIFY(workspace->fillPane(legacyPane, darkeye::myads::ContentConfig(legacyActorRoute)
                                                   .setWidget(new QWidget(&window))));
        QVERIFY(workspace->fillPane(legacyPane, darkeye::myads::ContentConfig(QStringLiteral("manage_batch"))
                                                   .setWidget(new QWidget(&window))));
        QVERIFY(workspace->fillPane(legacyPane, darkeye::myads::ContentConfig(QStringLiteral("setting"))
                                                   .setWidget(new QWidget(&window))));
        workspace->setActivePane(legacyPane);
        legacyPane->setCurrentContentId(QStringLiteral("setting"));
        QCOMPARE(workspace->panes().size(), 3);
        QVERIFY(window.close());
    }
    {
        darkeye::MainWindow window(themes, database.database(), privateDatabase.database(), paths);
        auto *workspace = window.findChild<darkeye::myads::WorkspaceWidget *>(QStringLiteral("mainWorkspace"));
        QCOMPARE(workspace->panes().size(), 2);
        QVERIFY(workspace->findPaneByContentId(firstRoute));
        QVERIFY(workspace->findPaneByContentId(secondRoute));
        QVERIFY(workspace->findPaneByContentId(shelfRoute));
        QVERIFY(workspace->findPaneByContentId(QStringLiteral("work_filters")));
        QVERIFY(!workspace->findPaneByContentId(workRoute));
        QVERIFY(!workspace->findPaneByContentId(legacyActorRoute));
        QVERIFY(!workspace->findPaneByContentId(QStringLiteral("manage_batch")));
        QVERIFY(!workspace->findPaneByContentId(QStringLiteral("setting")));
        QVERIFY(!window.findChild<darkeye::SettingsDialog *>());
        QCOMPARE(qobject_cast<darkeye::PersonDetailPage *>(workspace->findPaneByContentId(firstRoute)->contentWidget(firstRoute))->currentPersonId(), *first);
        window.show();
        window.activateWindow();
        QTRY_VERIFY(window.isActiveWindow());
        QTest::keyClick(window.windowHandle(), Qt::Key_O, Qt::ControlModifier);
        auto *search = window.findChild<darkeye::WorkspaceSearch *>();
        QTRY_VERIFY(search->isVisible());
        auto *recent = search->findChild<QListWidget *>();
        QVERIFY(recent->count() > 1);
        QCOMPARE(recent->item(0)->data(Qt::UserRole).toString(), workRoute);
        QCOMPARE(recent->item(0)->text(), QStringLiteral("SEARCH-001"));
        QTest::keyClick(search->findChild<QLineEdit *>(), Qt::Key_Escape);

        // Closing the last tab returns to the empty workspace.
        for (auto *pane : workspace->panes()) {
            for (const auto &id : pane->contentIds()) pane->removeContent(id);
        }
        QCoreApplication::processEvents();
        QCOMPARE(workspace->panes().size(), 1);
        QCOMPARE(workspace->rootPane()->contentCount(), 0);
        QVERIFY(workspace->rootPane()->currentContentId().isEmpty());
        QVERIFY(window.findChild<QLabel *>(QStringLiteral("workspaceEmptyHint"))->isVisible());
        QTest::keyClick(window.windowHandle(), Qt::Key_P, Qt::ControlModifier);
        QTRY_VERIFY(search->isVisible());
        auto *input = search->findChild<QLineEdit *>();
        input->setText(QStringLiteral("通知"));
        QTest::keyClick(input, Qt::Key_Return);
        auto *sidebar = workspace->sidebarPane(darkeye::myads::SidebarSide::Right);
        QCOMPARE(sidebar->currentContentId(), QStringLiteral("inbox"));
        QCOMPARE(sidebar->contentTitle(QStringLiteral("inbox")), QStringLiteral("采集任务"));
    }
}

void MainWindowSmokeTest::actressDetailOpenModes()
{
    QTemporaryDir directory;
    darkeye::settings::Paths paths(directory.path(), directory.filePath(QStringLiteral("data")));
    QVERIFY(paths.ensureRuntimeDirectories());
    QVERIFY(saveEmptyWorkspace(paths));
    darkeye::SqliteConnection database, privateDatabase;
    QString error;
    QVERIFY(database.open(paths.publicDatabase(), false, &error));
    QVERIFY(privateDatabase.open(paths.privateDatabase(), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(database, darkeye::DatabaseKind::Public, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(privateDatabase, darkeye::DatabaseKind::Private, &error));
    darkeye::PersonRepository people(database.database());
    const auto first = people.create(darkeye::PersonKind::Actress, QStringLiteral("甲"), QStringLiteral("First"), &error);
    const auto second = people.create(darkeye::PersonKind::Actress, QStringLiteral("乙"), QStringLiteral("Second"), &error);
    QVERIFY2(first && second, qPrintable(error));
    const auto firstRoute = QStringLiteral("actress_detail/%1").arg(*first);
    const auto secondRoute = QStringLiteral("actress_detail/%1").arg(*second);
    const auto switchRoute = QStringLiteral("actress_detail/switch");
    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    QString duplicateRoute;
    {
        darkeye::MainWindow window(themes, database.database(), privateDatabase.database(), paths);
        auto *workspace = window.findChild<darkeye::myads::WorkspaceWidget *>(QStringLiteral("mainWorkspace"));
        auto *search = window.findChild<darkeye::WorkspaceSearch *>();
        search->openRequested(firstRoute);
        auto *root = workspace->rootPane();
        auto *independent = qobject_cast<darkeye::PersonDetailPage *>(root->contentWidget(firstRoute));
        QVERIFY(independent);
        QCOMPARE(root->contentTitle(firstRoute), QStringLiteral("甲"));
        window.show();
        window.raise();
        window.activateWindow();
        QTRY_VERIFY(window.isActiveWindow());
        auto *input = search->findChild<QLineEdit *>(QStringLiteral("workspaceSearchInput"));
        auto *results = search->findChild<QListWidget *>(QStringLiteral("workspaceSearchResults"));
        QVERIFY(input && results);
        QTest::keyClick(window.windowHandle(), Qt::Key_P, Qt::ControlModifier);
        QTRY_VERIFY(search->isVisible());
        input->setText(QStringLiteral("女演员详情"));
        QVERIFY(results->currentItem());
        QCOMPARE(results->currentItem()->text(), QStringLiteral("页面 · 女演员详情（跟随切换）"));
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(!search->isVisible());
        auto *detail = qobject_cast<darkeye::PersonDetailPage *>(root->contentWidget(switchRoute));
        QVERIFY(detail && detail != independent);
        QCOMPARE(detail->currentPersonId(), 0);
        QCOMPARE(root->contentTitle(switchRoute), QStringLiteral("女演员详情（跟随切换）"));
        search->openRequested(firstRoute);
        QCOMPARE(detail->currentPersonId(), *first);
        QVERIFY(workspace->moveContent(root->paneId(), switchRoute, root->paneId(), darkeye::myads::DropZone::Right));
        auto *switchPane = workspace->findPaneByContentId(switchRoute);
        QVERIFY(switchPane && switchPane != root);
        workspace->setActivePane(root);
        QTest::keyClick(window.windowHandle(), Qt::Key_O, Qt::ControlModifier);
        QTRY_VERIFY(search->isVisible());
        input->setText(QStringLiteral("乙"));
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(!search->isVisible());
        QCOMPARE(workspace->activePane(), switchPane);
        QCOMPARE(switchPane->contentWidget(switchRoute), detail);
        QCOMPARE(detail->currentPersonId(), *second);
        QCOMPARE(independent->currentPersonId(), *first);
        QCOMPARE(switchPane->contentTitle(switchRoute), QStringLiteral("女演员详情（跟随切换）"));
        QVERIFY(!workspace->findPaneByContentId(secondRoute));

        workspace->setActivePane(root);
        QTest::keyClick(window.windowHandle(), Qt::Key_P, Qt::ControlModifier);
        QTRY_VERIFY(search->isVisible());
        input->setText(QStringLiteral("女演员详情"));
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(!search->isVisible());
        QCOMPARE(workspace->activePane(), switchPane);
        QCOMPARE(switchPane->contentWidget(switchRoute), detail);
        QCOMPARE(detail->currentPersonId(), *second);

        QTest::keyClick(window.windowHandle(), Qt::Key_P, Qt::ControlModifier);
        QTRY_VERIFY(search->isVisible());
        input->setText(QStringLiteral("独立页"));
        QVERIFY(results->currentItem());
        QCOMPARE(results->currentItem()->text(), QStringLiteral("页面 · 女演员详情（独立页）"));
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(search->isVisible());
        QVERIFY(input->hasFocus());
        QVERIFY(input->text().isEmpty());
        input->setText(QStringLiteral("甲"));
        QTRY_COMPARE(results->count(), 1);
        QCOMPARE(results->currentItem()->data(Qt::UserRole).toString(), firstRoute);
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(!search->isVisible());
        duplicateRoute = switchPane->currentContentId();
        QVERIFY(duplicateRoute.startsWith(firstRoute + u'/'));
        auto *duplicate = qobject_cast<darkeye::PersonDetailPage *>(switchPane->contentWidget(duplicateRoute));
        QVERIFY(duplicate && duplicate != independent);
        QCOMPARE(switchPane->contentTitle(duplicateRoute), QStringLiteral("甲"));
        QCOMPARE(duplicate->currentPersonId(), *first);
        QCOMPARE(detail->currentPersonId(), *second);
        QVERIFY(switchPane->setCurrentContentId(switchRoute));
        QVERIFY(window.close());
    }
    {
        darkeye::MainWindow window(themes, database.database(), privateDatabase.database(), paths);
        auto *workspace = window.findChild<darkeye::myads::WorkspaceWidget *>(QStringLiteral("mainWorkspace"));
        auto *switchPane = workspace->findPaneByContentId(switchRoute);
        QVERIFY(switchPane);
        QCOMPARE(workspace->activePane(), switchPane);
        QCOMPARE(switchPane->currentContentId(), switchRoute);
        auto *detail = qobject_cast<darkeye::PersonDetailPage *>(switchPane->contentWidget(switchRoute));
        QVERIFY(detail);
        QCOMPARE(detail->currentPersonId(), *second);
        QVERIFY(workspace->findPaneByContentId(firstRoute));
        QVERIFY(workspace->findPaneByContentId(duplicateRoute));
        QVERIFY(switchPane->removeContent(switchRoute));
        auto *search = window.findChild<darkeye::WorkspaceSearch *>();
        window.show();
        window.raise();
        window.activateWindow();
        QTRY_VERIFY(window.isActiveWindow());
        QTest::keyClick(window.windowHandle(), Qt::Key_O, Qt::ControlModifier);
        QTRY_VERIFY(search->isVisible());
        auto *input = search->findChild<QLineEdit *>(QStringLiteral("workspaceSearchInput"));
        QVERIFY(input);
        input->setText(QStringLiteral("乙"));
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY(!search->isVisible());
        QVERIFY(workspace->findPaneByContentId(secondRoute));
        QVERIFY(!workspace->findPaneByContentId(switchRoute));
    }
}

void MainWindowSmokeTest::openingPagesLeavesFocusUnset()
{
    QTemporaryDir directory;
    darkeye::settings::Paths paths(directory.path(), directory.filePath(QStringLiteral("data")));
    QVERIFY(paths.ensureRuntimeDirectories());
    darkeye::SqliteConnection database, privateDatabase;
    QString error;
    QVERIFY(database.open(paths.publicDatabase(), false, &error));
    QVERIFY(privateDatabase.open(paths.privateDatabase(), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(database, darkeye::DatabaseKind::Public, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(privateDatabase, darkeye::DatabaseKind::Private, &error));
    QVERIFY(darkeye::PersonRepository(database.database()).create(darkeye::PersonKind::Actress,
        QStringLiteral("焦点测试"), QStringLiteral("Focus Test"), &error));
    darkeye::Work work;
    work.serialNumber = QStringLiteral("FOCUS-001");
    const auto workId = darkeye::WorkRepository(database.database()).insertComplete(work, {}, {}, {}, &error);
    QVERIFY2(workId, qPrintable(error));
    const auto editorRoute = QStringLiteral("work_edit/%1").arg(*workId);
    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    darkeye::MainWindow window(themes, database.database(), privateDatabase.database(), paths);
    auto *search = window.findChild<darkeye::WorkspaceSearch *>();
    auto *workspace = window.findChild<darkeye::myads::WorkspaceWidget *>(QStringLiteral("mainWorkspace"));
    QVERIFY(search && workspace);
    search->openRequested(QStringLiteral("actress"));
    window.resize(1100, 700);
    window.show();
    window.raise();
    window.activateWindow();
    QTRY_COMPARE(QApplication::activeWindow(), &window);
    QTRY_VERIFY(QApplication::focusWidget() == nullptr);
    auto *actressPane = workspace->findPaneByContentId(QStringLiteral("actress"));
    auto *actress = actressPane->contentWidget(QStringLiteral("actress"));
    auto *filter = actress->findChild<QLineEdit *>();
    QVERIFY(filter);
    QCOMPARE(QApplication::focusWidget(), nullptr);
    filter->setFocus();
    QTRY_VERIFY(filter->hasFocus());
    search->openRequested(QStringLiteral("graph"));
    auto *pane = workspace->activePane();
    QCOMPARE(QApplication::focusWidget(), nullptr);
    search->openRequested(QStringLiteral("actress"));
    filter->setFocus();
    QTRY_VERIFY(filter->hasFocus());
    search->openRequested(QStringLiteral("mutiwork"));
    QCOMPARE(QApplication::focusWidget(), nullptr);
    // Selecting a tab must not restore an old filter as its keyboard target.
    pane = workspace->findPaneByContentId(QStringLiteral("actress"));
    pane->setCurrentContentId(QStringLiteral("actress"));
    QCOMPARE(QApplication::focusWidget(), nullptr);
    auto *tabs = pane->findChild<QTabBar *>(QStringLiteral("MyAdsTabBar"));
    QVERIFY(tabs);
    for (int index = 0; index < tabs->count(); ++index)
        if (tabs->tabData(index).toString() == QStringLiteral("mutiwork"))
            QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(index).center());
    QCOMPARE(pane->currentContentId(), QStringLiteral("mutiwork"));
    QCOMPARE(QApplication::focusWidget(), nullptr);
    QTest::keyClick(window.windowHandle(), Qt::Key_Tab);
    QTRY_VERIFY(QApplication::focusWidget() != nullptr);
    // An explicit click into a filter continues to work.
    pane->setCurrentContentId(QStringLiteral("actress"));
    QTest::mouseClick(filter, Qt::LeftButton);
    QTRY_VERIFY(filter->hasFocus());
    // A visible filter in another split must not capture newly opened pages.
    QVERIFY(workspace->moveContent(pane->paneId(), QStringLiteral("actress"), pane->paneId(), darkeye::myads::DropZone::Right));
    filter->setFocus();
    QTRY_VERIFY(filter->hasFocus());
    search->openRequested(QStringLiteral("mutiwork"));
    QCOMPARE(QApplication::focusWidget(), nullptr);
    auto *destination = workspace->activePane();
    search->openRequested(QStringLiteral("library_overview"));
    QCOMPARE(workspace->activePane(), destination);
    QTest::qWait(50);
    QCOMPARE(QApplication::focusWidget(), nullptr);
    search->openRequested(editorRoute);
    QVERIFY(workspace->moveContent(destination->paneId(), editorRoute,
                                   destination->paneId(), darkeye::myads::DropZone::Bottom));
    auto *editorPane = workspace->findPaneByContentId(editorRoute);
    QVERIFY(editorPane && editorPane != destination);
    auto *editor = qobject_cast<darkeye::AddWorkTabPage3 *>(editorPane->contentWidget(editorRoute));
    QVERIFY(editor);
    QTRY_VERIFY(editor->isInitialized());
    auto *serial = findWorkControl<QLineEdit>(editor, QStringLiteral("WorkSerialInput"));
    QVERIFY(serial);

    QVERIFY(editor->isVisible());
    QVERIFY(!findWorkControl<QLineEdit>(editor, QStringLiteral("WorkImageUrlInput")));
    QVERIFY(!findWorkControl<QLineEdit>(editor, QStringLiteral("WorkVideoUrlInput")));

    destination->setCurrentContentId(QStringLiteral("mutiwork"));
    QCOMPARE(QApplication::focusWidget(), nullptr);
    QCOMPARE(workspace->activePane(), destination);
    serial->setFocus();
    QTRY_VERIFY(serial->hasFocus());
    QCOMPARE(workspace->activePane(), editorPane);

    QTest::keyClick(serial, Qt::Key_P, Qt::ControlModifier);
    auto *input = search->findChild<QLineEdit *>();
    QTRY_VERIFY(input->hasFocus());
    input->setText(QStringLiteral("统计"));
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(workspace->findPaneByContentId(QStringLiteral("chart")), editorPane);
    QCOMPARE(workspace->activePane(), editorPane);
    QTRY_VERIFY(QApplication::focusWidget() == nullptr);
    QVERIFY(!search->isVisible());
}

void MainWindowSmokeTest::managementWorkFormScrollsInShortWindow()
{
    QTemporaryDir directory;
    darkeye::settings::Paths paths(directory.path(), directory.filePath(QStringLiteral("data")));
    QVERIFY(paths.ensureRuntimeDirectories());
    darkeye::SqliteConnection database, privateDatabase;
    QString error;
    QVERIFY(database.open(paths.publicDatabase(), false, &error));
    QVERIFY(privateDatabase.open(paths.privateDatabase(), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(database, darkeye::DatabaseKind::Public, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(privateDatabase, darkeye::DatabaseKind::Private, &error));
    darkeye::Work work;
    work.serialNumber = QStringLiteral("SCROLL-001");
    const auto workId = darkeye::WorkRepository(database.database()).insertComplete(work, {}, {}, {}, &error);
    QVERIFY2(workId, qPrintable(error));
    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    darkeye::MainWindow window(themes, database.database(), privateDatabase.database(), paths);
    window.resize(1100, 300);
    window.show();
    auto *search = window.findChild<darkeye::WorkspaceSearch *>();
    auto *workspace = window.findChild<darkeye::myads::WorkspaceWidget *>(QStringLiteral("mainWorkspace"));
    QVERIFY(search && workspace);

    const auto editorRoute = QStringLiteral("work_edit/%1").arg(*workId);
    search->openRequested(editorRoute);
    auto *editorPane = workspace->findPaneByContentId(editorRoute);
    QVERIFY(editorPane);
    auto *editor = qobject_cast<darkeye::AddWorkTabPage3 *>(editorPane->contentWidget(editorRoute));
    QVERIFY(editor);
    QTRY_VERIFY(editor->isInitialized());
    auto *editorWorkspace = editor->findChild<darkeye::myads::WorkspaceWidget *>();
    QVERIFY(editorWorkspace);
    const auto formId = QStringLiteral("addwork_basic");
    auto *formPane = editorWorkspace->findPaneByContentId(formId);
    QVERIFY(formPane);
    auto *formScroll = formPane->contentWidget(formId)->findChild<QScrollArea *>();
    QVERIFY(formScroll);
    QTRY_VERIFY(formScroll->verticalScrollBar()->maximum() > 0);
    QVERIFY(formScroll->viewport()->height() <= 300);
    formScroll->verticalScrollBar()->setValue(formScroll->verticalScrollBar()->maximum());
    QVERIFY(formScroll->widget()->y() < 0);

}

void MainWindowSmokeTest::statisticsPagesOpenIndependentlyAndScroll()
{
    QTemporaryDir directory;
    darkeye::settings::Paths paths(directory.path(), directory.filePath(QStringLiteral("data")));
    QVERIFY(paths.ensureRuntimeDirectories());
    const auto overviewRoute = QStringLiteral("library_overview");
    const auto favoritesRoute = QStringLiteral("top_actresses");
    const auto heatmapRoute = QStringLiteral("record_heatmap");
    const auto legacyRoute = QStringLiteral("information");
    const auto paneId = QStringLiteral("pane_1");
    darkeye::myads::LayoutTree tree;
    tree.addPaneToRoot(paneId);
    const QJsonObject savedLayout{
        {QStringLiteral("layout"), tree.toJson()},
        {QStringLiteral("pane_contents"), QJsonObject{
            {paneId, QJsonArray{QJsonObject{{QStringLiteral("content_id"), legacyRoute}}}}}},
        {QStringLiteral("pane_metadata"), QJsonObject{
            {paneId, QJsonObject{{QStringLiteral("current_content_id"), legacyRoute}}}}}
    };
    QVERIFY(darkeye::myads::saveLayoutAtomic(
        QDir(paths.dataDirectory()).filePath(QStringLiteral("workspace-v2.json")), savedLayout));
    darkeye::SqliteConnection database, privateDatabase;
    QString error;
    QVERIFY(database.open(paths.publicDatabase(), false, &error));
    QVERIFY(privateDatabase.open(paths.privateDatabase(), false, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(database, darkeye::DatabaseKind::Public, &error));
    QVERIFY(darkeye::SchemaManager::initializeEmptyDatabase(privateDatabase, darkeye::DatabaseKind::Private, &error));
    darkeye::ThemeService themes(*qobject_cast<QApplication *>(QCoreApplication::instance()));
    darkeye::MainWindow window(themes, database.database(), privateDatabase.database(), paths);
    window.resize(1100, 360);
    window.show();
    auto *search = window.findChild<darkeye::WorkspaceSearch *>();
    auto *workspace = window.findChild<darkeye::myads::WorkspaceWidget *>(QStringLiteral("mainWorkspace"));
    auto *restoredPane = workspace->findPaneByContentId(overviewRoute);
    QVERIFY(restoredPane);
    QCOMPARE(restoredPane->contentIds(), (QStringList{overviewRoute, favoritesRoute, heatmapRoute}));
    QCOMPARE(restoredPane->currentContentId(), overviewRoute);
    QVERIFY(!workspace->findPaneByContentId(legacyRoute));
    QCOMPARE(restoredPane->contentWidget(overviewRoute)->windowTitle(), QStringLiteral("库藏概览"));
    QCOMPARE(restoredPane->contentWidget(heatmapRoute)->windowTitle(), QStringLiteral("个人记录热力图"));
    search->openRequested(QStringLiteral("chart"));
    auto *pane = workspace->findPaneByContentId(QStringLiteral("chart"));
    QVERIFY(pane);
    auto *plots = qobject_cast<darkeye::StatisticsChartPage *>(pane->contentWidget(QStringLiteral("chart")));
    QVERIFY(plots);
    QCOMPARE(plots->windowTitle(), QStringLiteral("统计"));
    auto *plotScroll = plots->findChild<QScrollArea *>(Qt::FindDirectChildrenOnly);
    QVERIFY(plotScroll);
    QTRY_VERIFY(plotScroll->verticalScrollBar()->maximum() > 0);
    window.resize(1100, 300);
    QTRY_COMPARE(window.height(), 300);
    search->openRequested(favoritesRoute);
    auto *favorites = qobject_cast<darkeye::TopActressesPage *>(pane->contentWidget(favoritesRoute));
    QVERIFY(favorites);
    QCOMPARE(favorites->windowTitle(), QStringLiteral("最喜欢的女优"));
    auto *favoritesScroll = favorites->findChild<QScrollArea *>(QString{}, Qt::FindDirectChildrenOnly);
    QVERIFY(favoritesScroll);
    QTRY_VERIFY(favoritesScroll->verticalScrollBar()->maximum() > 0);
    favoritesScroll->verticalScrollBar()->setValue(favoritesScroll->verticalScrollBar()->maximum());
    QVERIFY(favoritesScroll->verticalScrollBar()->value() > 0);
    QVERIFY(workspace->moveContent(pane->paneId(), heatmapRoute, pane->paneId(),
                                   darkeye::myads::DropZone::Bottom));
    auto *heatmapPane = workspace->findPaneByContentId(heatmapRoute);
    QVERIFY(heatmapPane && heatmapPane != pane);
    QCOMPARE(workspace->panes().size(), 2);
    QVERIFY(pane->removeContent(QStringLiteral("chart")));
    QVERIFY(!workspace->findPaneByContentId(QStringLiteral("chart")));
    QCOMPARE(workspace->findPaneByContentId(overviewRoute), pane);
    QCOMPARE(workspace->findPaneByContentId(favoritesRoute), pane);
    search->openRequested(favoritesRoute);
    QCOMPARE(pane->contentWidget(favoritesRoute), favorites);

    QVERIFY(heatmapPane->removeContent(heatmapRoute));
    search->openRequested(heatmapRoute);
    auto *reopenedPane = workspace->findPaneByContentId(heatmapRoute);
    QVERIFY(reopenedPane);
    QVERIFY(qobject_cast<darkeye::RecordHeatmapPage *>(reopenedPane->contentWidget(heatmapRoute)));
}

QTEST_MAIN(MainWindowSmokeTest)
#include "MainWindowSmokeTest.moc"
#include <QComboBox>
