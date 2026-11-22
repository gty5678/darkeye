#include "darkeye_ui/theme/ThemeService.h"
#include "darkeye_ui/theme/IconProvider.h"
#include "darkeye_ui/components/Components.h"
#include "darkeye_ui/layouts/Layouts.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QKeySequence>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QStackedWidget>
#include <QStringListModel>
#include <QTableWidgetItem>
#include <QTime>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

namespace {

using namespace darkeye;

struct TwoColumnPage
{
    QWidget *page = nullptr;
    QVBoxLayout *left = nullptr;
    QVBoxLayout *right = nullptr;
};

TwoColumnPage makeTwoColumnPage()
{
    auto *page = new QWidget;
    auto *scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto *content = new QWidget(scroll);
    auto *columns = new QHBoxLayout(content);
    columns->setContentsMargins(16, 16, 16, 16);
    columns->setSpacing(24);

    auto *leftWidget = new QWidget(content);
    leftWidget->setMinimumWidth(320);
    auto *left = new QVBoxLayout(leftWidget);
    left->setSpacing(12);

    auto *rightWidget = new QWidget(content);
    rightWidget->setMinimumWidth(320);
    auto *right = new QVBoxLayout(rightWidget);
    right->setSpacing(12);

    columns->addWidget(leftWidget, 1);
    columns->addWidget(rightWidget, 1);
    scroll->setWidget(content);

    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->addWidget(scroll);
    return {page, left, right};
}

QHBoxLayout *addHorizontalRow(QVBoxLayout *column)
{
    auto *container = new QWidget;
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    column->addWidget(container);
    return layout;
}

class CalloutDemoBox final : public QWidget
{
public:
    explicit CalloutDemoBox(ThemeService *themes, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        m_button = new Button(QStringLiteral("悬停显示 CalloutTooltip"), this);
        m_callout = new CalloutTooltip(themes, this);
        m_timer = new QTimer(this);
        m_timer->setSingleShot(true);
        QObject::connect(m_timer, &QTimer::timeout, this, [this]
                {
                    m_callout->showFor(
                        m_button, QStringLiteral("这是 CalloutTooltip 尖角提示框"));
                });
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(m_button);
        m_button->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_button) {
            if (event->type() == QEvent::Enter) m_timer->start(300);
            else if (event->type() == QEvent::Leave) {
                m_timer->stop();
                m_callout->hide();
            }
        }
        return QWidget::eventFilter(watched, event);
    }

private:
    Button *m_button = nullptr;
    CalloutTooltip *m_callout = nullptr;
    QTimer *m_timer = nullptr;
};

QWidget *buildButtons(ThemeService &themes)
{
    const TwoColumnPage ui = makeTwoColumnPage();
    ui.left->addWidget(new Label(QStringLiteral("设计系统组件 Demo - 按钮")));

    auto *buttonRow = addHorizontalRow(ui.left);
    buttonRow->addWidget(new Button(QStringLiteral("默认按钮")));
    auto *primary = new Button(QStringLiteral("主要按钮"));
    primary->setVariant(QStringLiteral("primary"));
    buttonRow->addWidget(primary);

    ui.left->addWidget(new Label(QStringLiteral("斜角按钮 ChamferButton（令牌驱动）")));
    auto *chamferRow = addHorizontalRow(ui.left);
    for (const QString &name : {QStringLiteral("settings"), QStringLiteral("search"),
                                QStringLiteral("refresh_cw"), QStringLiteral("copy")}) {
        chamferRow->addWidget(new ChamferButton({}, name, 0, 40, 0.22, &themes));
    }

    ui.left->addWidget(new Label(QStringLiteral("状态切换按钮（令牌驱动，随主题变色）")));
    auto *toggleRow = addHorizontalRow(ui.left);
    toggleRow->addWidget(new StateToggleButton(
        QStringLiteral("x"), QStringLiteral("check"), 24, 24, &themes));
    toggleRow->addWidget(new StateToggleButton(
        QStringLiteral("eye"), QStringLiteral("eye_off"), 24, 24, &themes));
    toggleRow->addWidget(new StateToggleButton(
        QStringLiteral("chevron_down"), QStringLiteral("chevron_up"), 24, 24, &themes));

    ui.right->addWidget(new Label(QStringLiteral("图标按钮（令牌驱动，随主题变色）")));
    auto *iconRow = addHorizontalRow(ui.right);
    for (const QString &name : {QStringLiteral("settings"), QStringLiteral("search"),
                                QStringLiteral("refresh_cw"), QStringLiteral("copy"),
                                QStringLiteral("trash_2"), QStringLiteral("save")}) {
        auto *button = new IconPushButton(name, &themes);
        button->setToolTip(name);
        iconRow->addWidget(button);
    }

    ui.right->addWidget(new Label(QStringLiteral("旋转按钮 RotateButton（点击图标旋转 180°）")));
    auto *rotateRow = addHorizontalRow(ui.right);
    rotateRow->addWidget(new RotateButton(QStringLiteral("refresh_cw"), &themes));
    rotateRow->addWidget(new RotateButton(QStringLiteral("settings"), &themes));

    ui.right->addWidget(new Label(QStringLiteral("晃动按钮 ShakeButton（点击左右晃动）")));
    auto *shakeRow = addHorizontalRow(ui.right);
    shakeRow->addWidget(new ShakeButton(QStringLiteral("trash_2"), &themes));
    shakeRow->addWidget(new ShakeButton(QStringLiteral("copy"), &themes));

    ui.left->addStretch();
    ui.right->addStretch();
    return ui.page;
}

