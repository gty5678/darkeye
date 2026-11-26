#include "ui/layouts/myads/WorkspaceWidget.h"

#include "darkeye_ui/theme/IconProvider.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMimeData>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSplitter>
#include <QScopeGuard>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QHBoxLayout>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace darkeye::myads {

namespace {
class SidebarToggleButton final : public QToolButton
{
public:
    SidebarToggleButton(SidebarSide side, QWidget *parent) : QToolButton(parent)
    {
        constexpr int size = 26;
        constexpr int edgeInset = 6;
        const bool left = side == SidebarSide::Left;
        setFixedSize(size + edgeInset, size);
        setContentsMargins(left ? edgeInset : 0, 0, left ? 0 : edgeInset, 0);
        setIconSize(QSize(18, 18));
    }

    void applyTheme(const DockTheme &theme)
    {
        m_theme = theme;
        update();
    }

protected:
    bool hitButton(const QPoint &position) const override
    {
        return contentsRect().contains(position);
    }

    void paintEvent(QPaintEvent *) override
    {
        const QRect face = contentsRect();
        const QRectF shape = QRectF(face).adjusted(1, 1, -1, -1);
        const qreal left = shape.left();
        const qreal right = shape.right();
        const qreal top = shape.top();
        const qreal bottom = shape.bottom();
        const qreal corner = shape.width() / (2.0 + std::sqrt(2.0));
        const QPolygonF octagon{
            {left + corner, top}, {right - corner, top}, {right, top + corner},
            {right, bottom - corner}, {right - corner, bottom}, {left + corner, bottom},
            {left, bottom - corner}, {left, top + corner},
        };

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(isDown() ? m_theme.closePressed
                                  : (underMouse() || hasFocus()) ? m_theme.closeHover : m_theme.tabBarBackground);
        painter.drawPolygon(octagon);
        const QRect iconRect(face.x() + (face.width() - iconSize().width()) / 2,
                             face.y() + (face.height() - iconSize().height()) / 2,
                             iconSize().width(), iconSize().height());
        icon().paint(&painter, iconRect);
    }

private:
    DockTheme m_theme;
};

class WorkspaceSplitter final : public QSplitter
{
public:
    WorkspaceSplitter(Qt::Orientation orientation, QWidget *parent, const DockTheme &theme)
        : QSplitter(orientation, parent)
    {
        setObjectName(QStringLiteral("MyAdsSplitter"));
        setHandleWidth(2 * DockGap + 1);
        applyTheme(theme);
    }

    void applyTheme(const DockTheme &theme)
    {
        m_background = theme.background;
        m_tabStripBorder = theme.border;
        m_line = theme.splitterLine();
        m_hover = theme.primary;
        m_pressed = theme.primaryHover;
        for (int i = 1; i < count(); ++i) handle(i)->update();
    }

protected:
    QSplitterHandle *createHandle() override { return new Handle(this); }

private:
    class Handle final : public QSplitterHandle
    {
    public:
        explicit Handle(WorkspaceSplitter *owner)
            : QSplitterHandle(owner->orientation(), owner), m_owner(owner) {}

    protected:
        bool event(QEvent *event) override
        {
            if (event->type() == QEvent::UngrabMouse) m_dragging = false;
            if (event->type() == QEvent::Enter || event->type() == QEvent::Leave ||
                event->type() == QEvent::UngrabMouse)
                update();
            return QSplitterHandle::event(event);
        }

        void paintEvent(QPaintEvent *) override
        {
            QPainter painter(this);
            painter.fillRect(rect(), m_owner->m_background);
            const QRect line = orientation() == Qt::Horizontal
                ? QRect(rect().center().x(), 0, 1, height())
                : QRect(0, rect().center().y(), width(), 1);
            QColor color = underMouse() ? m_owner->m_hover : m_owner->m_line;
            if (m_dragging) color = m_owner->m_pressed;
            painter.fillRect(line, color);
            if (orientation() != Qt::Horizontal) return;

            const int center = rect().center().x();
            for (const auto *pane : m_owner->findChildren<PaneWidget *>()) {
                if (!pane->isVisibleTo(m_owner)) continue;
                const QPoint top = pane->mapTo(m_owner, QPoint()) - pos();
                const int y = top.y() + DockTabBarHeight - 1;
                if (y < 0 || y >= height()) continue;

                if (top.x() + pane->width() == 0)
                    painter.fillRect(0, y, center + 1, 1, m_owner->m_tabStripBorder);
                if (top.x() == width())
                    painter.fillRect(center, y, width() - center, 1, m_owner->m_tabStripBorder);
            }
        }

        void mousePressEvent(QMouseEvent *event) override
        {
            QSplitterHandle::mousePressEvent(event);
            if (event->button() == Qt::LeftButton) m_dragging = true;
            update();
        }

        void mouseReleaseEvent(QMouseEvent *event) override
        {
            QSplitterHandle::mouseReleaseEvent(event);
            if (event->button() == Qt::LeftButton) m_dragging = false;
            update();
        }

    private:
        WorkspaceSplitter *m_owner;
        bool m_dragging = false;
    };

    QColor m_background;
    QColor m_tabStripBorder;
    QColor m_line;
    QColor m_hover;
    QColor m_pressed;
};
bool containsWidget(const QWidget *parent, const QWidget *widget)
{
    for (; widget; widget = widget->parentWidget())
        if (widget == parent) return true;
    return false;
}
bool isHorizontalSplit(DropZone zone)
{
    return zone == DropZone::Left || zone == DropZone::Right ||
           zone == DropZone::RootLeft || zone == DropZone::RootRight;
}

bool isVerticalSidebarTree(const std::shared_ptr<LayoutNode> &node)
{
    return node->kind == LayoutNode::Kind::Pane ||
           (node->orientation == Qt::Vertical &&
            std::all_of(node->children.cbegin(), node->children.cend(), isVerticalSidebarTree));
}

LayoutTree singleSidebarPane(const QString &id)
{
    LayoutTree tree;
    tree.root()->orientation = Qt::Vertical;
    tree.addPaneToRoot(id);
    return tree;
}
} // namespace

class WorkspaceWidget::PreviewOverlay final : public QWidget
{
public:
    explicit PreviewOverlay(QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
        hide();
    }

    void showPreview(const QRect &rectangle, const DockTheme &theme, bool insertion = false)
    {
        m_rectangle = rectangle;
        m_theme = theme;
        m_insertion = insertion;
        setGeometry(parentWidget()->rect());
        show();
        raise();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        if (m_insertion) {
            painter.fillRect(m_rectangle, m_theme.primary);
            return;
        }
        QColor fill = m_theme.primary;
        fill.setAlpha(std::clamp(m_theme.previewAlpha, 0, 255));
        painter.setBrush(fill);
        painter.setPen(QPen(m_theme.primary, m_theme.previewBorderWidth));
        painter.drawRect(m_rectangle.adjusted(1, 1, -1, -1));
    }

private:
    QRect m_rectangle;
    DockTheme m_theme;
    bool m_insertion = false;
};

WorkspaceWidget::WorkspaceWidget(QWidget *parent, const DockTheme &theme)
    : WorkspaceWidget(parent, theme, true)
{
}

