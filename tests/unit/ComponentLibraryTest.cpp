#include "darkeye_ui/theme/ThemeService.h"
#include "darkeye_ui/components/AnimatedIndicators.h"
#include "darkeye_ui/components/Avatar.h"
#include "darkeye_ui/components/Breadcrumb.h"
#include "darkeye_ui/components/ChamferButton.h"
#include "darkeye_ui/components/Charts.h"
#include "darkeye_ui/components/Chip.h"
#include "darkeye_ui/components/CollapsibleSection.h"
#include "darkeye_ui/components/ColorPicker.h"
#include "darkeye_ui/components/ColorWheel.h"
#include "darkeye_ui/components/CompleterLineEdit.h"
#include "darkeye_ui/components/Components.h"
#include "darkeye_ui/components/DesignInput.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/EmptyState.h"
#include "darkeye_ui/components/HeartLabel.h"
#include "ui/components/IdCheckList.h"
#include "darkeye_ui/components/InteractionEffects.h"
#include "darkeye_ui/components/LinkCard.h"
#include "darkeye_ui/components/LoadingFeedback.h"
#include "darkeye_ui/components/ModalDialog.h"
#include "darkeye_ui/components/ModernScrollMenu.h"
#include "darkeye_ui/components/OctImage.h"
#include "darkeye_ui/components/SoftShadow.h"
#include "ui/components/PathManagement.h"
#include "darkeye_ui/components/MakerSelector.h"
#include "darkeye_ui/components/MakerComboDelegate.h"
#include "darkeye_ui/components/SearchBar.h"
#include "darkeye_ui/components/Sidebar.h"
#include "darkeye_ui/components/StateToggleButton.h"
#include "darkeye_ui/components/TokenControls.h"
#include "darkeye_ui/components/TokenViews.h"
#include "darkeye_ui/components/VerticalText.h"
#include "ui/components/PersonCard.h"
#include "ui/components/WorkCompletenessIndicators.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QColor>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QFile>
#include <QFileDialog>
#include <QElapsedTimer>
#include <QFrame>
#include <QGraphicsScene>
#include <QHeaderView>
#include <QImage>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <algorithm>
#include <memory>
#include <future>
#include <vector>

namespace
{
class ContextMenuSpy final : public QWidget
{
public:
    int contextMenuEvents = 0;

protected:
    void contextMenuEvent(QContextMenuEvent *event) override
    {
        ++contextMenuEvents;
        event->accept();
    }
};
} // namespace

class ComponentLibraryTest final : public QObject
{
    Q_OBJECT

private slots:
    void tokenControlsKeepPythonObjectNames();
    void comboPopupKeepsBoundedHeightAndCurrentSelection();
    void labelSupportsToneAndFormColumn();
    void progressSupportsBusyAndDeterminateModes();
    void stateButtonTogglesAndRefreshes();
    void chipAndEmptyStateKeepInteractionContract();
    void searchAndBreadcrumbKeepInteractionContract();
    void animatedIndicatorsKeepStateContract();
    void loadingAndInteractionEffectsKeepContract();
    void tokenViewsAndCollapsibleSectionKeepContract();
    void editableTableShortcutsKeepContract();
    void completerModalAndColorPickerKeepContract();
    void avatarsHeartAndChamferButtonKeepContract();
    void chartComponentsRenderCurrentData();
    void navigationComponentsKeepSelectionContract();
    void sidebarResolvesExternalIconBasePath();
    void linkImageAndVerticalComponentsKeepContract();
    void avatarBatchRendering();
    void softShadowCacheSeparatesContours();
    void makerSelectorKeepsIdAliasAndReloadContract();
    void makerComboDelegateKeepsTableEditContract();
    void paginationKeepsDynamicPageSizeContract();
    void oklchColorWheelKeepsPickerContract();
    void toastKeepsPythonFactoriesAndStackingContract();
    void personCardKeepsAvatarInteractionContract();
    void idCheckListKeepsStableSelectionWhileFiltering();
    void pathManagementKeepsPythonContract();
    void pathManagementBrowsesWithOneClick();
    void workCompletenessIndicatorsKeepPythonContract();
};

void ComponentLibraryTest::comboPopupKeepsBoundedHeightAndCurrentSelection()
{
    darkeye::ThemeService themes(*qApp);
    for (auto theme : {darkeye::ThemeId::Light, darkeye::ThemeId::Dark}) {
        QVERIFY(themes.setTheme(theme));
        darkeye::DesignComboBox combo;
        combo.setObjectName(QStringLiteral("BusinessSelector"));
        combo.setMaxVisibleItems(8);
        for (int i = 0; i < 100; ++i)
            combo.addItem(QString::number(i));
        combo.setCurrentIndex(70);
        combo.resize(220, 36);
        combo.move(300, 300);
        combo.show();
        QTest::qWait(20);
        combo.showPopup();
        QTRY_VERIFY(combo.view()->isVisible());
        const auto selected = combo.model()->index(70, 0);
        QTRY_VERIFY(combo.view()->viewport()->rect().intersects(combo.view()->visualRect(selected)));
        QVERIFY(combo.view()->height() < combo.view()->sizeHintForRow(0) * 12);
        QCOMPARE(combo.currentIndex(), 70);
        combo.hidePopup();
    }
}