QWidget *buildInputs(ThemeService &themes)
{
    const TwoColumnPage ui = makeTwoColumnPage();
    ui.left->addWidget(new Label(QStringLiteral("文本与输入")));
    ui.left->addWidget(new LineEdit);

    auto *placeholder = new LineEdit;
    placeholder->setPlaceholderText(QStringLiteral("占位符示例"));
    ui.left->addWidget(placeholder);

    ui.left->addWidget(new Label(QStringLiteral("多行文本框 TextEdit")));
    auto *textEdit = new TextEdit;
    textEdit->setPlaceholderText(QStringLiteral("多行输入示例…"));
    textEdit->setMinimumHeight(80);
    ui.left->addWidget(textEdit);

    ui.left->addWidget(new Label(QStringLiteral("纯文本多行 PlainTextEdit")));
    auto *plainEdit = new PlainTextEdit;
    plainEdit->setPlaceholderText(QStringLiteral("PlainTextEdit 占位符…"));
    plainEdit->setMinimumHeight(60);
    ui.left->addWidget(plainEdit);

    ui.left->addWidget(new Label(QStringLiteral("带补全 CompleterLineEdit（输入字母过滤）")));
    auto *completer = new CompleterLineEdit([] {
        return QStringList{QStringLiteral("Apple"), QStringLiteral("Banana"),
                           QStringLiteral("Cherry"), QStringLiteral("Date"),
                           QStringLiteral("Elderberry"), QStringLiteral("Fig"),
                           QStringLiteral("Grape")};
    });
    completer->setPlaceholderText(QStringLiteral("输入 a/b/c 等触发补全"));
    ui.left->addWidget(completer);

    ui.right->addWidget(new Label(QStringLiteral("Label tone 变体（normal / inverse）")));
    auto *toneRow = addHorizontalRow(ui.right);
    toneRow->addWidget(new Label(QStringLiteral("普通背景 normal")));
    auto *inverse = new Label(QStringLiteral("反相文字 inverse"));
    inverse->setTone(QStringLiteral("inverse"));
    toneRow->addWidget(inverse);

    ui.right->addWidget(new Label(QStringLiteral("竖排文字 VerticalTextLabel（tone 变体）")));
    auto *verticalRow = addHorizontalRow(ui.right);
    auto *normalVertical = new VerticalTextLabel(
        QStringLiteral("普通竖排\nnormal"), QStringLiteral("normal"), &themes);
    normalVertical->setMinimumHeight(160);
    auto *inverseVertical = new VerticalTextLabel(
        QStringLiteral("反相文字\ninverse"), QStringLiteral("inverse"), &themes);
    inverseVertical->setMinimumHeight(160);
    verticalRow->addWidget(normalVertical);
    verticalRow->addWidget(inverseVertical);

    ui.right->addWidget(new Label(QStringLiteral("TokenListView（随主题变色）")));
    auto *listView = new TokenListView;
    listView->setMinimumHeight(120);
    listView->setModel(new QStringListModel(
        {QStringLiteral("选项 A"), QStringLiteral("选项 B"), QStringLiteral("选项 C"),
         QStringLiteral("选项 D"), QStringLiteral("选项 E")}, listView));
    ui.right->addWidget(listView);

    ui.left->addStretch();
    ui.right->addStretch();
    return ui.page;
}

QWidget *buildToggles(ThemeService &themes)
{
    const TwoColumnPage ui = makeTwoColumnPage();
    ui.left->addWidget(new Label(QStringLiteral("开关 ToggleSwitch（令牌驱动，随主题变色）")));
    auto *switchRow = addHorizontalRow(ui.left);
    switchRow->addWidget(new ToggleSwitch(40, 24, &themes));
    switchRow->addWidget(new Label(QStringLiteral("默认")));
    auto *checkedSwitch = new ToggleSwitch(40, 24, &themes);
    checkedSwitch->setChecked(true);
    switchRow->addWidget(checkedSwitch);
    switchRow->addWidget(new Label(QStringLiteral("默认开")));

    ui.left->addWidget(new Label(QStringLiteral("令牌驱动 RadioButton（随主题变色）")));
    auto *radioGroup = new QButtonGroup(ui.page);
    for (const QString &text : {QStringLiteral("选项 A"), QStringLiteral("选项 B"),
                                QStringLiteral("选项 C")}) {
        auto *radio = new TokenRadioButton(text);
        radioGroup->addButton(radio);
        ui.left->addWidget(radio);
        if (text == QStringLiteral("选项 B")) radio->setChecked(true);
    }

    ui.left->addWidget(new Label(QStringLiteral("令牌驱动 CheckBox（随主题变色）")));
    auto *checkRow = addHorizontalRow(ui.left);
    for (const QString &text : {QStringLiteral("选项一"), QStringLiteral("选项二"),
                                QStringLiteral("选项三")}) {
        auto *check = new TokenCheckBox(text);
        check->setChecked(text == QStringLiteral("选项二"));
        checkRow->addWidget(check);
    }

    ui.left->addStretch();
    ui.right->addStretch();
    return ui.page;
}

QWidget *buildNumericInputs(ThemeService &themes)
{
    const TwoColumnPage ui = makeTwoColumnPage();
    ui.left->addWidget(new Label(QStringLiteral("令牌驱动 DateTimeEdit（随主题变色）")));
    auto *dateTime = new TokenDateTimeEdit;
    dateTime->setDisplayFormat(QStringLiteral("yy-MM-dd HH:mm"));
    // Keep gallery captures reproducible; this page is a visual fixture, not a clock demo.
    dateTime->setDateTime(QDateTime(QDate(2025, 1, 2), QTime(12, 34)));
    dateTime->setCalendarPopup(true);
    dateTime->setMinimumTime(QTime(0, 0));
    dateTime->setMaximumTime(QTime(23, 59));
    ui.left->addWidget(dateTime);

    ui.left->addWidget(new Label(QStringLiteral("令牌驱动 SpinBox（随主题变色）")));
    auto *spinSection = new QWidget;
    auto *spinForm = new QFormLayout(spinSection);
    for (const auto &[label, minimum, maximum, value] :
         QList<std::tuple<QString, int, int, int>>{
             {QStringLiteral("数量"), 0, 999, 10},
             {QStringLiteral("透明度"), 0, 100, 80}}) {
        auto *spin = new TokenSpinBox;
        spin->setRange(minimum, maximum);
        spin->setValue(value);
        spinForm->addRow(new Label(label), spin);
    }
    ui.left->addWidget(spinSection);

    ui.left->addWidget(new Label(
        QStringLiteral("令牌驱动 ClickableSlider（可点击跳转，随主题变色）")));
    auto *sliderSection = new QWidget;
    auto *sliderForm = new QFormLayout(sliderSection);
    for (const auto &[label, value] :
         QList<QPair<QString, int>>{{QStringLiteral("音量"), 60},
                                    {QStringLiteral("亮度"), 80},
                                    {QStringLiteral("对比度"), 50}}) {
        auto *slider = new ClickableSlider(Qt::Horizontal, &themes);
        slider->setRange(0, 100);
        slider->setValue(value);
        sliderForm->addRow(new Label(label), slider);
    }
    ui.left->addWidget(sliderSection);

    ui.right->addWidget(new Label(QStringLiteral("令牌驱动 KeySequenceEdit（快捷键编辑）")));
    auto *keySequence = new TokenKeySequenceEdit;
    keySequence->setClearButtonEnabled(true);
    ui.right->addWidget(keySequence);

    ui.right->addWidget(new Label(QStringLiteral("ProgressBar / IndeterminateProgressBar")));
    auto *progress = new ProgressBar;
    progress->setRange(0, 100);
    progress->setValue(66);
    progress->setFormat(QStringLiteral("Task %p%"));
    ui.right->addWidget(progress);

    auto *busy = new IndeterminateProgressBar;
    ui.right->addWidget(busy);
    auto *busyActions = addHorizontalRow(ui.right);
    auto *start = new Button(QStringLiteral("Start Busy"));
    auto *stop = new Button(QStringLiteral("Stop Busy"));
    QObject::connect(start, &QPushButton::clicked, busy, &IndeterminateProgressBar::start);
    QObject::connect(stop, &QPushButton::clicked, busy, [busy] { busy->stop(60); });
    busyActions->addWidget(start);
    busyActions->addWidget(stop);

    ui.left->addStretch();
    ui.right->addStretch();
    return ui.page;
}

