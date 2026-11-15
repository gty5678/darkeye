#include "ui/components/WorkTagSelector.h"
#include "darkeye_ui/components/DesignInput.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/components/IconButton.h"
#include "darkeye_ui/components/InteractionEffects.h"
#include "darkeye_ui/components/VerticalText.h"
#include "darkeye_ui/theme/ThemeService.h"
#include <QDateTime>
#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QGraphicsObject>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <algorithm>
#include <functional>

namespace darkeye
{
namespace
{
constexpr int cardWidth = 27, gap = 5, margin = 5, defaultViewWidth = 130, toolWidth = 24, panelWidth = 255;
bool cjk(QChar c)
{
    const ushort u = c.unicode();
    return (u >= 0x4e00 && u <= 0x9fff) || (u >= 0x3040 && u <= 0x30ff);
}
QFont cardFont(QChar c)
{
    QFont f(cjk(c) ? QStringLiteral("KaiTi") : QStringLiteral("Courier New"));
    f.setPointSize(cjk(c) ? 14 : 16);
    f.setBold(true);
    return f;
}
ThemeTokens themeTokens(ThemeService *s)
{
    return ThemeService::tokens(s ? s->current() : ThemeId::Light, s ? s->customPrimary() : QString());
}
QColor readable(const QColor &c)
{
    return .299 * c.red() + .587 * c.green() + .114 * c.blue() > 128
               ? QColor(Qt::black) : QColor(Qt::white);
}
class VerticalTabs final : public QTabWidget
{
  public:
    using QTabWidget::setTabBar;
};
} // namespace

class TagCard final : public QGraphicsObject
{
  public:
    TagCard(qint64 id, const QString &t, const QString &color, ThemeService *themes, std::function<void()> click,
            bool title = false)
        : m_id(id), m_text(t), m_color(color), m_themes(themes), m_click(std::move(click)), m_title(title),
          m_size(calculateSize())
    {
        setObjectName(title ? QStringLiteral("WorkTagTitle") : QStringLiteral("WorkTag_%1").arg(id));
        setAcceptHoverEvents(!title);
        setAcceptedMouseButtons(title ? Qt::NoButton : Qt::LeftButton);
        setCursor(title ? Qt::ArrowCursor : Qt::PointingHandCursor);
    }
    qint64 id() const
    {
        return m_id;
    }
    QRectF boundingRect() const override
    {
        return QRectF(QPointF(), QSizeF(m_size));
    }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        ThemeTokens t = themeTokens(m_themes);
        QColor bg = m_title ? QColor(t.text) : m_color;
        if (!bg.isValid())
            bg = QColor(t.pageBackground);
        QColor fg = readable(bg),
               border = m_title ? QColor(t.border) : QColor(Qt::transparent);
        if (m_invert)
            std::swap(bg, fg);
        else if (m_hover && !m_title)
        {
            bg = bg.lighter(110);
            fg = readable(m_color) == QColor(Qt::black) ? QColor("#646464") : QColor("#C5C5C5");
            border = Qt::red;
        }
        QRectF r = boundingRect();
        qreal cut = r.width() * .2, rad = r.width() * .1;
        QPainterPath out;
        out.moveTo(cut, 0);
        out.lineTo(r.width() - cut, 0);
        out.lineTo(r.width(), cut);
        out.lineTo(r.width(), r.height() - cut);
        out.lineTo(r.width() - cut, r.height());
        out.lineTo(cut, r.height());
        out.lineTo(0, r.height() - cut);
        out.lineTo(0, cut);
        out.closeSubpath();
        QPainterPath hole;
        hole.addEllipse(QPointF(r.width() / 2, cut + rad), rad, rad);
        p->setRenderHint(QPainter::Antialiasing);
        p->fillPath(out.subtracted(hole), bg);
        p->setPen(border);
        p->drawPath(out);
        p->drawPath(hole);
        qreal adjust = 0;
        if (!m_text.isEmpty())
        {
            QChar c = m_text.front();
            QFontMetrics fm(cardFont(c));
            adjust = fm.horizontalAdvance(c) * (cjk(c) ? .1 : .4);
        }
        int maxw = QFontMetrics(cardFont(QChar(0x4e2d))).horizontalAdvance(QChar(0x4e2d));
        qreal y = cut * 2 + rad * 2 - adjust;
        for (QChar c : m_text)
        {
            if (c == '\n')
                continue;
            QFont f = cardFont(c);
            QFontMetrics fm(f);
            int w = fm.horizontalAdvance(c);
            qreal h = cjk(c) ? fm.height() : fm.ascent() + fm.descent() * .3;
            p->setFont(f);
            p->setPen(fg);
            p->drawText(QPointF((r.width() - maxw) / 2 + (maxw - w) / 2, y + fm.ascent()), QString(c));
            y += h;
        }
    }
    void flash()
    {
        if (m_timer == nullptr)
        {
            m_timer = new QTimer(this);
            connect(m_timer, &QTimer::timeout, this,
                    [this]
                    {
                        if (QDateTime::currentMSecsSinceEpoch() >= m_end)
                        {
                            m_timer->stop();
                            m_invert = false;
                        }
                        else
                            m_invert = !m_invert;
                        update();
                    });
        }
        m_end = QDateTime::currentMSecsSinceEpoch() + 3000;
        m_invert = true;
        m_timer->start(150);
        update();
    }
    void resetHover()
    {
        m_hover = false;
        update();
    }

  protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *e) override
    {
        m_hover = true;
        update();
        QGraphicsObject::hoverEnterEvent(e);
    }
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *e) override
    {
        m_hover = false;
        update();
        QGraphicsObject::hoverLeaveEvent(e);
    }
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override
    {
        if (!m_title && e->button() == Qt::LeftButton)
        {
            e->accept();
            if (m_click)
                m_click();
            return;
        }
        QGraphicsObject::mousePressEvent(e);
    }

  private:
    QSizeF calculateSize() const
    {
        if (m_text.isEmpty())
            return {cardWidth, cardWidth * 2};
        qreal h = 0;
        for (QChar c : m_text)
        {
            if (c == '\n')
                continue;
            QFontMetrics fm(cardFont(c));
            h += cjk(c) ? fm.height() : fm.ascent() + fm.descent() * .3;
        }
        QChar c = m_text.front();
        QFontMetrics fm(cardFont(c));
        qreal adjust = fm.horizontalAdvance(c) * (cjk(c) ? .1 : .4);
        return {cardWidth, h + cardWidth * .8 - fm.descent() * .3 - adjust};
    }
    qint64 m_id;
    QString m_text;
    QColor m_color;
    ThemeService *m_themes;
    std::function<void()> m_click;
    QTimer *m_timer = nullptr;
    qint64 m_end = 0;
    bool m_title = false, m_hover = false, m_invert = false;
    QSizeF m_size;
};

class TagFlowScene final : public QGraphicsScene
{
  public:
    explicit TagFlowScene(int fallbackWidth, QObject *p = nullptr) : QGraphicsScene(p), m_fallbackWidth(fallbackWidth) {}
    void addCard(TagCard *c, bool updateLayout = true)
    {
        addItem(c);
        m_cards.append(c);
        if (updateLayout)
        {
            c->setVisible(true);
            relayout();
        }
    }
    void removeCard(TagCard *c)
    {
        m_cards.removeOne(c);
        removeItem(c);
        c->deleteLater();
        relayout();
    }
    void moveToEnd(TagCard *c)
    {
        if (m_cards.removeOne(c))
            m_cards.append(c);
        relayout();
    }
    void updateCards()
    {
        for (TagCard *card : std::as_const(m_cards))
            card->update();
    }
    void relayout(int requestedWidth = -1)
    {
        int width = requestedWidth;
        if (width <= margin * 2 && !views().isEmpty())
            width = views().constFirst()->viewport()->width();
        if (width <= margin * 2)
            width = m_fallbackWidth;
        int aw = qMax(1, width - margin * 2), cols = qMax(1, aw / (cardWidth + gap)),
            total = cols * cardWidth + (cols - 1) * gap;
        const qreal ox = margin + (aw - total) / 2.0;
        QVector<qreal> hs(cols, margin);
        bool any = false;
        for (TagCard *c : std::as_const(m_cards))
        {
            if (!c->isVisible())
                continue;
            any = true;
            int col = 0;
            for (int i = 1; i < cols; ++i)
                if (hs[i] < hs[col])
                    col = i;
            c->setPos(ox + col * (cardWidth + gap), hs[col]);
            hs[col] += c->boundingRect().height() + gap;
        }
        setSceneRect(0, 0, width, (any ? *std::max_element(hs.cbegin(), hs.cend()) : margin) + margin);
    }