void ComponentLibraryTest::workCompletenessIndicatorsKeepPythonContract()
{
    QCOMPARE(darkeye::workCompletenessKeys().size(), 15);
    QCOMPARE(darkeye::workCompletenessLabels().size(), 15);

    darkeye::WorkCompletenessLedStrip strip;
    const auto unknownCells = strip.findChildren<QWidget *>(
        QRegularExpression(QStringLiteral("^WorkCompletenessCell_")));
    QCOMPARE(unknownCells.size(), 15);
    QCOMPARE(unknownCells.first()->property("completenessState").toString(),
             QStringLiteral("未检测"));

    QMap<QString, bool> flags;
    flags.insert(QStringLiteral("cover"), true);
    strip.setCompleteness(flags);
    QCOMPARE(strip.findChild<QWidget *>(QStringLiteral("WorkCompletenessCell_cover"))
                 ->property("completenessState")
                 .toString(),
             QStringLiteral("已有"));
    QCOMPARE(strip.findChild<QWidget *>(QStringLiteral("WorkCompletenessCell_actress"))
                 ->property("completenessState")
                 .toString(),
             QStringLiteral("暂无"));

    QCOMPARE(darkeye::WorkCompletenessBitsDelegate::normalizeBits(
                 QStringLiteral("111111111111111")),
             QStringLiteral("111111111111111"));
    QCOMPARE(darkeye::WorkCompletenessBitsDelegate::normalizeBits(QStringLiteral("101")),
             QStringLiteral("???????????????"));
    QCOMPARE(darkeye::WorkCompletenessBitsDelegate::tooltipForBits(
                 QStringLiteral("111111111111111")),
             QStringLiteral("库内完整度 15/15\n信息完整"));
    QVERIFY(darkeye::WorkCompletenessBitsDelegate::tooltipForBits(
                QStringLiteral("011111111111111"))
                .contains(QStringLiteral("缺：封面")));
}

void ComponentLibraryTest::makerComboDelegateKeepsTableEditContract()
{
    QStandardItemModel model(1, 3);
    model.setData(model.index(0, 2), 17, Qt::EditRole);
    darkeye::MakerOption maker;
    maker.id = 17;
    maker.chineseName = QStringLiteral("示例片商");
    darkeye::MakerComboDelegate delegate({maker}, 2);
    QWidget parent;
    QStyleOptionViewItem option;
    std::unique_ptr<QWidget> editor(delegate.createEditor(&parent, option, model.index(0, 2)));
    auto *selector = qobject_cast<darkeye::MakerSelector *>(editor.get());
    QVERIFY(selector != nullptr);
    delegate.setEditorData(selector, model.index(0, 2));
    QCOMPARE(selector->maker(), std::optional<qint64>(17));

    selector->setMaker(std::nullopt);
    delegate.setModelData(selector, &model, model.index(0, 2));
    QVERIFY(!model.index(0, 2).data(Qt::EditRole).isValid());
}

void ComponentLibraryTest::pathManagementKeepsPythonContract()
{
    darkeye::SinglePathManagement single(QStringLiteral("封面路径："));
    QCOMPARE(single.size(), QSize(400, 100));
    single.loadPath(QStringLiteral("C:/covers"));
    QCOMPARE(single.path(), QStringLiteral("C:/covers"));
    auto *singleLine = single.findChild<QLineEdit *>(QStringLiteral("SinglePathLineEdit"));
    auto *browse = single.findChild<QPushButton *>(QStringLiteral("SinglePathBrowseButton"));
    QVERIFY(singleLine);
    QVERIFY(browse);
    QCOMPARE(singleLine->minimumWidth(), 300);
    QCOMPARE(browse->maximumWidth(), 30);

    darkeye::MultiplePathManagement multiple(QStringLiteral("路径列表管理："));
    multiple.loadPaths({QStringLiteral(" C:/one "), QStringLiteral("D:/two"), QString{}});
    QCOMPARE(multiple.paths(),
             QStringList({QStringLiteral("C:/one"), QStringLiteral("D:/two"), QString{}}));
    auto *table = multiple.table();
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(table->columnCount(), 2);
    QVERIFY(table->horizontalHeader()->isHidden());
    QVERIFY(!table->verticalHeader()->isHidden());
    QVERIFY(!table->showGrid());
    QCOMPARE(table->selectionBehavior(), QAbstractItemView::SelectRows);
    QCOMPARE(table->selectionMode(), QAbstractItemView::SingleSelection);
    QCOMPARE(table->horizontalHeader()->sectionResizeMode(0), QHeaderView::Stretch);
    QCOMPARE(table->horizontalHeader()->sectionResizeMode(1), QHeaderView::Fixed);
    QCOMPARE(table->columnWidth(1), 22);

    multiple.addRow();
    QCOMPARE(table->rowCount(), 4);
    QCOMPARE(table->currentRow(), 3);
    table->selectRow(1);
    multiple.deleteSelectedRows();
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(multiple.paths(),
             QStringList({QStringLiteral("C:/one"), QString{}, QString{}}));
}

void ComponentLibraryTest::pathManagementBrowsesWithOneClick()
{
    QTemporaryDir first;
    QTemporaryDir second;
    QVERIFY(first.isValid());
    QVERIFY(second.isValid());
    darkeye::MultiplePathManagement multiple;
    multiple.loadPaths({first.path(), first.path()});
    multiple.resize(500, 250);
    multiple.show();
    auto *table = multiple.table();
    table->selectRow(0);
    QCoreApplication::processEvents();

    // A single click must work before the row has been selected or edited.
    const bool nativeDialogsDisabled = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    bool opened = false;
    QTimer::singleShot(0, &multiple, [&] {
        for (QWidget *widget : QApplication::topLevelWidgets())
        {
            if (auto *dialog = qobject_cast<QFileDialog *>(widget))
            {
                opened = true;
                dialog->selectFile(second.path());
                QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            }
        }
    });
    QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier,
                      table->visualItemRect(table->item(1, 1)).center());
    QCoreApplication::processEvents();
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDialogsDisabled);
    QVERIFY(opened);
    QCOMPARE(multiple.paths(), QStringList({first.path(), second.path()}));
    QVERIFY(!(table->item(1, 1)->flags() & Qt::ItemIsEditable));
}

void ComponentLibraryTest::tokenControlsKeepPythonObjectNames()
{
    darkeye::TokenCheckBox check;
    darkeye::TokenRadioButton radio;
    darkeye::TokenSpinBox spin;
    darkeye::TokenGroupBox group;
    darkeye::TokenTabWidget tabs;
    darkeye::TransparentWidget transparent;
    QCOMPARE(check.objectName(), QStringLiteral("DesignCheckBox"));
    QCOMPARE(radio.objectName(), QStringLiteral("DesignRadioButton"));
    QCOMPARE(spin.objectName(), QStringLiteral("DesignSpinBox"));
    QCOMPARE(group.objectName(), QStringLiteral("DesignGroupBox"));
    QCOMPARE(tabs.objectName(), QStringLiteral("DesignTabWidget"));
    QCOMPARE(tabs.tabBar()->objectName(), QStringLiteral("DesignTabBar"));
    QCOMPARE(transparent.objectName(), QStringLiteral("TransparentWidget"));
    QVERIFY(transparent.testAttribute(Qt::WA_TranslucentBackground));
}