QWidget *buildContainers(ThemeService &themes)
{
    const TwoColumnPage ui = makeTwoColumnPage();
    ui.left->addWidget(new Label(QStringLiteral("令牌驱动 TabWidget（随主题变色）")));
    auto *tabs = new TokenTabWidget;
    tabs->addTab(new Label(QStringLiteral("第一个标签页内容"), tabs), QStringLiteral("概览"));
    tabs->addTab(new Label(QStringLiteral("第二个标签页内容"), tabs), QStringLiteral("详情"));
    tabs->addTab(new Label(QStringLiteral("第三个标签页内容"), tabs), QStringLiteral("设置"));
    ui.left->addWidget(tabs);

    ui.left->addWidget(new Label(QStringLiteral("令牌驱动 GroupBox（随主题变色）")));
    auto *group = new TokenGroupBox(QStringLiteral("分组设置示例"));
    auto *groupLayout = new QVBoxLayout(group);
    groupLayout->setSpacing(6);
    groupLayout->addWidget(new TokenCheckBox(QStringLiteral("启用此功能"), group));
    groupLayout->addWidget(new TokenCheckBox(QStringLiteral("显示高级选项"), group));
    groupLayout->addWidget(new TokenRadioButton(QStringLiteral("模式 A"), group));
    groupLayout->addWidget(new TokenRadioButton(QStringLiteral("模式 B"), group));
    ui.left->addWidget(group);

    ui.left->addWidget(new Label(
        QStringLiteral("TokenLinkCard（令牌驱动边框，点击打开链接）")));
    auto *links = new QWidget;
    auto *linksLayout = new QVBoxLayout(links);
    linksLayout->setContentsMargins(0, 0, 0, 0);
    linksLayout->setSpacing(8);
    linksLayout->addWidget(new TokenLinkCard(
        QStringLiteral("示例项目 A"),
        QStringLiteral("悬停高亮边框；聚焦为 color_border_focus"),
        QStringLiteral("https://example.com"), &themes, links), 0, Qt::AlignHCenter);
    linksLayout->addWidget(new TokenLinkCard(
        QStringLiteral("示例项目 B"), QStringLiteral("回车或空格也会打开链接"),
        QStringLiteral("https://example.org"), &themes, links), 0, Qt::AlignHCenter);
    ui.left->addWidget(links);

    ui.right->addWidget(new Label(QStringLiteral("令牌驱动 TokenTableWidget")));
    auto *table = new TokenTableWidget(4, 3);
    table->setHorizontalHeaderLabels(
        {QStringLiteral("列 A"), QStringLiteral("列 B"), QStringLiteral("列 C")});
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 3; ++column) {
            table->setItem(row, column, new QTableWidgetItem(
                QStringLiteral("(%1,%2)").arg(row).arg(column)));
        }
    }
    table->setMaximumHeight(180);
    ui.right->addWidget(table);

    ui.right->addWidget(new Label(QStringLiteral("TokenTableView (model/view)")));
    auto *tableView = new TokenTableView;
    auto *model = new QStandardItemModel(4, 3, tableView);
    model->setHorizontalHeaderLabels(
        {QStringLiteral("Model A"), QStringLiteral("Model B"), QStringLiteral("Model C")});
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 3; ++column) {
            model->setItem(row, column, new QStandardItem(
                QStringLiteral("R%1C%2").arg(row).arg(column)));
        }
    }
    tableView->setModel(model);
    tableView->setAlternatingRowColors(true);
    tableView->verticalHeader()->setVisible(false);
    tableView->setMaximumHeight(180);
    ui.right->addWidget(tableView);

    ui.right->addWidget(new Label(QStringLiteral("TransparentWidget（透明容器，透出下层）")));
    auto *frame = new QFrame;
    frame->setStyleSheet(
        QStringLiteral("QFrame { background-color: #e0e0e0; border-radius: 8px; }"));
    frame->setFixedHeight(80);
    auto *frameLayout = new QVBoxLayout(frame);
    auto *transparent = new TransparentWidget(frame);
    frameLayout->addWidget(transparent);
    auto *transparentLayout = new QVBoxLayout(transparent);
    transparentLayout->addWidget(
        new Label(QStringLiteral("透明容器内的文字，背景透出父级颜色"), transparent));
    ui.right->addWidget(frame);

    ui.left->addStretch();
    ui.right->addStretch();
    return ui.page;
}