  private:
    QList<TagCard *> m_cards;
    int m_fallbackWidth;
};

class TagGraphicsView final : public QGraphicsView
{
  public:
    explicit TagGraphicsView(QWidget *parent = nullptr) : QGraphicsView(parent)
    {
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setFrameShape(QFrame::NoFrame);
        setAlignment(Qt::AlignLeft | Qt::AlignTop);
        setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    }

  protected:
    void showEvent(QShowEvent *e) override
    {
        QGraphicsView::showEvent(e);
        if (auto *flow = dynamic_cast<TagFlowScene *>(scene()))
            flow->relayout(viewport()->width());
    }
    void resizeEvent(QResizeEvent *e) override
    {
        QGraphicsView::resizeEvent(e);
        if (auto *flow = dynamic_cast<TagFlowScene *>(scene()))
            flow->relayout(viewport()->width());
    }
};

TagDisplayPreview::TagDisplayPreview(ThemeService *themes, QWidget *parent)
    : QWidget(parent), m_themes(themes)
{
    setAttribute(Qt::WA_StyledBackground);
    setObjectName(QStringLiteral("TagDisplayPreview"));
    setFixedWidth(60);
    setMinimumHeight(100);
    setStyleSheet(QStringLiteral(
        "#TagDisplayPreview { border-radius: 12px; border: 3px dashed #cccccc; }"
        "#TagDisplayPreview QGraphicsView { background: transparent; border: none; }"));

    auto *layout = new QHBoxLayout(this);
    m_view = new TagGraphicsView(this);
    m_view->setAlignment(Qt::AlignCenter);
    m_view->setBackgroundBrush(Qt::NoBrush);
    m_scene = new QGraphicsScene(this);
    m_view->setScene(m_scene);
    layout->addWidget(m_view);
}

void TagDisplayPreview::setTag(const QString &name, const QString &color, const QString &detail)
{
    if (m_card != nullptr)
    {
        m_scene->removeItem(m_card);
        delete m_card;
    }
    m_card = new TagCard(-1, name, color, m_themes, {});
    m_card->setToolTip(detail);
    m_scene->addItem(m_card);
    m_scene->setSceneRect(m_card->boundingRect());
}