WorkspaceWidget::WorkspaceWidget(QWidget *parent, const DockTheme &theme, bool withSidebars)
    : QWidget(parent), m_theme(theme)
{
    setObjectName(QStringLiteral("MyAdsWorkspace"));
    setProperty("myadsWorkspace", true);
    setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QHBoxLayout(this);
    updateWorkspaceMargins();
    m_sidebarSplitter = new WorkspaceSplitter(Qt::Horizontal, this, theme);
    m_sidebarSplitter->setObjectName(QStringLiteral("MyAdsSidebarSplitter"));
    m_sidebarSplitter->setChildrenCollapsible(false);
    m_sidebarSplitter->setOpaqueResize(true);
    layout->addWidget(m_sidebarSplitter);
    m_center = new QWidget(m_sidebarSplitter);
    auto *centerLayout = new QHBoxLayout(m_center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    m_sidebarSplitter->addWidget(m_center);
    connect(m_sidebarSplitter, &QSplitter::splitterMoved, this, [this](int, int handle) {
        const auto sizes = m_sidebarSplitter->sizes();
        if (sizes.size() != 3) return;
        const int totalWidth = std::accumulate(sizes.cbegin(), sizes.cend(), 0);
        const int sidebarWidth = handle == 1 ? sizes.first() : sizes.last();
        if (sidebarWidth <= 0 || sidebarWidth >= totalWidth) return;
        auto &ratio = handle == 1 ? m_leftWidthRatio : m_rightWidthRatio;
        ratio = static_cast<double>(sidebarWidth) / totalWidth;
        emit layoutPreferencesChanged();
    });
    m_tree.addPaneToRoot(QStringLiteral("pane_1"));
    rebuild(false);
    if (withSidebars) createSidebars();
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *old, QWidget *now) {
        if (m_layoutTransitionInProgress) return;
        for (auto side : {SidebarSide::Left, SidebarSide::Right}) {
            auto *host = sidebar(side).host;
            if (sidebarAutoCollapse(side) && host && !host->isHidden()
                && containsWidget(host, old) && !containsWidget(host, now)) {
                QTimer::singleShot(0, host, [this, side, host] {
                    if (!m_layoutTransitionInProgress && sidebarAutoCollapse(side)
                        && !containsWidget(host, QApplication::focusWidget())
                        && !containsWidget(host, QApplication::activePopupWidget()))
                        hideSidebar(host);
                });
            }
        }
    });
    m_preview = new PreviewOverlay(this);
    applyTheme(theme);
    qApp->installEventFilter(this);
}

WorkspaceWidget::~WorkspaceWidget()
{
    if (qApp) {
        disconnect(qApp, &QApplication::focusChanged, this, nullptr);
        qApp->removeEventFilter(this);
    }
}

const LayoutTree &WorkspaceWidget::layoutTree() const noexcept { return m_tree; }
PaneWidget *WorkspaceWidget::pane(const QString &id) const { return m_panes.value(id); }

QList<PaneWidget *> WorkspaceWidget::panes() const
{
    QList<PaneWidget *> result;
    for (const auto &id : m_tree.paneIds())
        if (auto *item = pane(id)) result.append(item);
    return result;
}

void WorkspaceWidget::createSidebars(LayoutTree left, LayoutTree right)
{
    const auto create = [this](Sidebar &sidebar, LayoutTree tree, const QString &name,
                               const QString &label) {
        if (tree.paneIds().isEmpty()) {
            QString id = name;
            for (int suffix = 2; pane(id); ++suffix)
                id = name + QString::number(suffix);
            tree = singleSidebarPane(id);
        }
        sidebar.tree = std::move(tree);
        sidebar.host = new QWidget(m_sidebarSplitter);
        sidebar.host->setObjectName(name);
        sidebar.host->setAccessibleName(label);
        sidebar.host->setMinimumWidth(80);
        auto *layout = new QHBoxLayout(sidebar.host);
        layout->setContentsMargins(0, 0, 0, 0);
        sidebar.host->hide();
        rebuild(sidebar.tree, false);
    };
    create(m_leftSidebar, std::move(left), QStringLiteral("MyAdsLeftSidebar"), tr("左边栏"));
    create(m_rightSidebar, std::move(right), QStringLiteral("MyAdsRightSidebar"), tr("右边栏"));
    m_sidebarSplitter->insertWidget(0, m_leftSidebar.host);
    m_sidebarSplitter->addWidget(m_rightSidebar.host);
}

void WorkspaceWidget::applySidebarWidths()
{
    if (!m_leftSidebar.host || !m_rightSidebar.host) return;
    const double left = m_leftSidebar.host->isHidden() ? 0.0 : m_leftWidthRatio;
    const double right = m_rightSidebar.host->isHidden() ? 0.0 : m_rightWidthRatio;
    const double center = left + right < 1.0 ? 1.0 - left - right : 0.10;
    constexpr int scale = 10000;
    m_sidebarSplitter->setSizes({qRound(left * scale), qMax(1, qRound(center * scale)),
                                 qRound(right * scale)});
}

const WorkspaceWidget::Sidebar &WorkspaceWidget::sidebar(SidebarSide side) const
{
    return side == SidebarSide::Left ? m_leftSidebar : m_rightSidebar;
}

PaneWidget *WorkspaceWidget::sidebarPane(SidebarSide side) const
{
    return pane(sidebar(side).tree.paneIds().value(0));
}

const WorkspaceWidget::Sidebar *WorkspaceWidget::sidebarFor(const QString &paneId) const
{
    for (const auto *sidebar : {&m_leftSidebar, &m_rightSidebar})
        if (sidebar->tree.paneIds().contains(paneId)) return sidebar;
    return nullptr;
}

LayoutTree &WorkspaceWidget::treeFor(const QString &paneId)
{
    if (m_leftSidebar.tree.paneIds().contains(paneId)) return m_leftSidebar.tree;
    if (m_rightSidebar.tree.paneIds().contains(paneId)) return m_rightSidebar.tree;
    return m_tree;
}

bool WorkspaceWidget::isSidebar(const PaneWidget *item) const
{
    return item && pane(item->paneId()) == item && sidebarFor(item->paneId());
}

bool WorkspaceWidget::isSidebarOpen(SidebarSide side) const
{
    auto *host = sidebar(side).host;
    return host && !host->isHidden();
}

QList<QString> WorkspaceWidget::visiblePaneIds() const
{
    QList<QString> ids;
    if (isSidebarOpen(SidebarSide::Left)) ids.append(m_leftSidebar.tree.paneIds());
    ids.append(m_tree.paneIds());
    if (isSidebarOpen(SidebarSide::Right)) ids.append(m_rightSidebar.tree.paneIds());
    return ids;
}

void WorkspaceWidget::toggleSidebar(SidebarSide side)
{
    auto *item = sidebarPane(side);
    if (!item) return;
    if (isSidebarOpen(side))
        hideSidebar(sidebar(side).host);
    else
        setActivePane(item);
}

void WorkspaceWidget::setSidebarOpen(SidebarSide side, bool open)
{
    auto *host = sidebar(side).host;
    if (!host || open == isSidebarOpen(side)) return;
    if (!open) {
        hideSidebar(host);
        return;
    }
    host->show();
    applySidebarWidths();
    placeSidebarToggles();
    updateSidebarToggles();
}

void WorkspaceWidget::hideSidebar(QWidget *host)
{
    if ((host != m_leftSidebar.host && host != m_rightSidebar.host) || host->isHidden()) return;
    const bool wasActive = containsWidget(host, pane(m_activePaneId));
    if (containsWidget(host, window()->focusWidget())) {
        // Move focus before hiding the focused widget triggers Qt's tab-order fallback.
        if (auto *center = rootPane()) center->focusTabBar();
    }
    host->hide();
    placeSidebarToggles();
    updateSidebarToggles();
    applySidebarWidths();
    if (wasActive && containsWidget(host, pane(m_activePaneId))) setActivePane(rootPane());
}