QWidget *buildDataViews(ThemeService &themes)
{
    const TwoColumnPage ui = makeTwoColumnPage();
    ui.left->addWidget(new Label(QStringLiteral("P1: Data and Navigation")));

    ui.left->addWidget(new Label(QStringLiteral("Breadcrumb")));
    auto *breadcrumb = new Breadcrumb(
        {QStringLiteral("Home"), QStringLiteral("Workspace"), QStringLiteral("darkeye_ui")});
    auto *crumbInfo = new Label(QStringLiteral("Current: darkeye_ui"));
    QObject::connect(breadcrumb, &Breadcrumb::crumbClicked, crumbInfo,
            [crumbInfo](int, const QString &text) {
                crumbInfo->setText(QStringLiteral("Current: %1").arg(text));
            });
    ui.left->addWidget(breadcrumb);
    ui.left->addWidget(crumbInfo);

    ui.left->addWidget(new Label(QStringLiteral("SearchBar")));
    auto *search = new SearchBar(QStringLiteral("Search tree items..."));
    auto *searchInfo = new Label(QStringLiteral("Search text: "));
    ui.left->addWidget(search);
    ui.left->addWidget(searchInfo);

    ui.left->addWidget(new Label(QStringLiteral("Pagination")));
    auto *pagination = new Pagination(127, 10);
    pagination->setPageSizeOptions({10, 20, 50});
    auto *paginationInfo = new Label(QStringLiteral("Page 1"));
    QObject::connect(pagination, &Pagination::pageChanged, paginationInfo,
            [paginationInfo](int page) {
                paginationInfo->setText(QStringLiteral("Page %1").arg(page));
            });
    QObject::connect(pagination, &Pagination::pageSizeChanged, paginationInfo,
            [pagination, paginationInfo](int size) {
                paginationInfo->setText(
                    QStringLiteral("Page size %1, page %2")
                        .arg(size).arg(pagination->currentPage()));
            });
    ui.left->addWidget(pagination);
    ui.left->addWidget(paginationInfo);

    ui.right->addWidget(new Label(QStringLiteral("TreeView (Token style)")));
    auto *tree = new TreeView;
    auto *treeModel = new QStandardItemModel(0, 2, tree);
    treeModel->setHorizontalHeaderLabels(
        {QStringLiteral("Name"), QStringLiteral("Value")});

    auto *generalName = new QStandardItem(QStringLiteral("General"));
    generalName->appendRow({new QStandardItem(QStringLiteral("Theme")),
                            new QStandardItem(QStringLiteral("Light"))});
    generalName->appendRow({new QStandardItem(QStringLiteral("Language")),
                            new QStandardItem(QStringLiteral("zh-CN"))});
    treeModel->appendRow({generalName, new QStandardItem});

    auto *pipelineName = new QStandardItem(QStringLiteral("Pipeline"));
    auto *extractName = new QStandardItem(QStringLiteral("Extract"));
    extractName->appendRow({new QStandardItem(QStringLiteral("Frames")),
                            new QStandardItem(QStringLiteral("1024"))});
    extractName->appendRow({new QStandardItem(QStringLiteral("Workers")),
                            new QStandardItem(QStringLiteral("8"))});
    auto *trainName = new QStandardItem(QStringLiteral("Train"));
    trainName->appendRow({new QStandardItem(QStringLiteral("Epoch")),
                          new QStandardItem(QStringLiteral("80"))});
    trainName->appendRow({new QStandardItem(QStringLiteral("Batch")),
                          new QStandardItem(QStringLiteral("32"))});
    pipelineName->appendRow({extractName, new QStandardItem});
    pipelineName->appendRow({trainName, new QStandardItem});
    treeModel->appendRow({pipelineName, new QStandardItem});

    tree->setModel(treeModel);
    tree->expandAll();
    tree->setMinimumHeight(300);
    ui.right->addWidget(tree);

    const auto applyFilter = [tree, treeModel, searchInfo](const QString &text) {
        const QString keyword = text.trimmed().toLower();
        searchInfo->setText(QStringLiteral("Search text: %1").arg(text));
        std::function<bool(QStandardItem *)> matches =
            [&matches, &keyword](QStandardItem *item) {
                if (item == nullptr) return false;
                if (item->text().toLower().contains(keyword)) return true;
                for (int row = 0; row < item->rowCount(); ++row) {
                    for (int column = 0; column < item->columnCount(); ++column) {
                        if (matches(item->child(row, column))) return true;
                    }
                }
                return false;
            };
        for (int row = 0; row < treeModel->rowCount(); ++row) {
            if (keyword.isEmpty()) {
                tree->setRowHidden(row, QModelIndex(), false);
                continue;
            }
            const bool visible = matches(treeModel->item(row, 0))
                || matches(treeModel->item(row, 1));
            tree->setRowHidden(row, QModelIndex(), !visible);
        }
    };
    QObject::connect(search, &SearchBar::searchChanged, ui.page, applyFilter);
    QObject::connect(search, &SearchBar::searchSubmitted, ui.page, applyFilter);
    QObject::connect(search, &SearchBar::clearRequested, ui.page,
            [applyFilter] { applyFilter({}); });
    QObject::connect(search, &SearchBar::filterRequested, ui.page,
            [page = ui.page, &themes] {
                Toast::showMessage(page, QStringLiteral("Filter action entry clicked."),
                                   Toast::Level::Info, 1400, &themes);
            });

    ui.left->addStretch();
    ui.right->addStretch();
    return ui.page;
}