void ComponentLibraryTest::labelSupportsToneAndFormColumn()
{
    darkeye::DesignLabel constructed(QStringLiteral("标题"), QStringLiteral("inverse"));
    QCOMPARE(constructed.tone(), QStringLiteral("inverse"));
    darkeye::DesignLabel label(QStringLiteral("演员"));
    label.setTone(QStringLiteral("muted"));
    label.setFormLabelColumn(4);
    QCOMPARE(label.tone(), QStringLiteral("muted"));
    QVERIFY(label.minimumWidth() > 0);
    QVERIFY(label.alignment().testFlag(Qt::AlignRight));
}

void ComponentLibraryTest::progressSupportsBusyAndDeterminateModes()
{
    darkeye::IndeterminateProgressBar progress;
    QCOMPARE(progress.minimum(), 0);
    QCOMPARE(progress.maximum(), 0);
    progress.stop(120);
    QCOMPARE(progress.maximum(), 100);
    QCOMPARE(progress.value(), 100);
    progress.start();
    QCOMPARE(progress.maximum(), 0);
}

void ComponentLibraryTest::stateButtonTogglesAndRefreshes()
{
    darkeye::ThemeService themes(*qApp);
    darkeye::StateToggleButton button(QStringLiteral("x"), QStringLiteral("check"), 20, 28,
                                      &themes);
    QSignalSpy changed(&button, &darkeye::StateToggleButton::stateChanged);
    QVERIFY(!button.state());
    QVERIFY(!button.getState());
    button.click();
    QVERIFY(button.state());
    QVERIFY(button.getState());
    QCOMPARE(changed.count(), 1);
    QVERIFY(themes.setTheme(darkeye::ThemeId::Dark));
    QVERIFY(!button.icon().isNull());
    button.setState(false);
    QVERIFY(!button.state());
    QCOMPARE(changed.count(), 1);
}

void ComponentLibraryTest::chipAndEmptyStateKeepInteractionContract()
{
    darkeye::Chip chip(QStringLiteral("未观看"), QStringLiteral("info"), true, true);
    QCOMPARE(chip.objectName(), QStringLiteral("DesignChip"));
    QCOMPARE(chip.tone(), QStringLiteral("info"));
    QVERIFY(chip.isChecked());

    darkeye::EmptyState empty(QStringLiteral("暂无作品"), QStringLiteral("调整筛选条件后重试"),
                              QStringLiteral("清除筛选"));
    QSignalSpy triggered(&empty, &darkeye::EmptyState::actionTriggered);
    auto *action = empty.findChild<QPushButton *>(QStringLiteral("DesignEmptyStateAction"));
    QVERIFY(action != nullptr);
    action->click();
    QCOMPARE(triggered.count(), 1);
    empty.setActionText({});
    QVERIFY(action->isHidden());
}

void ComponentLibraryTest::searchAndBreadcrumbKeepInteractionContract()
{
    darkeye::SearchBar search(QStringLiteral("搜索作品"));
    QSignalSpy changed(&search, &darkeye::SearchBar::searchChanged);
    QSignalSpy cleared(&search, &darkeye::SearchBar::clearRequested);
    search.setText(QStringLiteral("ABP"));
    QCOMPARE(search.text(), QStringLiteral("ABP"));
    QCOMPARE(changed.count(), 1);
    QTest::keyClick(search.lineEdit(), Qt::Key_Escape);
    QVERIFY(search.text().isEmpty());
    QCOMPARE(cleared.count(), 1);

    darkeye::Breadcrumb breadcrumb(
        {QStringLiteral("作品"), QStringLiteral("详情"), QStringLiteral("剧照")});
    QCOMPARE(breadcrumb.currentIndex(), 2);
    QSignalSpy crumb(&breadcrumb, &darkeye::Breadcrumb::crumbClicked);
    const auto buttons =
        breadcrumb.findChildren<QPushButton *>(QStringLiteral("DesignBreadcrumbCrumb"));
    QCOMPARE(buttons.size(), 3);
    buttons.first()->click();
    QCOMPARE(breadcrumb.currentIndex(), 0);
    QCOMPARE(crumb.count(), 1);
}