void WorkspaceWidget::setSidebarToggleVisible(SidebarSide side, bool visible)
{
    const bool left = side == SidebarSide::Left;
    auto *&button = left ? m_leftSidebarToggle : m_rightSidebarToggle;
    if (!button) {
        button = new SidebarToggleButton(side, this);
        button->setObjectName(left ? QStringLiteral("MyAdsLeftSidebarToggle")
                                   : QStringLiteral("MyAdsRightSidebarToggle"));
        button->setProperty("skipPaneActivationOnMouseFocus", true);
        button->setFocusPolicy(Qt::TabFocus);
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &QToolButton::clicked, this, [this, side] { toggleSidebar(side); });
        placeSidebarToggles();
        updateSidebarToggles();
    }
    button->setVisible(visible);
}

void WorkspaceWidget::placeSidebarToggles()
{
    const auto place = [this](QToolButton *button, SidebarSide side) {
        if (!button) return;
        const auto edge = side == SidebarSide::Left ? Qt::LeftEdge : Qt::RightEdge;
        auto node = isSidebarOpen(side) ? sidebar(side).tree.root() : m_tree.root();
        while (node->kind == LayoutNode::Kind::Split) {
            if (node->children.isEmpty()) return;
            node = edge == Qt::RightEdge && node->orientation == Qt::Horizontal
                ? node->children.last() : node->children.first();
        }
        if (auto *target = pane(node->paneId)) {
            const bool visible = !button->isHidden();
            target->addTabBarButton(button, edge);
            button->setVisible(visible);
        }
    };
    place(m_leftSidebarToggle, SidebarSide::Left);
    place(m_rightSidebarToggle, SidebarSide::Right);
}

void WorkspaceWidget::updateSidebarToggles()
{
    const auto update = [this](QToolButton *button, SidebarSide side) {
        if (!button) return;
        const bool left = side == SidebarSide::Left;
        const bool open = isSidebarOpen(side);
        const QString label = left
            ? (open ? tr("收起左边栏") : tr("展开左边栏"))
            : (open ? tr("收起右边栏") : tr("展开右边栏"));
        static_cast<SidebarToggleButton *>(button)->applyTheme(m_theme);
        button->setToolTip(label);
        button->setAccessibleName(label);

        const QString icon = left
            ? (open ? QStringLiteral("panel_left_close") : QStringLiteral("panel_left_open"))
            : (open ? QStringLiteral("panel_right_close") : QStringLiteral("panel_right_open"));
        button->setIcon(IconProvider::builtIn(icon, button->iconSize(), m_theme.closeIcon,
                                              button->devicePixelRatioF()));
    };
    update(m_leftSidebarToggle, SidebarSide::Left);
    update(m_rightSidebarToggle, SidebarSide::Right);
}

bool WorkspaceWidget::sidebarAutoCollapse(SidebarSide side) const
{
    return side == SidebarSide::Left ? m_leftSidebarAutoCollapse : m_rightSidebarAutoCollapse;
}

void WorkspaceWidget::setSidebarAutoCollapse(SidebarSide side, bool enabled)
{
    auto &current = side == SidebarSide::Left ? m_leftSidebarAutoCollapse : m_rightSidebarAutoCollapse;
    if (current == enabled) return;
    current = enabled;
    auto *host = sidebar(side).host;
    if (enabled && host && !containsWidget(host, QApplication::focusWidget()))
        hideSidebar(host);
}

PaneWidget *WorkspaceWidget::rootPane() const
{
    const QList<QString> ids = m_tree.paneIds();
    return ids.isEmpty() ? nullptr : m_panes.value(ids.first());
}

PaneWidget *WorkspaceWidget::activePane() const
{
    PaneWidget *result = pane(m_activePaneId);
    const auto *sidebar = sidebarFor(m_activePaneId);
    return result && (!sidebar || !sidebar->host->isHidden()) ? result : rootPane();
}

void WorkspaceWidget::setActivePane(PaneWidget *target)
{
    if (!target || pane(target->paneId()) != target) return;
    if (const auto *sidebar = sidebarFor(target->paneId()); sidebar && sidebar->host->isHidden()) {
        sidebar->host->show();
        applySidebarWidths();
        // Give the opened pane focus before reparenting its toggle button.
        target->focusTabBar();
        placeSidebarToggles();
        updateSidebarToggles();
    }
    if (m_activePaneId == target->paneId()) return;
    m_activePaneId = target->paneId();
    emit activePaneChanged(m_activePaneId);
}

void WorkspaceWidget::updateWorkspaceMargins()
{
    for (auto *parent = parentWidget(); parent; parent = parent->parentWidget()) {
        if (qobject_cast<WorkspaceWidget *>(parent)) {
            layout()->setContentsMargins(0, 0, 0, 0);
            return;
        }
    }
    layout()->setContentsMargins(DockGap, DockGap, DockGap, DockGap);
}