QWidget *buildFeedback(ThemeService &themes, QWidget *anchor)
{
    Q_UNUSED(anchor)
    const TwoColumnPage ui = makeTwoColumnPage();
    ui.left->addWidget(new Label(QStringLiteral("P2: Experience Components")));

    ui.left->addWidget(new Label(QStringLiteral("Skeleton")));
    auto *skeletonLines = new QWidget;
    auto *skeletonLayout = new QVBoxLayout(skeletonLines);
    skeletonLayout->setContentsMargins(0, 0, 0, 0);
    skeletonLayout->setSpacing(6);
    auto *s1 = new Skeleton(12, 6, true, 35, &themes, skeletonLines);
    auto *s2 = new Skeleton(12, 6, true, 35, &themes, skeletonLines);
    auto *s3 = new Skeleton(12, 6, true, 35, &themes, skeletonLines);
    s1->setFixedWidth(280);
    s2->setFixedWidth(220);
    s3->setFixedWidth(250);
    skeletonLayout->addWidget(s1);
    skeletonLayout->addWidget(s2);
    skeletonLayout->addWidget(s3);
    ui.left->addWidget(skeletonLines);

    auto *skeletonActions = addHorizontalRow(ui.left);
    auto *startSkeleton = new Button(QStringLiteral("Start Skeleton"));
    auto *stopSkeleton = new Button(QStringLiteral("Stop Skeleton"));
    QObject::connect(startSkeleton, &QPushButton::clicked, ui.page,
            [s1, s2, s3] { s1->start(); s2->start(); s3->start(); });
    QObject::connect(stopSkeleton, &QPushButton::clicked, ui.page,
            [s1, s2, s3] { s1->stop(); s2->stop(); s3->stop(); });
    skeletonActions->addWidget(startSkeleton);
    skeletonActions->addWidget(stopSkeleton);

    ui.left->addWidget(new Label(QStringLiteral("Tag / Chip")));
    auto *chipRow = addHorizontalRow(ui.left);
    chipRow->addWidget(new Chip(QStringLiteral("Default")));
    chipRow->addWidget(new Chip(QStringLiteral("Info"), QStringLiteral("info")));
    chipRow->addWidget(new Chip(QStringLiteral("Success"), QStringLiteral("success")));
    chipRow->addWidget(new Chip(QStringLiteral("Warning"), QStringLiteral("warning")));
    chipRow->addWidget(new Tag(QStringLiteral("Error"), QStringLiteral("error")));

    auto *filterRow = addHorizontalRow(ui.left);
    auto *image = new Chip(QStringLiteral("Image"), QStringLiteral("default"), true, true);
    auto *video = new Chip(QStringLiteral("Video"), QStringLiteral("default"), true, false);
    auto *audio = new Chip(QStringLiteral("Audio"), QStringLiteral("default"), true, false);
    filterRow->addWidget(image);
    filterRow->addWidget(video);
    filterRow->addWidget(audio);
    auto *filterInfo = new Label(QStringLiteral("Checked: Image"));
    ui.left->addWidget(filterInfo);
    const auto refreshFilter = [image, video, audio, filterInfo] {
        QStringList checked;
        for (Chip *chip : {image, video, audio}) {
            if (chip->isChecked()) checked.append(chip->text());
        }
        filterInfo->setText(QStringLiteral("Checked: %1").arg(
            checked.isEmpty() ? QStringLiteral("None") : checked.join(QStringLiteral(", "))));
    };
    QObject::connect(image, &QPushButton::toggled, ui.page, refreshFilter);
    QObject::connect(video, &QPushButton::toggled, ui.page, refreshFilter);
    QObject::connect(audio, &QPushButton::toggled, ui.page, refreshFilter);

    ui.right->addWidget(new Label(QStringLiteral("EmptyState")));
    auto *empty = new EmptyState(
        QStringLiteral("No Search Result"),
        QStringLiteral("Try changing filters or keywords."),
        QStringLiteral("Reload"));
    empty->setIconText(QStringLiteral("◌"));
    empty->setMinimumHeight(220);
    QObject::connect(empty, &EmptyState::actionTriggered, ui.page,
            [page = ui.page, &themes] {
                Toast::showSuccess(page, QStringLiteral("Reload action triggered."),
                                   &themes, 1500);
            });
    ui.right->addWidget(empty);

    ui.right->addWidget(new Label(QStringLiteral("Avatar / AvatarGroup")));
    auto *avatarRow = addHorizontalRow(ui.right);
    avatarRow->addWidget(new Avatar(QStringLiteral("Dark Eye"), {}, 40, &themes));
    avatarRow->addWidget(new Avatar(QStringLiteral("Ada Lovelace"), {}, 40, &themes));
    avatarRow->addWidget(new Avatar(QStringLiteral("Logo"), {}, 40, &themes));
    ui.right->addWidget(new AvatarGroup(
        {QStringLiteral("Alice"), QStringLiteral("Bob"), QStringLiteral("Cindy"),
         QStringLiteral("David"), QStringLiteral("Eve"), QStringLiteral("Frank")},
        34, 10, 5, &themes));

    ui.left->addStretch();
    ui.right->addStretch();
    return ui.page;
}

QWidget *buildVisuals(ThemeService &themes)
{
    const TwoColumnPage ui = makeTwoColumnPage();
    ui.left->addWidget(new Label(QStringLiteral("CalloutTooltip（悬停显示尖角提示框）")));
    ui.left->addWidget(new CalloutDemoBox(&themes));

    ui.left->addWidget(new Label(QStringLiteral("HeartLabel（爱心喜欢/不喜欢）")));
    auto *heartRow = addHorizontalRow(ui.left);
    heartRow->addWidget(new HeartLabel);
    auto *checkedHeart = new HeartLabel;
    checkedHeart->setState(true);
    heartRow->addWidget(checkedHeart);

    ui.left->addWidget(new Label(QStringLiteral("HeartRatingWidget（1-5 颗心打分）")));
    ui.left->addWidget(new HeartRatingWidget);

    ui.left->addWidget(new Label(
        QStringLiteral("圆形加载指示器 CircularLoading（令牌驱动，随主题变色）")));
    auto *loadingRow = addHorizontalRow(ui.left);
    for (int size : {24, 32, 40}) {
        loadingRow->addWidget(new CircularLoading(size, 3, &themes));
    }

    ui.right->addWidget(new Label(QStringLiteral("OctImage（正八边形图片展示）")));
    const QString logoPath = QDir::current().filePath(QStringLiteral("resources/icons/logo.svg"));
    ui.right->addWidget(new OctImage(logoPath, {}, 120, true));
    ui.right->addWidget(new Label(QStringLiteral("OctImage 无图状态")));
    ui.right->addWidget(new OctImage({}, {}, 80, false));

    ui.left->addWidget(new Label(QStringLiteral("ModalDialog / Dialog")));
    auto *dialogRow = addHorizontalRow(ui.left);
    auto *confirm = new Button(QStringLiteral("Open ModalDialog"));
    QObject::connect(confirm, &QPushButton::clicked, ui.page,
            [page = ui.page, &themes] {
                const bool accepted = ModalDialog::confirm(
                    page, QStringLiteral("Confirm"),
                    QStringLiteral("Run this demo action?"), &themes,
                    QStringLiteral("OK"), QStringLiteral("Cancel"));
                Toast::showMessage(
                    page,
                    QStringLiteral("ModalDialog result: %1").arg(
                        accepted ? QStringLiteral("Accepted") : QStringLiteral("Rejected")),
                    Toast::Level::Info, 1800, &themes);
            });
    auto *danger = new Button(QStringLiteral("Open Dialog Danger"));
    QObject::connect(danger, &QPushButton::clicked, ui.page,
            [page = ui.page, &themes] {
                const bool accepted = Dialog::dangerConfirm(
                    page, QStringLiteral("Danger Action"),
                    QStringLiteral("This is a danger confirm demo."), &themes,
                    QStringLiteral("Delete"), QStringLiteral("Cancel"));
                if (accepted) {
                    Notification::showWarning(
                        page, QStringLiteral("Danger dialog accepted."), &themes, 2000);
                } else {
                    Notification::showMessage(
                        page, QStringLiteral("Danger dialog cancelled."),
                        Notification::Level::Info, 1600, &themes);
                }
            });
    dialogRow->addWidget(confirm);
    dialogRow->addWidget(danger);

    ui.right->addWidget(new Label(QStringLiteral("Toast / Notification")));
    auto *toastRow = addHorizontalRow(ui.right);
    auto *info = new Button(QStringLiteral("Info Toast"));
    QObject::connect(info, &QPushButton::clicked, ui.page,
            [page = ui.page, &themes] {
                Toast::showMessage(page, QStringLiteral("Info message from Toast."),
                                   Toast::Level::Info, 1800, &themes);
            });
    auto *success = new Button(QStringLiteral("Success Toast"));
    QObject::connect(success, &QPushButton::clicked, ui.page,
            [page = ui.page, &themes] {
                Toast::showSuccess(page, QStringLiteral("Success message from Toast."),
                                   &themes, 1800);
            });
    auto *warning = new Button(QStringLiteral("Warning Notification"));
    QObject::connect(warning, &QPushButton::clicked, ui.page,
            [page = ui.page, &themes] {
                Notification::showWarning(
                    page, QStringLiteral("Warning message from Notification."),
                    &themes, 2000);
            });
    auto *error = new Button(QStringLiteral("Error Notification"));
    QObject::connect(error, &QPushButton::clicked, ui.page,
            [page = ui.page, &themes] {
                Notification::showError(
                    page, QStringLiteral("Error message from Notification."),
                    &themes, 2200);
            });
    toastRow->addWidget(info);
    toastRow->addWidget(success);
    toastRow->addWidget(warning);
    toastRow->addWidget(error);

    ui.left->addStretch();
    ui.right->addStretch();
    return ui.page;
}