WorkTagSelector::WorkTagSelector(const QList<TagOption> &tags, ThemeService *themes, QWidget *parent)
    : QWidget(parent), m_tags(tags), m_themes(themes)
{
    setObjectName(QStringLiteral("WorkTagSelector"));
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto *left = new QWidget(this);
    left->setFixedWidth(defaultViewWidth + toolWidth);
    auto *ll = new QHBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->setSpacing(0);
    m_selectedView = new TagGraphicsView(left);
    m_selectedView->setObjectName(QStringLiteral("WorkTagList"));
    m_selectedView->setFixedWidth(defaultViewWidth);
    m_selectedView->setToolTip(QStringLiteral("点击已选标签可移除"));
    m_selectedFlow = new TagFlowScene(defaultViewWidth, m_selectedView);
    m_selectedFlow->setObjectName(QStringLiteral("WorkSelectedTagFlow"));
    m_selectedView->setScene(m_selectedFlow);
    applyViewStyle(m_selectedView);
    m_selectedFlow->addCard(new TagCard(-1, QStringLiteral("作品标签"), {}, themes, {}, true));
    auto *tools = new QWidget(left);
    tools->setFixedWidth(24);
    auto *tl = new QVBoxLayout(tools);
    tl->setContentsMargins(0, 0, 0, 0);
    auto *clear = new ShakeButton(QStringLiteral("brush_cleaning"), themes, tools);
    auto *reload = new RotateButton(QStringLiteral("refresh_cw"), themes, tools);
    m_expandButton = new IconButton(QStringLiteral("arrow_right"), themes, tools);
    clear->setProperty("workControlId", QStringLiteral("WorkTagClearButton"));
    reload->setProperty("workControlId", QStringLiteral("WorkTagReloadButton"));
    m_expandButton->setProperty("workControlId", QStringLiteral("WorkTagExpandButton"));
    clear->setToolTip(QStringLiteral("清空已选标签"));
    reload->setToolTip(QStringLiteral("重新加载标签"));
    m_expandButton->setToolTip(QStringLiteral("展开标签库"));
    clear->setButtonPixelSize(24);
    clear->setIconPixelSize(24);
    reload->setButtonPixelSize(24);
    reload->setIconPixelSize(24);
    m_expandButton->setButtonPixelSize(24);
    m_expandButton->setIconPixelSize(24);
    tl->addWidget(clear);
    tl->addWidget(reload);
    tl->addWidget(m_expandButton);
    tl->addStretch();
    ll->addWidget(m_selectedView);
    ll->addWidget(tools);
    root->addWidget(left);
    m_panel = new QWidget(this);
    m_panel->setObjectName(QStringLiteral("WorkTagAvailablePanel"));
    m_panel->setFixedWidth(0);
    auto *pl = new QVBoxLayout(m_panel);
    pl->setContentsMargins(0, 0, 0, 0);
    auto *bar = new QWidget(m_panel);
    auto *bl = new QHBoxLayout(bar);
    m_search = new DesignLineEdit(bar);
    // Keep the design-system object name: Python's MyLineEdit inherits it too.
    m_search->setProperty("workControlId", QStringLiteral("WorkTagSearch"));
    m_search->setPlaceholderText(QStringLiteral("搜索"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(50);
    m_search->installEventFilter(this);
    m_searchResult = new DesignLabel(QStringLiteral("无搜索结果"), bar);
    m_searchResult->setProperty("workControlId", QStringLiteral("WorkTagSearchResult"));
    m_searchResult->setFixedWidth(70);
    m_searchPrevious = new IconButton(QStringLiteral("arrow_up"), themes, bar);
    m_searchNext = new IconButton(QStringLiteral("arrow_down"), themes, bar);
    m_searchPrevious->setProperty("workControlId", QStringLiteral("WorkTagSearchPrevious"));
    m_searchNext->setProperty("workControlId", QStringLiteral("WorkTagSearchNext"));
    m_searchPrevious->setToolTip(QStringLiteral("向前搜索(Shift+Enter)"));
    m_searchNext->setToolTip(QStringLiteral("向后搜索(Enter)"));
    m_searchPrevious->setEnabled(false);
    m_searchNext->setEnabled(false);
    bl->addWidget(m_search);
    bl->addWidget(m_searchResult);
    bl->addWidget(m_searchPrevious);
    bl->addWidget(m_searchNext);
    bl->addStretch();
    auto *vt = new VerticalTabs;
    vt->setParent(m_panel);
    m_tabs = vt;
    m_tabs->setObjectName(QStringLiteral("WorkTagTypeTabs"));
    m_tabs->setTabPosition(QTabWidget::West);
    vt->setTabBar(new TokenVerticalTabBar(themes, vt));
    pl->addWidget(bar);
    pl->addWidget(m_tabs, 1);
    root->addWidget(m_panel);
    // Keep the selected-tags column anchored to the selector's left edge while
    // the available-tags panel changes width.  This matches TagSelector5's
    // trailing stretch and prevents any spare layout space from being placed
    // before the left column.
    root->addStretch(1);
    setFixedWidth(defaultViewWidth + toolWidth);
    m_panelAnimation = new QVariantAnimation(this);
    m_panelAnimation->setDuration(300);
    m_panelAnimation->setEasingCurve(QEasingCurve::InOutQuad);
    connect(m_panelAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_panel->setFixedWidth(v.toInt());
        setFixedWidth(m_selectedContainerWidth + v.toInt());
    });
    connect(clear, &QPushButton::clicked, this, &WorkTagSelector::clearSelection);
    connect(reload, &QPushButton::clicked, this, &WorkTagSelector::reloadTags);
    connect(m_expandButton, &QPushButton::clicked, this, [this] { setPanelExpanded(!m_panelExpanded); });
    connect(m_search, &QLineEdit::textChanged, this, &WorkTagSelector::rebuildSearch);
    connect(m_searchPrevious, &QPushButton::clicked, this, [this] { navigateSearch(-1); });
    connect(m_searchNext, &QPushButton::clicked, this, [this] { navigateSearch(1); });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (auto *view = m_views.value(m_tabs->tabText(index)))
        {
            if (auto *flow = m_flows.value(m_tabs->tabText(index)))
                flow->relayout();
            view->verticalScrollBar()->setValue(0);
        }
    });
    if (themes)
        connect(themes, &ThemeService::themeChanged, this, [this] {
            applyViewStyle(m_selectedView);
            for (auto *v : std::as_const(m_views))
                applyViewStyle(v);
            m_selectedFlow->updateCards();
            for (auto *flow : std::as_const(m_flows))
                flow->updateCards();
            applyTabStyle();
        });
    setTags(tags);
}