bool WorkspaceWidget::eventFilter(QObject *watched, QEvent *event)
{
    QWidget *receiver = qobject_cast<QWidget *>(watched);
    if (event->type() == QEvent::ParentChange && receiver &&
        (receiver == this || receiver->isAncestorOf(this)))
        updateWorkspaceMargins();
    if (receiver == m_sidebarSplitter && event->type() == QEvent::Resize)
        QTimer::singleShot(0, this, &WorkspaceWidget::applySidebarWidths);
    if ((event->type() == QEvent::Move || event->type() == QEvent::Resize ||
         event->type() == QEvent::Show || event->type() == QEvent::Hide) &&
        receiver && isAncestorOf(receiver) &&
        (qobject_cast<PaneWidget *>(receiver) || dynamic_cast<WorkspaceSplitter *>(receiver) ||
         qobject_cast<WorkspaceWidget *>(receiver))) {
        for (auto *child = receiver; child != this; child = child->parentWidget()) {
            auto *splitter = dynamic_cast<WorkspaceSplitter *>(child->parentWidget());
            if (!splitter || splitter->orientation() != Qt::Horizontal) continue;
            const int index = splitter->indexOf(child);
            if (index > 0) splitter->handle(index)->update();
            if (index >= 0 && index + 1 < splitter->count()) splitter->handle(index + 1)->update();
        }
    }
    if (event->type() == QEvent::DragLeave && receiver && receiver == m_dockDragTarget) {
        m_dockDragTarget.clear();
        hideDropPreview();
        event->accept();
        return true;
    }
    if (receiver && (event->type() == QEvent::DragEnter ||
                     event->type() == QEvent::DragMove || event->type() == QEvent::Drop)) {
        PaneWidget *target = nullptr;
        for (QWidget *widget = receiver; widget && widget != this; widget = widget->parentWidget()) {
            if (auto *candidate = qobject_cast<PaneWidget *>(widget)) {
                if (pane(candidate->paneId()) == candidate) target = candidate;
                break;
            }
        }
        auto *drop = static_cast<QDropEvent *>(event);
        if (target && drop->mimeData()->hasFormat(MyAdsTabMimeType)) {
            // Child views (for example QGraphicsView) accept drops themselves.
            // Route dock drags here before those views pass them to their scenes.
            const QJsonObject payload = QJsonDocument::fromJson(
                drop->mimeData()->data(MyAdsTabMimeType)).object();
            const QString sourceId = payload.value(QStringLiteral("pane_id")).toString();
            const QString contentId = payload.value(QStringLiteral("content_id")).toString();
            PaneWidget *source = pane(sourceId);
            if (!source || !source->contentWidget(contentId) ||
                !(drop->possibleActions() & Qt::MoveAction)) {
                drop->ignore();
                hideDropPreview();
                return true;
            }
            const QPoint global = receiver->mapToGlobal(drop->position().toPoint());
            if (event->type() == QEvent::Drop) {
                m_dockDragTarget.clear();
                const bool moved = moveContent(sourceId, contentId, target->paneId(),
                                               dropZone(target, global),
                                               target->tabInsertionIndex(global));
                hideDropPreview();
                if (!moved) {
                    drop->ignore();
                    return true;
                }
            } else {
                m_dockDragTarget = receiver;
                handleDragMove(target, global);
            }
            drop->setDropAction(Qt::MoveAction);
            drop->accept();
            return true;
        }
    }
    if (!m_layoutTransitionInProgress && event->type() == QEvent::MouseButtonPress &&
        receiver && receiver->window() == window()) {
        for (auto side : {SidebarSide::Left, SidebarSide::Right}) {
            auto *host = sidebar(side).host;
            const bool left = side == SidebarSide::Left;
            if (sidebarAutoCollapse(side) && host && !host->isHidden() && !containsWidget(host, receiver)
                && receiver != (left ? m_leftSidebarToggle : m_rightSidebarToggle)
                && receiver != m_sidebarSplitter->handle(left ? 1 : 2))
                QTimer::singleShot(0, host, [this, side, host] {
                    if (!m_layoutTransitionInProgress && sidebarAutoCollapse(side)) hideSidebar(host);
                });
        }
    }
    const bool mousePress = event->type() == QEvent::MouseButtonPress;
    const bool focusIn = event->type() == QEvent::FocusIn;
    const bool skipMouseActivation = (mousePress || focusIn) && receiver
        && receiver->property("skipPaneActivationOnMouseFocus").toBool()
        && (mousePress ? static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton
                       : static_cast<QFocusEvent *>(event)->reason() == Qt::MouseFocusReason);
    if (!m_layoutTransitionInProgress && (mousePress || focusIn) && !skipMouseActivation) {
        // A page can contain its own workspace. Keep walking until we reach a pane owned here.
        for (auto *widget = receiver; widget && widget != this; widget = widget->parentWidget()) {
            auto *target = qobject_cast<PaneWidget *>(widget);
            if (target && pane(target->paneId()) == target) {
                setActivePane(target);
                break;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

PaneWidget *WorkspaceWidget::findPaneByContentId(const QString &contentId) const
{
    for (PaneWidget *item : m_panes)
        if (item->contentIds().contains(contentId)) return item;
    return nullptr;
}

QString WorkspaceWidget::nextPaneId() const
{
    const auto ids = m_tree.paneIds();
    for (int i = 1;; ++i) {
        const QString candidate = QStringLiteral("pane_%1").arg(i);
        if (!ids.contains(candidate) && !pane(candidate)) return candidate;
    }
}

QString WorkspaceWidget::nextContentId()
{
    QString candidate;
    do candidate = QStringLiteral("content_%1").arg(++m_contentCounter);
    while (findPaneByContentId(candidate));
    return candidate;
}

ContentConfig WorkspaceWidget::createContentConfig(const QString &id)
{
    return {.contentId = id.isEmpty() ? nextContentId() : id};
}

bool WorkspaceWidget::fillPane(PaneWidget *target, const ContentConfig &content)
{
    if (!target || pane(target->paneId()) != target) return false;
    QString id = content.contentId.isEmpty() ? nextContentId() : content.contentId;
    if (findPaneByContentId(id)) id = nextContentId();
    QWidget *widget = content.widget;
    if (!widget) {
        widget = new QLabel(content.title.isEmpty() ? id : content.title);
        qobject_cast<QLabel *>(widget)->setAlignment(Qt::AlignCenter);
    }
    return target->addContent(id, content.title.isEmpty() ? id : content.title,
                              widget, content.icon, content.closeable, -1, content.closePolicy);
}

PaneWidget *WorkspaceWidget::createPane(const QString &id, bool sidebar)
{
    if (PaneWidget *existing = m_reusablePanes.take(id)) {
        m_panes.insert(id, existing);
        return existing;
    }
    if (auto *existing = pane(id)) return existing;
    auto *result = new PaneWidget(id, this, sidebar);
    connectPane(result);
    m_panes.insert(id, result);
    return result;
}

void WorkspaceWidget::connectPane(PaneWidget *result)
{
    result->applyTheme(m_theme);
    connect(result, &PaneWidget::activationRequested, this, [this, result] {
        setActivePane(result);
    });
    connect(result, &PaneWidget::currentContentChanged, this, [this, result] {
        if (result == activePane()) emit activeContentChanged();
        emit layoutPreferencesChanged();
    });
    connect(result, &PaneWidget::layoutPreferencesChanged, this, &WorkspaceWidget::layoutPreferencesChanged);
    connect(result, &PaneWidget::tabContextMenuCreated, this, &WorkspaceWidget::tabContextMenuCreated);
    connect(result, &PaneWidget::tabContextMenuCreated, this,
            [this, result](QMenu *menu, const QString &contentId) {
        QMenu *moveMenu = menu->addMenu(tr("移动到其他组"));
        moveMenu->setObjectName(QStringLiteral("moveTabToGroup"));
        const QString sourceId = result->paneId();
        const auto ids = m_tree.paneIds();
        for (int i = 0; i < ids.size(); ++i) {
            if (ids[i] == sourceId) continue;
            PaneWidget *target = pane(ids[i]);
            if (!target) continue;
            QString title = target->contentTitle(target->currentContentId());
            title.replace(QLatin1Char('&'), QStringLiteral("&&"));
            const QString label = title.isEmpty() ? tr("组 %1").arg(i + 1)
                : tr("组 %1：%2").arg(i + 1).arg(title);
            QAction *action = moveMenu->addAction(label);
            action->setData(ids[i]);
            connect(action, &QAction::triggered, this,
                    [this, sourceId, contentId, targetId = ids[i]] {
                moveContent(sourceId, contentId, targetId, DropZone::Center);
            });
        }
        for (const auto *sidebar : {&m_leftSidebar, &m_rightSidebar}) {
            if (!sidebar->host || sidebar->host->isHidden()) continue;
            const auto sidebarPaneIds = sidebar->tree.paneIds();
            for (int i = 0; i < sidebarPaneIds.size(); ++i) {
                if (sidebarPaneIds[i] == sourceId) continue;
                const auto name = sidebar->host->accessibleName();
                const auto label = sidebarPaneIds.size() == 1 ? name : tr("%1 · 组 %2").arg(name).arg(i + 1);
                QAction *action = moveMenu->addAction(label);
                action->setData(sidebarPaneIds[i]);
                connect(action, &QAction::triggered, this,
                        [this, sourceId, contentId, targetId = sidebarPaneIds[i]] {
                    moveContent(sourceId, contentId, targetId, DropZone::Center);
                });
            }
        }
        moveMenu->setEnabled(!moveMenu->actions().isEmpty());
    });
    connect(result, &PaneWidget::contentClosed, this, &WorkspaceWidget::contentClosed);
    connect(result, &PaneWidget::paneEmpty, this,
            [this](PaneWidget *empty) { removeEmptyPane(empty->paneId()); });
    connect(result, &PaneWidget::dragMoved, this, &WorkspaceWidget::handleDragMove);
    connect(result, &PaneWidget::tabDropped, this, &WorkspaceWidget::handleDrop);
    connect(result, &PaneWidget::dragFinished, this, [this] {
        if (m_preview) m_preview->hide();
    });
}

QWidget *WorkspaceWidget::buildNode(const std::shared_ptr<LayoutNode> &node, bool sidebar)
{
    if (node->kind == LayoutNode::Kind::Pane) return createPane(node->paneId, sidebar);
    auto *splitter = new WorkspaceSplitter(node->orientation, this, m_theme);
    splitter->setChildrenCollapsible(false);
    splitter->setOpaqueResize(true);
    for (const auto &child : node->children) splitter->addWidget(buildNode(child, sidebar));
    const QList<int> requestedSizes = node->sizes;
    const auto validSizes = [splitter](const QList<int> &sizes) {
        return sizes.size() == splitter->count() &&
               std::all_of(sizes.cbegin(), sizes.cend(), [](int size) { return size > 0; });
    };
    if (validSizes(requestedSizes)) splitter->setSizes(requestedSizes);
    // 新建 splitter 时父布局还没有最终几何尺寸。此时 setSizes 可能被 Qt 的一次
    // 零尺寸布局覆盖；在事件循环下一轮重新应用保存的比例。
    if (validSizes(requestedSizes)) {
        QTimer::singleShot(0, splitter, [splitter, requestedSizes] {
            splitter->setSizes(requestedSizes);
        });
    }
    connect(splitter, &QSplitter::splitterMoved, this,
            [this, splitter, node, validSizes](int, int) {
                const QList<int> sizes = splitter->sizes();
                // 忽略父级重建/隐藏期间的零尺寸通知，不能让它污染可持久化布局。
                if (validSizes(sizes)) {
                    node->sizes = sizes;
                    emit layoutPreferencesChanged();
                }
            });
    return splitter;
}

void WorkspaceWidget::connectSplitters(QWidget *widget, const std::shared_ptr<LayoutNode> &node)
{
    if (node->kind != LayoutNode::Kind::Split) return;
    auto *splitter = qobject_cast<QSplitter *>(widget);
    Q_ASSERT(splitter);
    connect(splitter, &QSplitter::splitterMoved, this, [this, splitter, node] {
        const QList<int> sizes = splitter->sizes();
        if (sizes.size() == splitter->count() &&
            std::all_of(sizes.cbegin(), sizes.cend(), [](int size) { return size > 0; })) {
            node->sizes = sizes;
            emit layoutPreferencesChanged();
        }
    });
    for (int i = 0; i < splitter->count(); ++i)
        connectSplitters(splitter->widget(i), node->children.at(i));
}

void WorkspaceWidget::rebuild(bool preserveContents)
{
    rebuild(m_tree, preserveContents);
}

void WorkspaceWidget::rebuild(LayoutTree &tree, bool preserveContents)
{
    auto *sidebar = &tree == &m_leftSidebar.tree ? &m_leftSidebar
                  : &tree == &m_rightSidebar.tree ? &m_rightSidebar : nullptr;
    auto *host = sidebar ? sidebar->host : m_center;
    auto *&root = sidebar ? sidebar->root : m_rootWidget;
    QWidget *oldRoot = root;
    const QPointer<QWidget> focus = QApplication::focusWidget();
    const bool restoreFocus = sidebar && containsWidget(oldRoot, focus);
    for (auto it = m_panes.begin(); it != m_panes.end();) {
        if (!oldRoot || !containsWidget(oldRoot, it.value())) {
            ++it;
            continue;
        }
        if (preserveContents) m_reusablePanes.insert(it.key(), it.value());
        it = m_panes.erase(it);
    }
    if (oldRoot) host->layout()->removeWidget(oldRoot);
    // Keep the single-pane sidebar as a bare pane. Its tree still has a
    // vertical root, so the next split can use the regular LayoutTree API.
    const auto node = tree.root();
    root = sidebar && node->children.size() == 1
        ? buildNode(node->children.first(), true) : buildNode(node, sidebar != nullptr);
    host->layout()->addWidget(root);
    placeSidebarToggles();
    auto *oldPane = qobject_cast<PaneWidget *>(oldRoot);
    if (oldRoot && oldRoot != root && (!oldPane || pane(oldPane->paneId()) != oldPane)) {
        oldRoot->hide();
        oldRoot->deleteLater();
    }
    m_reusablePanes.clear();
    root->show();
    if (restoreFocus && focus && containsWidget(root, focus)) {
        focus->setFocus();
    } else if (sidebar && !host->isHidden() && (restoreFocus || !pane(m_activePaneId))) {
        auto *remaining = pane(tree.paneIds().first());
        setActivePane(remaining);
        if (restoreFocus) remaining->focusTabBar();
    }
    if (!pane(m_activePaneId)) {
        const QList<QString> ids = m_tree.paneIds();
        const QString nextActiveId = ids.isEmpty() ? QString() : ids.first();
        if (m_activePaneId != nextActiveId) {
            m_activePaneId = nextActiveId;
            emit activePaneChanged(m_activePaneId);
        }
    }
    if (m_preview) m_preview->raise();
}

void WorkspaceWidget::beginLayoutUpdate()
{
    ++m_layoutUpdateDepth;
}

void WorkspaceWidget::endLayoutUpdate()
{
    if (m_layoutUpdateDepth <= 0) return;
    --m_layoutUpdateDepth;
    if (m_layoutUpdateDepth != 0 || m_pendingLayoutRebuilds.isEmpty()) return;

    const auto pending = std::exchange(m_pendingLayoutRebuilds, {});
    for (auto *tree : pending) rebuild(*tree);
    setActivePane(pane(m_activePaneId));
    emit layoutChanged();
}

PaneWidget *WorkspaceWidget::split(PaneWidget *target, Placement placement, int percent)
{
    if (!target || pane(target->paneId()) != target) return nullptr;
    const QString id = nextPaneId();
    const bool before = placement == Placement::Left || placement == Placement::Top;
    const Qt::Orientation orientation =
        placement == Placement::Left || placement == Placement::Right
            ? Qt::Horizontal : Qt::Vertical;
    auto &tree = treeFor(target->paneId());
    const bool sidebar = &tree != &m_tree;
    if (sidebar && orientation == Qt::Horizontal) return nullptr;
    tree.split(target->paneId(), orientation, before, id, percent);
    if (m_layoutUpdateDepth > 0) {
        // Python's LayoutTree mutates the live tree incrementally.  During a
        // programmatic batch, register the new pane immediately so later
        // splits can target it, but defer rebuilding the QWidget tree until
        // the batch is complete.
        PaneWidget *created = createPane(id, sidebar);
        m_activePaneId = id;
        m_pendingLayoutRebuilds.insert(&tree);
        return created;
    }
    // 拆分会重建树，但已有窗格（及其中的内容 widget）必须继续复用；否则调用方
    // 持有的 PaneWidget 指针会在连续拆分中失效，并可能在销毁工作区时重复释放布局。
    rebuild(tree);
    setActivePane(pane(id));
    emit layoutChanged();
    return pane(id);
}

void WorkspaceWidget::removeEmptyPane(const QString &id)
{
    auto &tree = treeFor(id);
    if (tree.paneIds().size() <= 1) { emit layoutChanged(); return; }
    if (tree.removePane(id)) {
        rebuild(tree);
        emit layoutChanged();
    }
}

DropZone WorkspaceWidget::hitTest(const QRect &rectangle, const QPoint &position)
{
    if (rectangle.width() <= 0 || rectangle.height() <= 0) return DropZone::Center;
    const double x = static_cast<double>(position.x() - rectangle.left()) / rectangle.width();
    const double y = static_cast<double>(position.y() - rectangle.top()) / rectangle.height();
    if (x >= 0.25 && x < 0.75 && y >= 0.25 && y < 0.75) return DropZone::Center;
    if (y < 0.25) return DropZone::Top;
    if (y >= 0.75) return DropZone::Bottom;
    if (x < 0.25) return DropZone::Left;
    return DropZone::Right;
}

DropZone WorkspaceWidget::dropZone(PaneWidget *target, const QPoint &global) const
{
    // The tab strip takes priority over pane/root edge splitting.
    if (target->tabInsertionIndex(global) >= 0) return DropZone::Center;
    if (isSidebar(target)) {
        const auto body = target->contentRect();
        const int y = target->mapFromGlobal(global).y() - body.top();
        if (y < body.height() / 4) return DropZone::Top;
        if (y >= body.height() * 3 / 4) return DropZone::Bottom;
        return DropZone::Center;
    }
    const QPoint local = m_rootWidget->mapFromGlobal(global);
    const double x = static_cast<double>(local.x()) / std::max(1, m_rootWidget->width());
    const double y = static_cast<double>(local.y()) / std::max(1, m_rootWidget->height());
    if (x >= 0.30 && x < 0.70 && y < 0.05) return DropZone::RootTop;
    if (x >= 0.30 && x < 0.70 && y >= 0.95) return DropZone::RootBottom;
    if (y >= 0.30 && y < 0.70 && x < 0.05) return DropZone::RootLeft;
    if (y >= 0.30 && y < 0.70 && x >= 0.95) return DropZone::RootRight;
    return hitTest(target->rect(), target->mapFromGlobal(global));
}

QRect WorkspaceWidget::previewRect(PaneWidget *target, DropZone zone) const
{
    const bool root = zone == DropZone::RootLeft || zone == DropZone::RootRight ||
                      zone == DropZone::RootTop || zone == DropZone::RootBottom;
    const auto *sidebar = sidebarFor(target->paneId());
    const QWidget *areaWidget = root ? (sidebar ? sidebar->root : m_rootWidget) : target;
    const QRect area(areaWidget->mapTo(this, QPoint()), areaWidget->size());
    return previewRectForZone(area, zone);
}

QRect WorkspaceWidget::previewRectForZone(const QRect &area, DropZone zone)
{
    const int halfWidth = area.width() / 2;
    const int halfHeight = area.height() / 2;
    switch (zone) {
    case DropZone::Left: case DropZone::RootLeft:
        return QRect(area.left(), area.top(), halfWidth, area.height());
    case DropZone::Right: case DropZone::RootRight:
        return QRect(area.left() + halfWidth, area.top(),
                     area.width() - halfWidth, area.height());
    case DropZone::Top: case DropZone::RootTop:
        return QRect(area.left(), area.top(), area.width(), halfHeight);
    case DropZone::Bottom: case DropZone::RootBottom:
        return QRect(area.left(), area.top() + halfHeight,
                     area.width(), area.height() - halfHeight);
    case DropZone::Center:
        // Python SplitPreviewOverlay highlights the complete target pane for a merge.
        return area;
    }
    return {};
}

void WorkspaceWidget::handleDragMove(PaneWidget *target, const QPoint &global)
{
    const int index = target->tabInsertionIndex(global);
    if (index >= 0) {
        const QRect marker = target->tabInsertionRect(index);
        m_preview->showPreview(QRect(target->mapTo(this, marker.topLeft()), marker.size()),
                               m_theme, true);
        return;
    }
    const DropZone zone = dropZone(target, global);
    showDropPreview(target, zone);
}

void WorkspaceWidget::showDropPreview(PaneWidget *target, DropZone zone)
{
    if (!target && zone != DropZone::RootLeft && zone != DropZone::RootRight &&
        zone != DropZone::RootTop && zone != DropZone::RootBottom) return;
    if (isSidebar(target) && isHorizontalSplit(zone)) return;
    PaneWidget *geometryTarget = target ? target : rootPane();
    if (geometryTarget) m_preview->showPreview(previewRect(geometryTarget, zone), m_theme);
}

void WorkspaceWidget::hideDropPreview()
{
    m_preview->hide();
}

void WorkspaceWidget::handleDrop(PaneWidget *target, const QPoint &global,
                                 const QString &sourcePaneId, const QString &contentId)
{
    moveContent(sourcePaneId, contentId, target->paneId(), dropZone(target, global),
                target->tabInsertionIndex(global));
    hideDropPreview();
}

bool WorkspaceWidget::moveContent(const QString &sourcePaneId, const QString &contentId,
                                  const QString &targetPaneId, DropZone zone, int insertionIndex)
{
    PaneWidget *source = pane(sourcePaneId);
    PaneWidget *target = pane(targetPaneId);
    if (!source || !target ||
        (source == target && zone == DropZone::Center && insertionIndex < 0)) return false;
    const bool horizontal = isHorizontalSplit(zone);
    auto &targetTree = treeFor(targetPaneId);
    auto &sourceTree = treeFor(sourcePaneId);
    const bool involvesSidebar = &targetTree != &m_tree || &sourceTree != &m_tree;
    if (horizontal && &targetTree != &m_tree) return false;
    if (zone != DropZone::Center) insertionIndex = -1;
    if (insertionIndex >= 0) insertionIndex = qMin(insertionIndex, target->contentCount());
    const QJsonObject snapshot = targetTree.toJson();
    PaneContent content = source->takeContent(contentId);
    if (!content.widget) return false;
    if (source == target && insertionIndex > content.index) --insertionIndex;
    QString destinationId = targetPaneId;
    try {
        if (zone != DropZone::Center) {
            destinationId = nextPaneId();
            const bool root = zone == DropZone::RootLeft || zone == DropZone::RootRight ||
                              zone == DropZone::RootTop || zone == DropZone::RootBottom;
            const bool before = zone == DropZone::Left || zone == DropZone::Top ||
                                zone == DropZone::RootLeft || zone == DropZone::RootTop;
            const auto orientation = horizontal ? Qt::Horizontal : Qt::Vertical;
            if (root) targetTree.splitRoot(orientation, before, destinationId);
            else targetTree.split(targetPaneId, orientation, before, destinationId);
            rebuild(targetTree);
            source = pane(sourcePaneId);
            target = pane(destinationId);
        }
        if (!target || !target->addContent(content.contentId, content.title, content.widget,
                                           content.icon, content.closeable, insertionIndex, content.closePolicy))
            throw std::runtime_error("drop failed");
        if (source && source->contentCount() == 0 && sourceTree.paneIds().size() > 1) {
            (void)sourceTree.removePane(sourcePaneId);
            rebuild(sourceTree);
        }
        emit contentMoved(contentId, destinationId);
        setActivePane(pane(destinationId));
        target = pane(destinationId);
        if (target && involvesSidebar) target->focusTabBar();
        emit layoutChanged();
        return true;
    } catch (...) {
        targetTree = LayoutTree::fromJson(snapshot);
        rebuild(targetTree);
        if (PaneWidget *restoredSource = pane(sourcePaneId)) restoredSource->restoreContent(content);
        return false;
    }
}

bool WorkspaceWidget::resetToSingleEmptyPane()
{
    LayoutTree emptyLayout;
    const auto rootId = QStringLiteral("pane_1");
    emptyLayout.addPaneToRoot(isSidebar(pane(rootId)) ? nextPaneId() : rootId);
    QJsonObject document{{QStringLiteral("schema_version"), LayoutTree::SchemaVersion},
                         {QStringLiteral("layout"), emptyLayout.toJson()}};
    if (m_leftSidebar.host && m_rightSidebar.host) {
        document.insert(QStringLiteral("sidebars"), QJsonObject{
            {QStringLiteral("left"), m_leftSidebar.tree.paneIds().first()},
            {QStringLiteral("right"), m_rightSidebar.tree.paneIds().first()},
            {QStringLiteral("left_width_ratio"), m_leftWidthRatio},
            {QStringLiteral("right_width_ratio"), m_rightWidthRatio}
        });
    }
    return restoreLayout(document, {});
}

QList<WorkspaceWidget::DockedContent> WorkspaceWidget::dockedContents() const
{
    auto panesInOrder = m_tree.paneIds();
    panesInOrder.append(m_leftSidebar.tree.paneIds());
    panesInOrder.append(m_rightSidebar.tree.paneIds());
    QList<DockedContent> contents;
    for (const auto &paneId : panesInOrder) {
        auto *item = pane(paneId);
        for (const auto &id : item->contentIds())
            contents.append({item, id, item->contentWidget(id)});
    }
    return contents;
}

bool WorkspaceWidget::requestContentClose(const QList<DockedContent> &contents,
                                          const QSet<QWidget *> &reused)
{
    const QPointer<WorkspaceWidget> workspaceGuard(this);
    for (const auto &content : contents) {
        if (content.widget && reused.contains(content.widget.data())) continue;
        if (!content.pane || !content.widget || content.pane->contentWidget(content.id) != content.widget.data() ||
            !content.pane->requestContentClose(content.id) || !workspaceGuard)
            return false;
    }
    // A close prompt can run an event loop. Abort if its callbacks changed the tabs being replaced.
    return contents == dockedContents();
}

bool WorkspaceWidget::requestWindowClose()
{
    if (m_layoutTransitionInProgress) return false;
    m_layoutTransitionInProgress = true;
    const QPointer<WorkspaceWidget> workspaceGuard(this);
    const auto finish = qScopeGuard([workspaceGuard] {
        if (workspaceGuard) workspaceGuard->m_layoutTransitionInProgress = false;
    });
    return requestContentClose(dockedContents(), {});
}

bool WorkspaceWidget::saveLayout(const QString &path, const ContentDescriptor &descriptor,
                                 const PaneMetadata &metadata, QString *error) const
{
    QJsonObject documentData{{QStringLiteral("schema_version"), LayoutTree::SchemaVersion},
                             {QStringLiteral("layout"), m_tree.toJson()}};
    if (const auto *active = activePane())
        documentData.insert(QStringLiteral("active_pane_id"), active->paneId());
    QJsonObject contents;
    QJsonObject paneMetadata;
    auto ids = m_tree.paneIds();
    if (m_leftSidebar.host && m_rightSidebar.host) {
        const auto leftIds = m_leftSidebar.tree.paneIds();
        const auto rightIds = m_rightSidebar.tree.paneIds();
        const auto needsSaving = [this](const QList<QString> &sidebarIds) {
            const auto *item = pane(sidebarIds.first());
            return sidebarIds.size() > 1 || item->contentCount() || item->iconOnly();
        };
        if (needsSaving(leftIds) || needsSaving(rightIds) ||
            m_leftWidthRatio != DefaultSidebarWidthRatio || m_rightWidthRatio != DefaultSidebarWidthRatio) {
            ids.append(leftIds);
            ids.append(rightIds);
            const auto saveSidebar = [](const Sidebar &sidebar, const QList<QString> &sidebarIds) {
                return sidebarIds.size() == 1 ? QJsonValue(sidebarIds.first()) : QJsonValue(sidebar.tree.toJson());
            };
            documentData.insert(QStringLiteral("sidebars"),
                QJsonObject{{QStringLiteral("left"), saveSidebar(m_leftSidebar, leftIds)},
                            {QStringLiteral("right"), saveSidebar(m_rightSidebar, rightIds)},
                            {QStringLiteral("left_width_ratio"), m_leftWidthRatio},
                            {QStringLiteral("right_width_ratio"), m_rightWidthRatio}});
        }
    }
    for (const QString &paneId : ids) {
        const PaneWidget *item = pane(paneId);
        QJsonArray items;
        if (descriptor) {
            for (const QString &contentId : item->contentIds()) {
                QJsonObject value = descriptor(item, contentId);
                if (!value.isEmpty()) {
                    if (!value.contains(QStringLiteral("content_id")))
                        value.insert(QStringLiteral("content_id"), contentId);
                    items.append(value);
                }
            }
        }
        if (!items.isEmpty()) contents.insert(paneId, items);
        QJsonObject meta = metadata ? metadata(item) : QJsonObject();
        if (!item->currentContentId().isEmpty())
            meta.insert(QStringLiteral("current_content_id"), item->currentContentId());
        if (item->iconOnly()) meta.insert(QStringLiteral("icon_only"), true);
        if (!meta.isEmpty()) paneMetadata.insert(paneId, meta);
    }
    if (!contents.isEmpty()) documentData.insert(QStringLiteral("pane_contents"), contents);
    if (!paneMetadata.isEmpty()) documentData.insert(QStringLiteral("pane_metadata"), paneMetadata);
    return saveLayoutAtomic(path, documentData, error);
}

bool WorkspaceWidget::loadLayout(const QString &path, const ContentFactory &factory,
                                 QString *error)
{
    if (error) error->clear();
    const QJsonObject documentData = loadLayoutFile(path, error);
    if (documentData.isEmpty()) {
        if (error && error->isEmpty()) *error = QStringLiteral("工作区布局为空");
        return false;
    }
    return restoreLayout(documentData, factory, error);
}

bool WorkspaceWidget::restoreLayout(const QJsonObject &documentData, const ContentFactory &factory,
                                    QString *error)
{
    if (error) error->clear();
    if (m_layoutTransitionInProgress) return false;
    m_layoutTransitionInProgress = true;
    const QPointer<WorkspaceWidget> workspaceGuard(this);
    const auto finish = qScopeGuard([workspaceGuard] {
        if (workspaceGuard) workspaceGuard->m_layoutTransitionInProgress = false;
    });

    const int version = documentData.value(QStringLiteral("schema_version")).toInt(-1);
    if (version != LayoutTree::SchemaVersion) {
        if (error) *error = QStringLiteral("不支持的工作区布局版本");
        return false;
    }
    try {
        const QJsonObject layout = documentData.value(QStringLiteral("layout")).toObject();
        LayoutTree candidate = LayoutTree::fromJson(layout);
        const QList<QString> paneIdList = candidate.paneIds();
        QSet<QString> paneIds(paneIdList.cbegin(), paneIdList.cend());
        LayoutTree left;
        LayoutTree right;
        const auto sidebars = documentData.value(QStringLiteral("sidebars")).toObject();
        const auto widthRatio = [&sidebars](const QString &key) {
            const double ratio = sidebars.value(key).toDouble(DefaultSidebarWidthRatio);
            return ratio > 0.0 && ratio < 1.0 ? ratio : DefaultSidebarWidthRatio;
        };
        if (documentData.contains(QStringLiteral("sidebars"))) {
            const auto readSidebar = [&paneIds](const QJsonValue &value) {
                LayoutTree tree = value.isString() ? singleSidebarPane(value.toString())
                                                   : LayoutTree::fromJson(value.toObject());
                if (!isVerticalSidebarTree(tree.root()))
                    throw std::invalid_argument("sidebar splits must be vertical");
                tree.normalize();
                for (const auto &id : tree.paneIds()) {
                    if (paneIds.contains(id)) throw std::invalid_argument("duplicate sidebar pane ID");
                    paneIds.insert(id);
                }
                return tree;
            };
            left = readSidebar(sidebars.value(QStringLiteral("left")));
            right = readSidebar(sidebars.value(QStringLiteral("right")));
        }
        if (documentData.contains(QStringLiteral("pane_contents")) &&
            !documentData.value(QStringLiteral("pane_contents")).isObject())
            throw std::invalid_argument("pane_contents must be an object");
        if (documentData.contains(QStringLiteral("pane_metadata")) &&
            !documentData.value(QStringLiteral("pane_metadata")).isObject())
            throw std::invalid_argument("pane_metadata must be an object");
        const QJsonObject contents = documentData.value(QStringLiteral("pane_contents")).toObject();
        const QJsonObject metadata = documentData.value(QStringLiteral("pane_metadata")).toObject();
        QSet<QString> contentIds;
        for (auto it = contents.begin(); it != contents.end(); ++it) {
            if (!paneIds.contains(it.key()) || !it.value().isArray())
                throw std::invalid_argument("pane_contents references an unknown pane");
            for (const auto &value : it.value().toArray()) {
                if (!value.isObject()) throw std::invalid_argument("invalid content descriptor");
                const QJsonObject descriptor = value.toObject();
                if (descriptor.contains(QStringLiteral("content_id"))) {
                    const QJsonValue idValue = descriptor.value(QStringLiteral("content_id"));
                    if (!idValue.isString() || idValue.toString().trimmed().isEmpty())
                        throw std::invalid_argument("content id must be a non-empty string");
                    const QString id = idValue.toString();
                    if (contentIds.contains(id)) throw std::invalid_argument("duplicate content id");
                    contentIds.insert(id);
                }
            }
        }
        for (auto it = metadata.begin(); it != metadata.end(); ++it)
            if (!paneIds.contains(it.key()) || !it.value().isObject())
                throw std::invalid_argument("pane_metadata references an unknown pane");

        // Build in a hidden workspace. Factories may return existing editor
        // widgets, so finish every factory call before transferring any page.
        WorkspaceWidget staged(nullptr, m_theme, false);
        staged.m_tree = std::move(candidate);
        staged.rebuild(false);
        staged.createSidebars(std::move(left), std::move(right));
        for (auto it = metadata.begin(); it != metadata.end(); ++it)
            staged.pane(it.key())->setIconOnly(it.value().toObject().value(QStringLiteral("icon_only")).toBool());
        struct PreparedContent {
            QString paneId;
            ContentConfig config;
            QPointer<QWidget> widget;
            QPointer<QWidget> originalParent;
            bool wasHidden;
        };
        QList<PreparedContent> prepared;
        QSet<QWidget *> widgets;
        if (factory) {
            for (auto it = contents.begin(); it != contents.end(); ++it) {
                for (const auto &value : it.value().toArray()) {
                    std::optional<ContentConfig> config = factory(value.toObject());
                    if (!config) continue;
                    if (config->widget) {
                        if (widgets.contains(config->widget))
                            throw std::runtime_error("content factory returned the same widget twice");
                        widgets.insert(config->widget);
                        // Adopt new pages. Detached KeepAlive pages still belong to the caller.
                        if (!config->widget->parentWidget() && config->closePolicy == ClosePolicy::DeleteLater)
                            config->widget->setParent(&staged);
                    }
                    auto *widget = config->widget;
                    prepared.append({it.key(), *config, widget, widget ? widget->parentWidget() : nullptr,
                                     widget && widget->isHidden()});
                }
            }
        }

        const auto originalContents = dockedContents();
        if (!requestContentClose(originalContents, widgets)) return false;

        QList<std::pair<DockedContent, PaneContent>> transferred;
        bool committed = false;
        const auto rollback = qScopeGuard([&] {
            if (committed) return;
            for (auto it = transferred.rbegin(); it != transferred.rend(); ++it) {
                if (!it->first.pane || !it->first.widget) continue;
                const QSignalBlocker blocker(it->first.pane);
                it->first.pane->restoreContent(it->second);
            }
            for (const auto &item : prepared) {
                if (!item.widget || item.widget->parentWidget() == item.originalParent) continue;
                item.widget->setParent(item.originalParent);
                if (item.widget) item.widget->setVisible(!item.wasHidden);
            }
        });
        for (const auto &item : prepared) {
            if (item.config.widget && !item.widget)
                throw std::runtime_error("content factory page was destroyed during preparation");
            if (item.widget) {
                for (const auto &content : originalContents) {
                    if (content.widget != item.widget) continue;
                    const QSignalBlocker blocker(content.pane);
                    transferred.append({content, content.pane->takeContent(content.id)});
                    break;
                }
            }
            if (!staged.fillPane(staged.pane(item.paneId), item.config))
                throw std::runtime_error("content factory result could not be inserted");
        }
        for (auto it = metadata.begin(); it != metadata.end(); ++it)
            staged.pane(it.key())->setCurrentContentId(
                it.value().toObject().value(QStringLiteral("current_content_id")).toString());

        // Removed pages can leave a saved pane without any of its tabs.
        // Preserve panes that were saved empty.
        if (factory) {
            for (auto it = contents.begin(); it != contents.end(); ++it) {
                const auto *pane = staged.pane(it.key());
                if (pane && pane->contentCount() == 0 && !it.value().toArray().isEmpty())
                    staged.removeEmptyPane(it.key());
            }
        }

        QStringList closedIds;
        for (const auto &content : originalContents) {
            if (!content.pane || !content.widget || widgets.contains(content.widget.data())) continue;
            QSignalBlocker blocker(content.pane);
            if (content.pane->disposeContent(content.id)) closedIds.append(content.id);
            if (!content.pane) blocker.dismiss();
            if (!workspaceGuard) return false;
        }

        // Rebind callbacks to the surviving workspace before adopting the root.
        for (PaneWidget *item : staged.m_panes) {
            disconnect(item, nullptr, &staged, nullptr);
            connectPane(item);
        }
        connectSplitters(staged.m_rootWidget, staged.m_tree.root());
        QWidget *oldRoot = m_rootWidget;
        m_center->layout()->removeWidget(oldRoot);
        oldRoot->hide();
        m_rootWidget = staged.m_rootWidget;
        for (auto *sidebar : {m_leftSidebar.host, m_rightSidebar.host}) {
            if (!sidebar) continue;
            sidebar->hide();
            sidebar->setParent(this);
            sidebar->deleteLater();
        }
        m_leftSidebar = std::exchange(staged.m_leftSidebar, {});
        m_rightSidebar = std::exchange(staged.m_rightSidebar, {});
        for (const auto *sidebar : {&m_leftSidebar, &m_rightSidebar}) {
            if (sidebar->tree.paneIds().size() > 1)
                connectSplitters(sidebar->root, sidebar->tree.root());
        }
        m_sidebarSplitter->insertWidget(0, m_leftSidebar.host);
        m_sidebarSplitter->addWidget(m_rightSidebar.host);
        m_leftWidthRatio = widthRatio(QStringLiteral("left_width_ratio"));
        m_rightWidthRatio = widthRatio(QStringLiteral("right_width_ratio"));
        m_tree = std::move(staged.m_tree);
        m_panes = std::move(staged.m_panes);
        m_contentCounter = std::max(m_contentCounter, staged.m_contentCounter);
        staged.m_rootWidget = nullptr;
        m_center->layout()->addWidget(m_rootWidget);
        m_rootWidget->show();
        placeSidebarToggles();
        updateSidebarToggles();
        oldRoot->deleteLater();
        committed = true;
        auto *savedActivePane = pane(documentData.value(QStringLiteral("active_pane_id")).toString());
        setActivePane(savedActivePane ? savedActivePane : rootPane());
        hideDropPreview();
        m_preview->raise();
        for (const auto &id : closedIds) {
            emit contentClosed(id);
            if (!workspaceGuard) return true;
        }
        emit layoutChanged();
        return true;
    } catch (const std::exception &exception) {
        if (error) *error = QString::fromUtf8(exception.what());
        return false;
    } catch (...) {
        if (error) *error = QStringLiteral("恢复工作区内容失败");
        return false;
    }
}

void WorkspaceWidget::applyTheme(const DockTheme &theme)
{
    m_theme = theme;
    setStyleSheet(theme.styleSheet());
    updateSidebarToggles();
    static_cast<WorkspaceSplitter *>(m_sidebarSplitter)->applyTheme(theme);
    for (PaneWidget *item : m_panes) item->applyTheme(theme);
    for (auto *splitter : findChildren<QSplitter *>(QStringLiteral("MyAdsSplitter")))
        if (auto *workspaceSplitter = dynamic_cast<WorkspaceSplitter *>(splitter))
            workspaceSplitter->applyTheme(theme);
}

DockTheme WorkspaceWidget::theme() const { return m_theme; }

} // namespace darkeye::myads