QWidget *buildAdvanced(ThemeService &themes)
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    layout->addWidget(new Label(QStringLiteral("高级组件（图表、导航与容器）")));
    QHash<QDate, int> activity;
    for (int day = 1; day <= 31; day += 3) {
        activity.insert(QDate(2025, 1, day), (day / 3) % 4 + 1);
    }
    auto *heatmap = new CalendarHeatmap(2025, activity, &themes, page);
    heatmap->setMaximumHeight(155);
    layout->addWidget(heatmap, 0, Qt::AlignHCenter);

    auto *columns = new QHBoxLayout;
    columns->setSpacing(12);
    auto *left = new QVBoxLayout;
    auto *middle = new QVBoxLayout;
    auto *right = new QVBoxLayout;

    left->addWidget(new Label(QStringLiteral("RadarChartWidget")));
    auto *radar = new RadarChartWidget(
        {QStringLiteral("速度"), QStringLiteral("质量"), QStringLiteral("覆盖"),
         QStringLiteral("稳定性"), QStringLiteral("体验")},
        {0.72, 0.86, 1.0, 0.78, 0.68}, {}, 5, &themes);
    radar->setMinimumSize(260, 220);
    left->addWidget(radar);

    left->addWidget(new Label(QStringLiteral("TokenCollapsibleSection")));
    auto *collapsible = new TokenCollapsibleSection(QStringLiteral("展开的示例"), &themes);
    collapsible->addWidget(new Label(QStringLiteral("内容随主题令牌切换"), collapsible));
    collapsible->expand();
    left->addWidget(collapsible);

    middle->addWidget(new Label(QStringLiteral("LazyScrollArea / TokenListWidget")));
    auto *lazy = new LazyScrollArea(120);
    lazy->setMinimumHeight(130);
    lazy->setLoader([](int pageIndex, int) {
        if (pageIndex > 0) return QList<QWidget *>{};
        return QList<QWidget *>{new Label(QStringLiteral("懒加载项目 A")),
                                new Label(QStringLiteral("懒加载项目 B"))};
    });
    middle->addWidget(lazy);
    auto *listWidget = new TokenListWidget;
    listWidget->addItems({QStringLiteral("列表项目 A"), QStringLiteral("列表项目 B"),
                          QStringLiteral("列表项目 C")});
    listWidget->setMaximumHeight(100);
    middle->addWidget(listWidget);

    middle->addWidget(new Label(QStringLiteral("MakerSelector")));
    auto *makers = new MakerSelector(
        {{qint64(1), QStringLiteral("东京热"), QStringLiteral("東京熱"),
          {QStringLiteral("Tokyo Hot")}},
         {qint64(2), QStringLiteral("S1"), {}, {QStringLiteral("S1 NO.1")}}});
    makers->setMaker(qint64(1));
    middle->addWidget(makers);

    right->addWidget(new Label(QStringLiteral("TokenVLabel / TokenVerticalTabBar")));
    auto *verticalRow = new QWidget;
    auto *verticalLayout = new QHBoxLayout(verticalRow);
    verticalLayout->setContentsMargins(0, 0, 0, 0);
    verticalLayout->addWidget(new TokenVLabel(QStringLiteral("令牌标签"), &themes));
    auto *verticalTabs = new TokenVerticalTabBar(&themes);
    verticalTabs->addTab(QStringLiteral("概览"));
    verticalTabs->addTab(QStringLiteral("高级"));
    verticalLayout->addWidget(verticalTabs);
    right->addWidget(verticalRow);

    right->addWidget(new Label(QStringLiteral("ModernScrollMenu")));
    auto *menu = new ModernScrollMenu(
        {{QStringLiteral("第一节"), new Label(QStringLiteral("滚动菜单内容 A"))},
         {QStringLiteral("第二节"), new Label(QStringLiteral("滚动菜单内容 B"))}});
    menu->setMinimumHeight(180);
    right->addWidget(menu);

    columns->addLayout(left, 1);
    columns->addLayout(middle, 1);
    columns->addLayout(right, 1);
    layout->addLayout(columns, 1);
    return page;
}