void ComponentLibraryTest::animatedIndicatorsKeepStateContract()
{
    darkeye::ThemeService themes(*qApp);
    darkeye::ToggleSwitch toggle(48, 24, &themes);
    QSignalSpy toggled(&toggle, &darkeye::ToggleSwitch::toggled);
    toggle.setChecked(true);
    QVERIFY(toggle.isChecked());
    QCOMPARE(toggled.count(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(toggle.offset() > 20.0, 500);
    QVERIFY(themes.setTheme(darkeye::ThemeId::Dark));
    QCOMPARE(toggle.offset(), 26.0);

    darkeye::CircularLoading loading(32, 4, &themes);
    loading.show();
    QTRY_VERIFY_WITH_TIMEOUT(loading.isAnimating(), 200);
    loading.stop();
    QVERIFY(!loading.isAnimating());
    loading.start();
    QVERIFY(loading.isAnimating());
    loading.hide();
    QVERIFY(!loading.isAnimating());
}

void ComponentLibraryTest::loadingAndInteractionEffectsKeepContract()
{
    darkeye::ThemeService themes(*qApp);
    darkeye::DesignButton iconButton(QStringLiteral("带图标"), QStringLiteral("primary"),
                                     QStringLiteral("check"), QSize(18, 20),
                                     QColor(QStringLiteral("#336699")));
    QCOMPARE(iconButton.variant(), QStringLiteral("primary"));
    QCOMPARE(iconButton.iconSize(), QSize(18, 20));
    QVERIFY(!iconButton.icon().isNull());
    darkeye::Skeleton skeleton(16, 6, true, 20, &themes);
    QCOMPARE(skeleton.objectName(), QStringLiteral("DesignSkeleton"));
    QVERIFY(skeleton.isAnimating());
    skeleton.setAnimated(false);
    QVERIFY(!skeleton.isAnimating());

    QWidget target;
    target.resize(40, 24);
    darkeye::CalloutTooltip tooltip(&themes);
    tooltip.showFor(&target, QStringLiteral("刷新"));
    QVERIFY(tooltip.isVisible());
    QVERIFY(tooltip.width() > 0);
    tooltip.showFor(&target, {});
    QVERIFY(!tooltip.isVisible());

    darkeye::RotateButton rotate(QStringLiteral("refresh"), &themes);
    darkeye::ShakeButton shake(QStringLiteral("settings"), &themes);
    darkeye::IconButton configuredIcon(QStringLiteral("settings"), {}, 18, 40,
                                       false, true, &themes);
    QCOMPARE(configuredIcon.iconSize(), QSize(18, 18));
    QCOMPARE(configuredIcon.size(), QSize(40, 40));
    QCOMPARE(configuredIcon.property("hoverable").toBool(), false);
    darkeye::RotateButton configuredRotate(QStringLiteral("refresh"), {}, 19, 41,
                                           false, &themes);
    darkeye::ShakeButton configuredShake(QStringLiteral("settings"), {}, 20, 42,
                                         true, &themes);
    QCOMPARE(configuredRotate.size(), QSize(41, 41));
    QCOMPARE(configuredShake.size(), QSize(42, 42));
    QTest::mousePress(&rotate, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(rotate.angle() > 0.0, 200);
    QTest::mousePress(&shake, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(shake.iconOffset()) > 0.0, 200);

    darkeye::ClickableSlider slider(Qt::Horizontal, &themes);
    slider.setRange(0, 100);
    slider.resize(200, 22);
    QTest::mouseClick(&slider, Qt::LeftButton, {}, QPoint(150, 11));
    QVERIFY(slider.value() >= 70);
}

void ComponentLibraryTest::tokenViewsAndCollapsibleSectionKeepContract()
{
    darkeye::TokenTableView tableView;
    darkeye::TokenTableWidget tableWidget(2, 3);
    darkeye::TokenTreeView tree;
    darkeye::TokenListView listView;
    darkeye::TokenListWidget listWidget;
    darkeye::TokenDateTimeEdit dateTime;
    darkeye::TokenKeySequenceEdit shortcut;
    QCOMPARE(tableView.objectName(), QStringLiteral("DesignTableView"));
    QCOMPARE(tableWidget.objectName(), QStringLiteral("DesignTableWidget"));
    QCOMPARE(tree.objectName(), QStringLiteral("DesignTreeView"));
    QVERIFY(tree.alternatingRowColors());
    QVERIFY(tree.uniformRowHeights());
    QCOMPARE(listView.objectName(), QStringLiteral("DesignListView"));
    QCOMPARE(listWidget.objectName(), QStringLiteral("DesignListWidget"));
    QCOMPARE(dateTime.objectName(), QStringLiteral("DesignDateTimeEdit"));
    QCOMPARE(shortcut.objectName(), QStringLiteral("DesignKeySequenceEdit"));
    const auto shortcutLines = shortcut.findChildren<QLineEdit *>();
    QVERIFY(!shortcutLines.isEmpty());
    QCOMPARE(shortcutLines.first()->objectName(), QStringLiteral("DesignKeySequenceEditLineEdit"));

    darkeye::ThemeService themes(*qApp);
    darkeye::TokenCollapsibleSection section(QStringLiteral("筛选"), &themes);
    section.addWidget(new QWidget(&section));
    QSignalSpy toggled(&section, &darkeye::TokenCollapsibleSection::toggled);
    QVERIFY(!section.isExpanded());
    section.expand();
    QVERIFY(section.isExpanded());
    QVERIFY(section.contentWidget()->isVisibleTo(&section));
    QCOMPARE(toggled.count(), 1);
    section.collapse();
    QVERIFY(!section.isExpanded());

    darkeye::TokenVLabel explicitLabel(
        QStringLiteral("标签"), QColor(QStringLiteral("#112233")),
        QColor(QStringLiteral("#f0f0f0")), 72, 144,
        QColor(QStringLiteral("#445566")), QColor(QStringLiteral("#778899")),
        &themes);
    QCOMPARE(explicitLabel.size(), QSize(72, 144));
    explicitLabel.setTextDynamic(QStringLiteral("新标签"));
    QVERIFY(explicitLabel.size().width() > 0);
    QVERIFY(explicitLabel.size().height() > 0);
}

void ComponentLibraryTest::editableTableShortcutsKeepContract()
{
    darkeye::TokenTableWidget table(1, 1);
    QSignalSpy addRequested(&table, &darkeye::TokenTableWidget::addRequested);
    QSignalSpy deleteRequested(&table, &darkeye::TokenTableWidget::deleteRequested);
    QSignalSpy submitRequested(&table, &darkeye::TokenTableWidget::submitRequested);

    QTest::keyClick(&table, Qt::Key_N, Qt::ControlModifier);
    QTest::keyClick(&table, Qt::Key_Delete);
    QTest::keyClick(&table, Qt::Key_S, Qt::ControlModifier);
    QCOMPARE(addRequested.count(), 1);
    QCOMPARE(deleteRequested.count(), 1);
    QCOMPARE(submitRequested.count(), 1);

    darkeye::ReorderableTokenTableWidget reorderable;
    QVERIFY(reorderable.dragEnabled());
    QVERIFY(reorderable.acceptDrops());
    QVERIFY(reorderable.showDropIndicator());
    QCOMPARE(reorderable.dragDropMode(), QAbstractItemView::InternalMove);
}

void ComponentLibraryTest::completerModalAndColorPickerKeepContract()
{
    darkeye::CompleterLineEdit completer(
        [] { return QStringList{QStringLiteral("ABP-001"), QStringLiteral("SSIS-002")}; });
    QSignalSpy loaded(&completer, &darkeye::CompleterLineEdit::itemsLoaded);
    QTRY_VERIFY_WITH_TIMEOUT(loaded.count() >= 1, 1000);
    QCOMPARE(completer.items().size(), 2);
    QVERIFY(completer.completer() != nullptr);
    QCOMPARE(completer.completer()->popup()->objectName(), QStringLiteral("DesignCompleterPopup"));

    darkeye::ModalDialog dialog(QStringLiteral("删除作品"), QStringLiteral("确定删除吗？"),
                                QStringLiteral("删除"), QStringLiteral("取消"), true, true);
    QCOMPARE(dialog.objectName(), QStringLiteral("DesignModalDialog"));
    auto *confirm = dialog.findChild<QPushButton *>(QStringLiteral("DesignModalConfirm"));
    QVERIFY(confirm != nullptr);
    QCOMPARE(confirm->property("variant").toString(), QStringLiteral("danger"));

    darkeye::ColorPicker picker(QColor(QStringLiteral("#123456")));
    QSignalSpy colors(&picker, &darkeye::ColorPicker::colorChanged);
    QCOMPARE(picker.color(), QStringLiteral("#123456"));
    QCOMPARE(picker.getColor(), QStringLiteral("#123456"));
    picker.setColor(QStringLiteral("#abcdef"));
    QCOMPARE(picker.color(), QStringLiteral("#abcdef"));
    QCOMPARE(picker.getColor(), QStringLiteral("#abcdef"));
    QCOMPARE(colors.count(), 1);
    picker.setShape(darkeye::ColorPicker::Shape::Circle);
    QCOMPARE(picker.size(), QSize(32, 32));
    QVERIFY(picker.text().isEmpty());
}

void ComponentLibraryTest::avatarsHeartAndChamferButtonKeepContract()
{
    darkeye::ThemeService themes(*qApp);
    darkeye::Avatar avatar(QStringLiteral("Ada Lovelace"), {}, 36, &themes);
    QCOMPARE(avatar.objectName(), QStringLiteral("DesignAvatar"));
    QCOMPARE(avatar.initials(), QStringLiteral("AL"));
    QCOMPARE(avatar.size(), QSize(36, 36));
    darkeye::AvatarGroup group(
        {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), QStringLiteral("D")}, 32,
        10, 3, &themes);
    QCOMPARE(group.findChildren<darkeye::Avatar *>().size(), 3);

    darkeye::HeartLabel heart;
    QSignalSpy clicked(&heart, &darkeye::HeartLabel::clicked);
    QTest::mousePress(&heart, Qt::LeftButton);
    QVERIFY(heart.isChecked());
    QVERIFY(heart.getState());
    QCOMPARE(clicked.count(), 1);

    heart.setState(false);
    QImage hollowHeart(heart.size(), QImage::Format_ARGB32_Premultiplied);
    hollowHeart.fill(Qt::transparent);
    heart.render(&hollowHeart);
    QVERIFY(hollowHeart.pixelColor(hollowHeart.width() / 2, hollowHeart.height() / 2)
                .alpha()
            == 0);

    darkeye::ChamferButton chamfer(QStringLiteral("刷新"), QStringLiteral("refresh"), 20, 40, 0.22,
                                   &themes);
    QCOMPARE(chamfer.objectName(), QStringLiteral("DesignChamferButton"));
    QCOMPARE(chamfer.chamferRatio(), 0.22);
    chamfer.setChamferRatio(2.0);
    QCOMPARE(chamfer.chamferRatio(), 1.0);
    chamfer.setSelected(true);
    QVERIFY(chamfer.isSelected());
    chamfer.setHoverable(false);
    QVERIFY(!chamfer.isHoverable());
    chamfer.setMenuId(QStringLiteral("library"));
    QCOMPARE(chamfer.menuId(), QStringLiteral("library"));
    chamfer.setUseNativeTooltip(false);
    QVERIFY(!chamfer.usesNativeTooltip());
    QVERIFY(chamfer.toolTip().isEmpty());

    chamfer.setIconName({});
    QImage withoutExternalIcon(chamfer.size(), QImage::Format_ARGB32_Premultiplied);
    withoutExternalIcon.fill(Qt::transparent);
    chamfer.render(&withoutExternalIcon);

    QTemporaryDir iconDirectory;
    QVERIFY(iconDirectory.isValid());
    const QString externalIconPath = iconDirectory.filePath(QStringLiteral("external-icon.svg"));
    QFile externalIcon(externalIconPath);
    QVERIFY(externalIcon.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(externalIcon.write(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\">"
        "<rect width=\"24\" height=\"24\" fill=\"currentColor\"/></svg>") > 0);
    externalIcon.close();

    chamfer.setIconPath(externalIconPath);
    QImage withExternalIcon(chamfer.size(), QImage::Format_ARGB32_Premultiplied);
    withExternalIcon.fill(Qt::transparent);
    chamfer.render(&withExternalIcon);
    QVERIFY(withExternalIcon != withoutExternalIcon);

    chamfer.setIconName(QStringLiteral("refresh"));
}

void ComponentLibraryTest::chartComponentsRenderCurrentData()
{
    darkeye::ThemeService themes(*qApp);
    const QDate activeDate(2026, 9, 10);
    darkeye::CalendarHeatmap calendar(2026, {{activeDate, 4}}, &themes);
    QCOMPARE(calendar.year(), 2026);
    QCOMPARE(calendar.data().value(activeDate), 4);
    QImage calendarImage(calendar.size(), QImage::Format_ARGB32_Premultiplied);
    calendarImage.fill(Qt::transparent);
    calendar.render(&calendarImage);
    QVERIFY(!calendarImage.isNull());
    calendar.updateData(2024, {{QDate(2024, 2, 29), 2}});
    QCOMPARE(calendar.year(), 2024);

    darkeye::RadarChartWidget radar(
        {QStringLiteral("完整度"), QStringLiteral("评分"), QStringLiteral("收藏")}, {0.8, 0.6, 1.0},
        {QStringLiteral("80"), QStringLiteral("3"), QStringLiteral("是")}, 5, &themes);
    QCOMPARE(radar.categories().size(), 3);
    QVERIFY(radar.scene() != nullptr);
    QVERIFY(radar.scene()->items().size() > 10);
    radar.updateChart({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")},
                      {0.1, 0.2, 0.3});
    QCOMPARE(radar.values().at(2), 0.3);
}

void ComponentLibraryTest::navigationComponentsKeepSelectionContract()
{
    darkeye::ThemeService themes(*qApp);
    darkeye::Sidebar sidebar(
        {{QStringLiteral("work"), QStringLiteral("作品"), QStringLiteral("database")},
         {QStringLiteral("person"), QStringLiteral("人物"), QStringLiteral("house")}},
        &themes);
    QCOMPARE(sidebar.selectedId(), QStringLiteral("work"));
    QSignalSpy selection(&sidebar, &darkeye::Sidebar::selectionChanged);
    sidebar.select(QStringLiteral("person"));
    QCOMPARE(sidebar.selectedId(), QStringLiteral("person"));
    QCOMPARE(selection.count(), 1);
    sidebar.clearSelection();
    QVERIFY(sidebar.selectedId().isEmpty());
    sidebar.toggleMenu();
    QVERIFY(sidebar.isExpanded());

    auto *general = new QWidget;
    auto *advanced = new QWidget;
    darkeye::ModernScrollMenu menu(
        {{QStringLiteral("常规"), general}, {QStringLiteral("高级"), advanced}});
    QCOMPARE(menu.sectionCount(), 2);
    QCOMPARE(menu.findChildren<QFrame *>(QStringLiteral("DesignScrollMenuSeparator")).size(), 1);
    QCOMPARE(menu.currentSection(), 0);
    menu.scrollToSection(1);
    QCOMPARE(menu.currentSection(), 1);
    menu.addSection(QStringLiteral("外观"), new QWidget);
    QCOMPARE(menu.sectionCount(), 3);
    QCOMPARE(menu.findChildren<QFrame *>(QStringLiteral("DesignScrollMenuSeparator")).size(), 2);

    darkeye::Sidebar2 compact(
        {{QStringLiteral("work"), QStringLiteral("作品"), QStringLiteral("database")},
         {QStringLiteral("home"), QStringLiteral("首页"), QStringLiteral("house")}},
        &themes);
    QCOMPARE(compact.width(), 72);
    QCOMPARE(compact.selectedId(), QStringLiteral("work"));
    compact.select(QStringLiteral("home"));
    QCOMPARE(compact.selectedId(), QStringLiteral("home"));
}

void ComponentLibraryTest::sidebarResolvesExternalIconBasePath()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString iconPath = directory.filePath(QStringLiteral("external.svg"));
    QFile iconFile(iconPath);
    QVERIFY(iconFile.open(QIODevice::WriteOnly | QIODevice::Text));
    iconFile.write("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 16 16\">"
                   "<path fill=\"currentColor\" d=\"M0 0h16v16H0z\"/></svg>");
    iconFile.close();

    darkeye::ThemeService themes(*qApp);
    darkeye::Sidebar sidebar(
        {{QStringLiteral("external"), QStringLiteral("外部图标"),
          QStringLiteral("external.svg")}},
        directory.path(), &themes);
    const auto iconButtons = sidebar.findChildren<darkeye::IconButton *>();
    const auto external = std::find_if(
        iconButtons.cbegin(), iconButtons.cend(),
        [](const darkeye::IconButton *button)
        { return button->iconName() == QStringLiteral("external.svg"); });
    QVERIFY(external != iconButtons.cend());
    QVERIFY(!(*external)->icon().isNull());
}

void ComponentLibraryTest::linkImageAndVerticalComponentsKeepContract()
{
    darkeye::ThemeService themes(*qApp);
    darkeye::TokenLinkCard link(QStringLiteral("项目主页"), QStringLiteral("打开 Darkeye 文档"),
                                QStringLiteral("https://example.com"), &themes);
    QCOMPARE(link.objectName(), QStringLiteral("DesignLinkCard"));
    QCOMPARE(link.url(), QUrl(QStringLiteral("https://example.com")));
    QCOMPARE(link.focusPolicy(), Qt::StrongFocus);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString imagePath = directory.filePath(QStringLiteral("avatar.png"));
    QImage source(100, 100, QImage::Format_RGB32);
    source.fill(Qt::magenta);
    QVERIFY(source.save(imagePath));
    darkeye::OctImage oct(imagePath, {}, 80, false);
    QSignalSpy loaded(&oct, &darkeye::OctImage::imageLoaded);
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
    QVERIFY(!oct.pixmap().isNull());
    QVERIFY(oct.mask().isEmpty());
    QVERIFY(!oct.containsAvatarPoint(QPointF(0, 0)));
    QVERIFY(oct.containsAvatarPoint(QPointF(40, 40)));
    const auto rendered = oct.grab().toImage();
    QCOMPARE(rendered.pixelColor(0, 0).alpha(), 0);
    QCOMPARE(rendered.pixelColor(rendered.width() / 2, rendered.height() / 2), QColor(Qt::magenta));
    darkeye::OctImage shadowed(imagePath, {}, 80, true);
    QSignalSpy shadowLoaded(&shadowed, &darkeye::OctImage::imageLoaded);
    QTRY_COMPARE_WITH_TIMEOUT(shadowLoaded.count(), 1, 1000);
    const auto shadowRender = shadowed.grab().toImage();
    const qreal scale = shadowRender.devicePixelRatio();
    bool hasSoftShadow = false;
    for (int y = 0; y < shadowRender.height(); ++y) {
        const QPointF point(40, (y + 0.5) / scale);
        if (shadowed.containsAvatarPoint(point))
            continue;
        const int alpha = shadowRender.pixelColor(qRound(40 * scale), y).alpha();
        hasSoftShadow |= alpha > 0 && alpha < 255;
    }
    QVERIFY(hasSoftShadow);
    QCOMPARE(shadowRender.pixelColor(0, 0).alpha(), 0);
    oct.updateImage({});
    QTRY_VERIFY_WITH_TIMEOUT(!oct.pixmap().isNull(), 1000);
    QCOMPARE(oct.source(), QStringLiteral(":/icons/anonymous.jpg"));

    darkeye::VerticalTextLabel vertical(QStringLiteral("作品ABP"), QStringLiteral("normal"),
                                        &themes);
    QCOMPARE(vertical.text(), QStringLiteral("作品ABP"));
    vertical.setTone(QStringLiteral("inverse"));
    QCOMPARE(vertical.property("tone").toString(), QStringLiteral("inverse"));
    vertical.setText(QStringLiteral("作品《ABP》"));
    QCOMPARE(vertical.text(), QStringLiteral("作品︽ABP︾"));

    darkeye::TokenVLabel tag(QStringLiteral("收藏"), &themes);
    QVERIFY(tag.sizeHint().width() > 0);
    tag.setTextDynamic(QStringLiteral("未观看"));
    QCOMPARE(tag.text(), QStringLiteral("未观看"));

    darkeye::TokenVerticalTabBar tabs(&themes);
    tabs.addTab(QStringLiteral("作品"));
    tabs.addTab(QStringLiteral("人物AB"));
    QCOMPARE(tabs.objectName(), QStringLiteral("VerticalTabBar"));
    QVERIFY(tabs.tabSizeHint(1).height() > 40);
}

void ComponentLibraryTest::softShadowCacheSeparatesContours()
{
    const auto images = std::async(std::launch::async, [] {
        QPainterPath rectangle;
        rectangle.addRect(QRectF(8, 8, 64, 64));
        QPainterPath triangle;
        triangle.moveTo(40, 8);
        triangle.lineTo(72, 72);
        triangle.lineTo(8, 72);
        triangle.closeSubpath();
        return QList<QImage>{darkeye::renderSoftShadow(QSize(80, 80), rectangle, 1),
                             darkeye::renderSoftShadow(QSize(80, 80), triangle, 1),
                             darkeye::renderSoftShadow(QSize(80, 80), rectangle, 1)};
    }).get();
    QCOMPARE(images.at(0).cacheKey(), images.at(2).cacheKey());
    QVERIFY(images.at(0).pixelColor(12, 40).alpha() > 60);
    QVERIFY(images.at(1).pixelColor(12, 40).alpha() < 10);
}

void ComponentLibraryTest::avatarBatchRendering()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString imagePath = directory.filePath(QStringLiteral("large-avatar.png"));
    QImage source(1000, 1000, QImage::Format_RGB32);
    source.fill(Qt::magenta);
    QVERIFY(source.save(imagePath));
    std::vector<std::unique_ptr<darkeye::OctImage>> avatars;
    int loaded = 0;
    for (int i = 0; i < 48; ++i) {
        auto avatar = std::make_unique<darkeye::OctImage>(imagePath, QString(), 150, true);
        connect(avatar.get(), &darkeye::OctImage::imageLoaded, this, [&loaded] { ++loaded; });
        avatars.push_back(std::move(avatar));
    }
    QTRY_COMPARE_WITH_TIMEOUT(loaded, 48, 10000);
    QElapsedTimer timer;
    timer.start();
    for (const auto &avatar : avatars) {
        // Keep only a screen-sized source bitmap, rather than the full photo.
        const int side = qCeil(avatar->height() * avatar->devicePixelRatioF());
        QCOMPARE(avatar->pixmap().size(), QSize(side, side));
        const auto image = avatar->grab().toImage();
        QCOMPARE(image.pixelColor(image.width() / 2, image.height() / 2), QColor(Qt::magenta));
    }
    qInfo() << "48 avatar first paints (ms):" << timer.nsecsElapsed() / 1000000.0;
    // A screen change must prepare a new cache asynchronously, while retaining
    // the old bitmap for immediate paints during the transition.
    QEvent dprChange(QEvent::DevicePixelRatioChange);
    QCoreApplication::sendEvent(avatars.front().get(), &dprChange);
    QVERIFY(!avatars.front()->pixmap().isNull());
    QTRY_COMPARE_WITH_TIMEOUT(loaded, 49, 10000);
}

void ComponentLibraryTest::makerSelectorKeepsIdAliasAndReloadContract()
{
    darkeye::MakerSelector selector({
        {qint64(10),
         QStringLiteral("东京热"),
         QStringLiteral("東京熱"),
         {QStringLiteral("Tokyo Hot")}},
        {qint64(20), {}, QStringLiteral("S1 NO.1 STYLE"), {QStringLiteral("S1")}},
    });
    QCOMPARE(selector.objectName(), QStringLiteral("DesignMakerSelector"));
    selector.setMaker(qint64(20));
    QVERIFY(selector.maker().has_value());
    QCOMPARE(*selector.maker(), qint64(20));
    selector.setMaker(qint64(999));
    QVERIFY(!selector.maker().has_value());

    QSignalSpy loaded(&selector, &darkeye::MakerSelector::makersLoaded);
    selector.setLoader(
        []
        {
            return QList<darkeye::MakerOption>{
                {qint64(30), QStringLiteral("新厂商"), {}, {QStringLiteral("New")}}};
        });
    selector.reloadMakers();
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 1000);
    QCOMPARE(selector.count(), 2);
    selector.setMaker(qint64(30));
    QCOMPARE(*selector.maker(), qint64(30));
}

void ComponentLibraryTest::paginationKeepsDynamicPageSizeContract()
{
    darkeye::Pagination pagination(101, 20);
    pagination.setCurrentPage(5);
    QCOMPARE(pagination.currentPage(), 5);
    QSignalSpy sizes(&pagination, &darkeye::Pagination::pageSizeChanged);
    QSignalSpy pages(&pagination, &darkeye::Pagination::pageChanged);
    pagination.setPageSize(33);
    QCOMPARE(pagination.pageSize(), 33);
    QCOMPARE(pagination.currentPage(), 4);
    QVERIFY(pagination.pageSizeOptions().contains(33));
    QCOMPARE(sizes.count(), 1);
    QCOMPARE(pages.count(), 1);
    auto *selector = pagination.findChild<QComboBox *>(QStringLiteral("PaginationPageSize"));
    QVERIFY(selector != nullptr);
    QCOMPARE(selector->currentData().toInt(), 33);
    pagination.setPageSizeOptions({25, 50});
    QCOMPARE(pagination.pageSizeOptions(), QList<int>({25, 33, 50}));
}

void ComponentLibraryTest::oklchColorWheelKeepsPickerContract()
{
    float lightness = 0.0F;
    float chroma = 0.0F;
    float hue = 0.0F;
    QVERIFY(darkeye::OKLCHColorWheel::srgbHexToOklch(QStringLiteral("#336699"), lightness,
                                                     chroma, hue));
    const QColor roundTrip = darkeye::OKLCHColorWheel::oklchToRgb(lightness, chroma, hue);
    QVERIFY(qAbs(roundTrip.red() - 0x33) <= 1);
    QVERIFY(qAbs(roundTrip.green() - 0x66) <= 1);
    QVERIFY(qAbs(roundTrip.blue() - 0x99) <= 1);

    darkeye::ColorWheelSimple popup;
    QSignalSpy changed(&popup, &darkeye::ColorWheelSimple::colorChanged);
    popup.setInitialColor(QStringLiteral("#123456"));
    QVERIFY(changed.count() >= 1);
    QVERIFY(popup.getHexColor().compare(QStringLiteral("#123456"), Qt::CaseInsensitive) == 0);
    popup.show();
    QTRY_VERIFY(popup.isVisible());
    popup.hide();

    darkeye::RatingSelector rating;
    const auto hearts = rating.findChildren<QLabel *>(QStringLiteral("RatingHeart"));
    QCOMPARE(hearts.size(), 5);
    QCOMPARE(hearts.first()->text(), QStringLiteral("🤍"));
    QSignalSpy ratings(&rating, &darkeye::RatingSelector::ratingChanged);
    QTest::mousePress(hearts.at(2), Qt::LeftButton);
    QCOMPARE(rating.rating(), 3);
    QCOMPARE(rating.getRating(), 3);
    QCOMPARE(ratings.count(), 1);
    QCOMPARE(hearts.at(2)->text(), QStringLiteral("❤️"));
}

void ComponentLibraryTest::toastKeepsPythonFactoriesAndStackingContract()
{
    darkeye::ThemeService themes(*qApp);
    QWidget anchor;
    anchor.setGeometry(100, 100, 500, 300);
    anchor.show();

    auto *success = darkeye::Toast::showSuccess(&anchor, QStringLiteral("保存成功"), &themes, 0);
    auto *warning = darkeye::Toast::showWarning(&anchor, QStringLiteral("字段不完整"), &themes, 0);
    QTRY_VERIFY(success->isVisible());
    QTRY_VERIFY(warning->isVisible());
    QCOMPARE(success->level(), darkeye::Toast::Level::Success);
    QCOMPARE(warning->level(), darkeye::Toast::Level::Warning);
    QCOMPARE(success->x() + success->width(),
             anchor.mapToGlobal(anchor.rect().topLeft()).x() + anchor.width() - 16);
    QCOMPARE(warning->y(), success->y() + success->height() + 8);

    auto *screenToast =
        darkeye::Toast::showError(nullptr, QStringLiteral("后台任务失败"), &themes, 0);
    QTRY_VERIFY(screenToast->isVisible());
    QVERIFY(screenToast->x() > 0);
    QVERIFY(screenToast->y() >= 0);

    success->close();
    warning->close();
    screenToast->close();
}

void ComponentLibraryTest::personCardKeepsAvatarInteractionContract()
{
    ContextMenuSpy host;
    darkeye::PersonCard card(42, QStringLiteral("示例演员"), {}, {}, &host);
    QCOMPARE(card.objectName(), QStringLiteral("PersonCard"));
    QCOMPARE(card.personId(), qint64(42));
    QCOMPARE(card.name(), QStringLiteral("示例演员"));
    QSignalSpy activated(&card, &darkeye::PersonCard::activated);
    QSignalSpy edited(&card, &darkeye::PersonCard::editRequested);
    QVERIFY(!card.avatar()->containsAvatarPoint(QPointF(0, 0)));
    QVERIFY(card.avatar()->containsAvatarPoint(QPointF(75, 75)));
    QVERIFY(card.avatar()->graphicsEffect() == nullptr);
    QTest::mouseClick(card.avatar(), Qt::LeftButton, Qt::NoModifier, QPoint(1, 1));
    QCOMPARE(activated.count(), 0);
    QTest::mouseClick(card.avatar(), Qt::LeftButton);
    QTest::mouseClick(card.avatar(), Qt::RightButton);
    QCOMPARE(activated.count(), 1);
    QTRY_COMPARE(edited.count(), 1);
    QContextMenuEvent contextMenu(QContextMenuEvent::Mouse, card.avatar()->rect().center(),
                                  card.avatar()->mapToGlobal(card.avatar()->rect().center()));
    QCoreApplication::sendEvent(card.avatar(), &contextMenu);
    QCOMPARE(host.contextMenuEvents, 0);
    card.updateData(84, QStringLiteral("更新姓名"), {});
    QCOMPARE(card.personId(), qint64(84));
    QCOMPARE(card.name(), QStringLiteral("更新姓名"));
}

void ComponentLibraryTest::idCheckListKeepsStableSelectionWhileFiltering()
{
    darkeye::IdCheckList selector(QStringLiteral("演员"));
    selector.setOptions({{1, QStringLiteral("演员甲"), QStringLiteral("女优")},
                         {2, QStringLiteral("演员乙"), QStringLiteral("男优")}});
    QSignalSpy changed(&selector, &darkeye::IdCheckList::selectionChanged);
    selector.setSelectedIds({2});
    QCOMPARE(selector.selectedIds(), QList<qint64>{2});
    auto *search = selector.findChild<QLineEdit *>(QStringLiteral("IdCheckListSearch"));
    auto *list = selector.findChild<QListWidget *>(QStringLiteral("IdCheckListItems"));
    QVERIFY(search != nullptr);
    QVERIFY(list != nullptr);
    search->setText(QStringLiteral("甲"));
    QVERIFY(!list->item(0)->isHidden());
    QVERIFY(list->item(1)->isHidden());
    QCOMPARE(selector.selectedIds(), QList<qint64>{2});
    list->item(0)->setCheckState(Qt::Checked);
    QCOMPARE(selector.selectedIds(), QList<qint64>({1, 2}));
    QCOMPARE(changed.count(), 1);
    selector.clearSelection();
    QVERIFY(selector.selectedIds().isEmpty());
    QCOMPARE(changed.count(), 2);
}

QTEST_MAIN(ComponentLibraryTest)
#include "ComponentLibraryTest.moc"