bool WorkTagSelector::eventFilter(QObject *w, QEvent *e)
{
    if (w == m_search && e->type() == QEvent::KeyPress)
    {
        auto *k = static_cast<QKeyEvent *>(e);
        if ((k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter)
            && (k->modifiers() == Qt::NoModifier || k->modifiers() == Qt::ShiftModifier))
        {
            navigateSearch(k->modifiers().testFlag(Qt::ShiftModifier) ? -1 : 1);
            return true;
        }
    }
    return QWidget::eventFilter(w, e);
}
void WorkTagSelector::setLoader(Loader l)
{
    m_loader = std::move(l);
}
void WorkTagSelector::reloadTags()
{
    if (m_loader)
        setTags(m_loader());
}
QList<qint64> WorkTagSelector::selectedIds() const
{
    return m_selectedOrder;
}
void WorkTagSelector::setSelectedIds(const QList<qint64> &ids)
{
    QSet<qint64> requested;
    QList<qint64> order;
    for (qint64 id : ids)
    {
        if (!tagById(id) || requested.contains(id)) continue;
        requested.insert(id);
        order.append(id);
    }
    for (qint64 id : QList<qint64>(m_selectedOrder))
        if (!requested.contains(id)) setTagSelected(id, false, false);
    // 数据库回填与 Python load_with_ids 相同：不弹互斥提示，忠实恢复已有关联。
    for (qint64 id : order)
    {
        if (m_selectedIds.contains(id)) continue;
        const TagOption *tag = tagById(id);
        if (!tag) continue;
        m_selectedIds.insert(id);
        m_selectedOrder.append(id);
        auto *card = new TagCard(id, tag->name, tag->color, m_themes,
                                 [this, id] { QTimer::singleShot(0, this, [this, id] { setTagSelected(id, false); }); });
        m_selectedItems.insert(id, card);
        m_selectedFlow->addCard(card);
    }
    updateAvailableVisibility();
    emit selectionChanged();
}
void WorkTagSelector::clearSelection()
{
    for (qint64 id : QList<qint64>(m_selectedOrder))
        setTagSelected(id, false, false);
}
void WorkTagSelector::setModified(bool modified)
{
    // Python TagSelector5.set_state highlights only the selected-tags view.
    m_selectedView->setProperty("addWorkModified", modified);
    applyViewStyle(m_selectedView);
}
void WorkTagSelector::setSelectedColumnWidth(int width, int containerWidth)
{
    if (width <= 0)
        return;
    m_selectedColumnWidth = width;
    m_selectedContainerWidth = qMax(width + toolWidth, containerWidth);
    m_selectedView->setFixedWidth(width);
    if (QWidget *left = m_selectedView->parentWidget())
        left->setFixedWidth(m_selectedContainerWidth);
    setFixedWidth(m_selectedContainerWidth + m_panel->width());
    m_selectedFlow->relayout();
}
void WorkTagSelector::setAvailablePanelWidth(int width)
{
    if (width <= 0 || width == m_availablePanelWidth)
        return;
    m_availablePanelWidth = width;
    setPanelExpanded(m_panelExpanded, false);
}
void WorkTagSelector::setAvailablePanelExpanded(bool expanded)
{
    setPanelExpanded(expanded, false);
}
void WorkTagSelector::setPanelExpanded(bool on, bool animate)
{
    m_panelExpanded = on;
    m_expandButton->setIconName(on ? QStringLiteral("arrow_left") : QStringLiteral("arrow_right"));
    m_expandButton->setToolTip(on ? QStringLiteral("收起标签库") : QStringLiteral("展开标签库"));
    m_panelAnimation->stop();
    if (!animate)
    {
        const int width = on ? m_availablePanelWidth : 0;
        m_panel->setFixedWidth(width);
        setFixedWidth(m_selectedContainerWidth + width);
        return;
    }
    m_panelAnimation->setStartValue(m_panel->width());
    m_panelAnimation->setEndValue(on ? m_availablePanelWidth : 0);
    m_panelAnimation->start();
    if (on)
        QTimer::singleShot(350, this, [this] {
            if (auto *f = m_flows.value(m_tabs->tabText(m_tabs->currentIndex())))
                f->relayout();
        });
}
void WorkTagSelector::setTagSelected(qint64 id, bool selected, bool switchTab)
{
    if (selected == m_selectedIds.contains(id))
        return;
    const TagOption *t = tagById(id);
    if (!t)
        return;
    if (selected && m_mutexCheckEnabled && !t->mutexGroup.isEmpty())
        for (qint64 sid : std::as_const(m_selectedIds))
        {
            const TagOption *s = tagById(sid);
            if (s && s->mutexGroup == t->mutexGroup)
            {
                QMessageBox::warning(this, QStringLiteral("标签冲突"),
                                     QStringLiteral("标签 <b>'%1'</b> 与已选标签 <b>'%2'</b> "
                                                    "互斥！<br><br>请先移除冲突标签再添加。")
                                         .arg(t->name, s->name));
                return;
            }
        }
    if (selected)
    {
        m_selectedIds.insert(id);
        m_selectedOrder.append(id);
        auto *c = new TagCard(id, t->name, t->color, m_themes,
                              [this, id] { QTimer::singleShot(0, this, [this, id] { setTagSelected(id, false); }); });
        m_selectedItems.insert(id, c);
        m_selectedFlow->addCard(c);
    }
    else
    {
        m_selectedIds.remove(id);
        m_selectedOrder.removeOne(id);
        if (auto *c = m_selectedItems.take(id))
            m_selectedFlow->removeCard(c);
        if (auto *c = m_items.value(id))
        {
            c->resetHover();
            c->setVisible(true);
            if (auto *f = m_flows.value(t->typeName))
            {
                f->moveToEnd(c);
                if (switchTab)
                    for (int i = 0; i < m_tabs->count(); ++i)
                        if (m_tabs->tabText(i) == t->typeName)
                        {
                            m_tabs->setCurrentIndex(i);
                            break;
                        }
            }
        }
    }
    updateAvailableVisibility();
    emit selectionChanged();
}
void WorkTagSelector::rebuildSearch(const QString &s)
{
    m_searchMatches.clear();
    m_searchIndex = -1;
    QString n = s.trimmed();
    if (!n.isEmpty())
        for (const auto &t : std::as_const(m_tags))
            // Mirror TagSelector5.get_tagid_by_keyword(): search canonical names
            // and redirect aliases, but not type names or descriptive text.
            if (t.name.contains(n, Qt::CaseInsensitive)
                || std::any_of(t.aliases.cbegin(), t.aliases.cend(),
                               [&n](const QString &alias) {
                                   return alias.contains(n, Qt::CaseInsensitive);
                               }))
                m_searchMatches.append(t.id);
    bool ok = !m_searchMatches.isEmpty();
    m_searchPrevious->setEnabled(ok);
    m_searchNext->setEnabled(ok);
    m_searchResult->setText(ok            ? QStringLiteral("找到 %1 个结果").arg(m_searchMatches.size())
                            : n.isEmpty() ? QStringLiteral("无搜索结果")
                                          : QStringLiteral("无匹配结果"));
    if (ok)
        navigateSearch(1);
}
void WorkTagSelector::navigateSearch(int d)
{
    if (m_searchMatches.isEmpty())
        return;
    m_searchIndex = (m_searchIndex + d + m_searchMatches.size()) % m_searchMatches.size();
    showSearchResult();
}
void WorkTagSelector::showSearchResult()
{
    if (m_searchIndex < 0)
        return;
    qint64 id = m_searchMatches[m_searchIndex];
    m_searchResult->setText(QStringLiteral("结果 %1/%2").arg(m_searchIndex + 1).arg(m_searchMatches.size()));
    if (auto *c = m_selectedItems.value(id))
    {
        m_selectedView->centerOn(c);
        c->flash();
        return;
    }
    const TagOption *t = tagById(id);
    auto *c = m_items.value(id);
    if (!t || !c)
        return;
    for (int i = 0; i < m_tabs->count(); ++i)
        if (m_tabs->tabText(i) == t->typeName)
        {
            m_tabs->setCurrentIndex(i);
            break;
        }
    if (auto *v = m_views.value(t->typeName))
        v->centerOn(c);
    c->flash();
}
void WorkTagSelector::updateAvailableVisibility()
{
    for (auto i = m_items.cbegin(); i != m_items.cend(); ++i)
        i.value()->setVisible(!m_selectedIds.contains(i.key()));
    for (auto *f : std::as_const(m_flows))
        f->relayout();
    m_selectedFlow->relayout();
}
void WorkTagSelector::setTags(const QList<TagOption> &tags)
{
    m_updating = true;
    m_tags = tags;
    m_items.clear();
    m_flows.clear();
    m_views.clear();
    while (m_tabs->count())
    {
        QWidget *p = m_tabs->widget(0);
        m_tabs->removeTab(0);
        p->deleteLater();
    }
    for (const auto &t : m_tags)
    {
        TagFlowScene *f = m_flows.value(t.typeName);
        if (!f)
        {
            auto *v = new TagGraphicsView(m_tabs);
            v->setObjectName(QStringLiteral("WorkAvailableTagList"));
            f = new TagFlowScene(panelWidth - 25, v);
            f->setObjectName(QStringLiteral("WorkAvailableTagFlow"));
            v->setScene(f);
            applyViewStyle(v);
            m_flows.insert(t.typeName, f);
            m_views.insert(t.typeName, v);
            m_tabs->addTab(v, t.typeName);
        }
        auto *c = new TagCard(t.id, t.name, t.color, m_themes, [this, id = t.id] {
            if (!m_updating)
                setTagSelected(id, true);
        });
        c->setToolTip(t.detail);
        // Match Python's TagSelector5 construction path: populate the whole
        // scene first, then perform one layout pass per tag group. Relayout on
        // every insertion turns initial loading into quadratic work.
        f->addCard(c, false);
        m_items.insert(t.id, c);
    }
    QSet<qint64> available;
    for (const auto &t : m_tags)
        available.insert(t.id);
    m_selectedIds.intersect(available);
    QList<qint64> order = m_selectedOrder;
    m_selectedOrder.clear();
    for (qint64 id : order)
        if (m_selectedIds.contains(id))
            m_selectedOrder.append(id);
    for (qint64 id : std::as_const(m_selectedIds))
        if (!m_selectedOrder.contains(id))
            m_selectedOrder.append(id);
    for (auto *c : std::as_const(m_selectedItems))
        m_selectedFlow->removeCard(c);
    m_selectedItems.clear();
    for (qint64 id : std::as_const(m_selectedOrder))
    {
        const TagOption *t = tagById(id);
        if (!t)
            continue;
        auto *c = new TagCard(id, t->name, t->color, m_themes,
                              [this, id] { QTimer::singleShot(0, this, [this, id] { setTagSelected(id, false); }); });
        m_selectedItems.insert(id, c);
        m_selectedFlow->addCard(c);
    }
    m_updating = false;
    updateAvailableVisibility();
    applyTabStyle();
    rebuildSearch(m_search->text());
}
const TagOption *WorkTagSelector::tagById(qint64 id) const
{
    for (const auto &t : m_tags)
        if (t.id == id)
            return &t;
    return nullptr;
}
void WorkTagSelector::applyViewStyle(QWidget *w) const
{
    ThemeTokens t = themeTokens(m_themes);
    const QVariant modified = w->property("addWorkModified");
    const QString border = modified.toBool() ? t.warning : t.border;
    const QString radius = modified.isValid() ? QStringLiteral("5px") : t.radiusMd;
    w->setStyleSheet(QStringLiteral("QGraphicsView{border:%1 dashed "
                                    "%2;border-radius:%3;background:%4}")
                         .arg(t.borderWidth, border, radius, t.pageBackground));
}
void WorkTagSelector::applyTabStyle() const
{
    ThemeTokens t = themeTokens(m_themes);
    m_tabs->setStyleSheet(QStringLiteral("QTabWidget::pane{border:none;background:transparent;margin:0;"
                                         "padding:0}QTabBar::tab{background:%1;color:%2;border:%3 solid "
                                         "%4;padding:8px "
                                         "18px;font-size:%5;font-family:'%6';min-width:90px;border-top-left-"
                                         "radius:%7;border-bottom-left-radius:%7;margin-right:4px}QTabBar::"
                                         "tab:hover{background:%8;color:%2}QTabBar::tab:selected{background:%"
                                         "9;color:%10;font-weight:bold}")
                              .arg(t.pageBackground, t.text, t.borderWidth, t.border, t.fontSizeMiddle,
                                   t.fontFamilyBase, t.radiusMd, t.inputBackground, t.primary, t.textInverse));
}
} // namespace darkeye