QWidget *buildColorIcons(ThemeService &themes)
{
    const TwoColumnPage ui = makeTwoColumnPage();
    ui.left->addWidget(new Label(QStringLiteral("颜色选择器 ColorPicker（点击弹出色轮）")));
    auto *colorRow = addHorizontalRow(ui.left);
    colorRow->addWidget(new ColorPicker);
    colorRow->addWidget(new ColorPicker(QColor(QStringLiteral("#FF4081"))));
    colorRow->addWidget(new ColorPicker(
        QColor(QStringLiteral("#4CAF50")), false, ColorPicker::Shape::Rectangle));
    colorRow->addWidget(new ColorPicker(
        QColor(QStringLiteral("#2196F3")), true, ColorPicker::Shape::Octagon));

    ui.right->addWidget(new Label(
        QStringLiteral("内置图标（来自 resources/icons 内联，颜色随主题）")));
    const QStringList names = {
        QStringLiteral("x"), QStringLiteral("check"), QStringLiteral("plus"),
        QStringLiteral("minus"), QStringLiteral("chevron_up"),
        QStringLiteral("chevron_down"), QStringLiteral("chevron_left"),
        QStringLiteral("chevron_right"), QStringLiteral("arrow_up"),
        QStringLiteral("arrow_down"), QStringLiteral("arrow_left"),
        QStringLiteral("arrow_right"), QStringLiteral("settings"),
        QStringLiteral("bell"), QStringLiteral("bell_ring"),
        QStringLiteral("arrow_down_to_line"), QStringLiteral("arrow_up_to_line"),
        QStringLiteral("list_x"), QStringLiteral("list_plus"), QStringLiteral("save"),
        QStringLiteral("share_2"), QStringLiteral("library_big"),
        QStringLiteral("circle_question_mark"), QStringLiteral("circle_plus"),
        QStringLiteral("triangle_up"), QStringLiteral("triangle_down"),
        QStringLiteral("trash_2"), QStringLiteral("database"), QStringLiteral("menu"),
        QStringLiteral("scroll_text"), QStringLiteral("eraser"),
        QStringLiteral("square"), QStringLiteral("brush_cleaning"),
        QStringLiteral("square_pen"), QStringLiteral("eye"), QStringLiteral("eye_off"),
        QStringLiteral("languages"), QStringLiteral("mars"), QStringLiteral("venus"),
        QStringLiteral("film"), QStringLiteral("chart_line"), QStringLiteral("sprout"),
        QStringLiteral("tv"), QStringLiteral("layout_panel_left"),
        QStringLiteral("copy"), QStringLiteral("refresh_cw"),
        QStringLiteral("search"), QStringLiteral("house"), QStringLiteral("love_off"),
        QStringLiteral("love_on"), QStringLiteral("layout_grid"),
        QStringLiteral("layout_waterfall"), QStringLiteral("note_pen"),
        QStringLiteral("funnel"), QStringLiteral("panel_left_open"),
        QStringLiteral("panel_left_close"), QStringLiteral("panel_right_open"),
        QStringLiteral("panel_right_close"), QStringLiteral("link"),
    };
    auto *iconContainer = new QWidget;
    auto *iconFlow = new FlowLayout(iconContainer, 0, 4);
    QList<QPair<QString, Button *>> iconButtons;
    const auto iconColor = [&themes] {
        return QColor(ThemeService::tokens(
            themes.current(), themes.customPrimary()).icon);
    };
    for (const QString &name : names) {
        auto *button = new Button({}, iconContainer);
        button->setIcon(IconProvider::builtIn(name, QSize(24, 24), iconColor()));
        button->setIconSize(QSize(24, 24));
        button->setToolTip(name);
        button->setFixedSize(40, 40);
        iconButtons.append({name, button});
        iconFlow->addWidget(button);
    }
    QObject::connect(&themes, &ThemeService::themeChanged, ui.page,
            [iconButtons, iconColor](ThemeId) {
                const QColor color = iconColor();
                for (const auto &[name, button] : iconButtons) {
                    button->setIcon(IconProvider::builtIn(name, QSize(24, 24), color));
                    button->setIconSize(QSize(24, 24));
                }
            });
    auto *iconScroll = new QScrollArea;
    iconScroll->setWidget(iconContainer);
    iconScroll->setWidgetResizable(true);
    iconScroll->setFrameShape(QFrame::NoFrame);
    iconScroll->setMaximumHeight(220);
    ui.right->addWidget(iconScroll);

    ui.left->addStretch();
    ui.right->addStretch();
    return ui.page;
}

QWidget *buildThemePage(ThemeService &themes)
{
    const TwoColumnPage ui = makeTwoColumnPage();
    ui.left->addWidget(new Label(QStringLiteral("主题切换")));
    auto *selector = new ComboBox;
    selector->setObjectName(QStringLiteral("GalleryThemeSelector"));
    for (const ThemeId theme : ThemeService::availableThemes()) {
        selector->addItem(ThemeService::displayName(theme), static_cast<int>(theme));
    }
    selector->setCurrentIndex(selector->findData(static_cast<int>(themes.current())));
    ui.left->addWidget(selector);

    auto *primaryRow = new QWidget;
    auto *primaryLayout = new QHBoxLayout(primaryRow);
    primaryLayout->setContentsMargins(0, 0, 0, 0);
    primaryLayout->addWidget(
        new Label(QStringLiteral("主色（仅亮色/暗色主题可调）"), primaryRow));
    auto *primary = new ColorPicker(
        QColor(ThemeService::tokens(themes.current(), themes.customPrimary()).primary),
        true, ColorPicker::Shape::Octagon, primaryRow);
    primaryLayout->addWidget(primary);
    ui.left->addWidget(primaryRow);

    const auto updatePrimary = [&themes, primaryRow, primary] {
        const bool editable = themes.current() == ThemeId::Light
            || themes.current() == ThemeId::Dark;
        primaryRow->setEnabled(editable);
        if (editable) {
            QSignalBlocker blocker(primary);
            primary->setColor(ThemeService::tokens(
                themes.current(), themes.customPrimary()).primary);
        }
    };
    QObject::connect(selector, &QComboBox::currentIndexChanged, ui.page,
            [&themes, selector, updatePrimary](int index) {
                themes.setTheme(
                    static_cast<ThemeId>(selector->itemData(index).toInt()));
                updatePrimary();
            });
    QObject::connect(primary, &ColorPicker::colorConfirmed, ui.page,
            [&themes](const QString &color) {
                if (themes.current() == ThemeId::Light
                    || themes.current() == ThemeId::Dark) {
                    themes.setTheme(themes.current(), color);
                }
            });
    updatePrimary();

    ui.left->addWidget(new Label(
        QStringLiteral("切换上方主题可预览设计令牌在各组件上的效果。")));
    ui.left->addStretch();
    ui.right->addStretch();
    return ui.page;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("Darkeye Component Gallery"));
    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption snapshotOption(QStringLiteral("snapshot-dir"),
        QStringLiteral("将每个分组保存为 PNG"), QStringLiteral("directory"));
    QCommandLineOption allThemesOption(QStringLiteral("all-themes"),
        QStringLiteral("与 --snapshot-dir 一起使用，输出全部七套主题"));
    QCommandLineOption customPrimaryOption(QStringLiteral("custom-primary"),
        QStringLiteral("与 --snapshot-dir 一起使用，额外输出 Light 自定义主色快照"),
        QStringLiteral("#RRGGBB"));
    QCommandLineOption smokeOption(QStringLiteral("smoke-test"),
        QStringLiteral("构建全部组件页并在短暂事件循环后退出"));
    parser.addOption(snapshotOption);
    parser.addOption(allThemesOption);
    parser.addOption(customPrimaryOption);
    parser.addOption(smokeOption);
    parser.process(application);

    darkeye::ThemeService themes(application);
    themes.setTheme(darkeye::ThemeId::Light);
    QMainWindow window;
    window.setWindowTitle(QStringLiteral("设计系统组件 Demo"));
    window.setMinimumSize(1080, 720);
    window.resize(1280, 820);
    auto *central = new QWidget(&window);
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    const QList<darkeye::SidebarMenuDefinition> menus = {
        {QStringLiteral("buttons"), QStringLiteral("按钮"), QStringLiteral("square_pen")},
        {QStringLiteral("text"), QStringLiteral("文本输入"), QStringLiteral("scroll_text")},
        {QStringLiteral("toggles"), QStringLiteral("开关选择"), QStringLiteral("check")},
        {QStringLiteral("inputs"), QStringLiteral("数值滑块"), QStringLiteral("list_plus")},
        {QStringLiteral("containers"), QStringLiteral("标签页分组"),
         QStringLiteral("layout_panel_left")},
        {QStringLiteral("data_nav"), QStringLiteral("P1 Data/Nav"),
         QStringLiteral("layout_panel_left")},
        {QStringLiteral("p2_experience"), QStringLiteral("P2 Experience"),
         QStringLiteral("circle_plus")},
        {QStringLiteral("more"), QStringLiteral("更多组件"), QStringLiteral("circle_plus")},
        {QStringLiteral("advanced"), QStringLiteral("高级组件"), QStringLiteral("chart_line")},
        {QStringLiteral("color_icons"), QStringLiteral("颜色图标"), QStringLiteral("copy")},
        {QStringLiteral("theme"), QStringLiteral("主题"), QStringLiteral("refresh_cw")},
    };
    auto *sidebar = new darkeye::Sidebar(menus, &themes, central);
    auto *stack = new QStackedWidget(central);
    stack->setObjectName(QStringLiteral("GalleryPages"));
    const QStringList pageIds = {
        QStringLiteral("buttons"),       QStringLiteral("text"),
        QStringLiteral("toggles"),       QStringLiteral("inputs"),
        QStringLiteral("containers"),    QStringLiteral("data-nav"),
        QStringLiteral("p2-experience"), QStringLiteral("more"),
        QStringLiteral("advanced"),      QStringLiteral("color-icons"),
        QStringLiteral("theme"),
        QStringLiteral("setting"),
    };
    stack->addWidget(buildButtons(themes));
    stack->addWidget(buildInputs(themes));
    stack->addWidget(buildToggles(themes));
    stack->addWidget(buildNumericInputs(themes));
    stack->addWidget(buildContainers(themes));
    stack->addWidget(buildDataViews(themes));
    stack->addWidget(buildFeedback(themes, &window));
    stack->addWidget(buildVisuals(themes));
    stack->addWidget(buildAdvanced(themes));
    stack->addWidget(buildColorIcons(themes));
    stack->addWidget(buildThemePage(themes));
    auto *settingsPage = new QWidget;
    auto *settingsLayout = new QVBoxLayout(settingsPage);
    settingsLayout->setContentsMargins(24, 24, 24, 24);
    auto *settingsTitle = new Label(QStringLiteral("设置"), settingsPage);
    settingsTitle->setTone(QStringLiteral("inverse"));
    settingsLayout->addWidget(settingsTitle);
    settingsLayout->addWidget(
        new Label(QStringLiteral("这里是示例设置页面内容。"), settingsPage));
    settingsLayout->addStretch();
    stack->addWidget(settingsPage);

    QHash<QString, int> pageIndexes;
    for (int index = 0; index < menus.size(); ++index)
    {
        pageIndexes.insert(menus.at(index).id, index);
    }
    pageIndexes.insert(QStringLiteral("setting"), stack->count() - 1);
    QObject::connect(sidebar, &darkeye::Sidebar::itemClicked, &window,
                     [stack, pageIndexes](const QString &menuId)
                     {
                         stack->setCurrentIndex(pageIndexes.value(menuId, 0));
                     });
    sidebar->select(QStringLiteral("buttons"));
    root->addWidget(sidebar);
    root->addWidget(stack, 1);
    window.setCentralWidget(central);
    window.show();

    const QString snapshotDirectory = parser.value(snapshotOption);
    if (!snapshotDirectory.isEmpty())
    {
        QDir().mkpath(snapshotDirectory);
        const bool captureAllThemes = parser.isSet(allThemesOption);
        const QString customPrimary = parser.value(customPrimaryOption);
        QTimer::singleShot(300, &application,
                          [&application, &window, stack, &themes, pageIds,
                           snapshotDirectory, captureAllThemes, customPrimary]
                          {
            const QVector<darkeye::ThemeId> themesToCapture = captureAllThemes
                ? darkeye::ThemeService::availableThemes()
                : QVector<darkeye::ThemeId>{themes.current()};
            for (const darkeye::ThemeId theme : themesToCapture)
            {
                themes.setTheme(theme);
                const QString themeName =
                    darkeye::ThemeService::toSettings(theme).toLower();
                const QString targetDirectory = captureAllThemes
                    ? QDir(snapshotDirectory).filePath(themeName)
                    : snapshotDirectory;
                QDir().mkpath(targetDirectory);
                for (int index = 0; index < stack->count(); ++index)
                {
                    stack->setCurrentIndex(index);
                    QApplication::processEvents();
                    window.grab().save(QDir(targetDirectory).filePath(
                        QStringLiteral("component-gallery-%1.png").arg(pageIds.at(index))));
                }
            }
            if (!customPrimary.isEmpty())
            {
                themes.setTheme(darkeye::ThemeId::Light, customPrimary);
                const QString targetDirectory = QDir(snapshotDirectory).filePath(
                    QStringLiteral("light-custom-primary"));
                QDir().mkpath(targetDirectory);
                for (int index = 0; index < stack->count(); ++index)
                {
                    stack->setCurrentIndex(index);
                    QApplication::processEvents();
                    window.grab().save(QDir(targetDirectory).filePath(
                        QStringLiteral("component-gallery-%1.png").arg(pageIds.at(index))));
                }
            }
            application.quit();
        });
    }
    else if (parser.isSet(smokeOption))
    {
        QTimer::singleShot(300, &application, &QCoreApplication::quit);
    }
    return application.exec();
}
