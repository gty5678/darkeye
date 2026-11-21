#include "ChamferStyle.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QCursor>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QTextEdit>
#include <QPainter>
#include <QGraphicsEffect>
#include <QGroupBox>
#include <QScrollBar>
#include <QStylePainter>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QtMath>
#include "ChamferPath.h"
#include <QRegularExpression>
#include <QWidget>
#include <QSet>

namespace darkeye {
namespace {
QSet<QString> chamferNames;
constexpr int cut = 6;

void paintScrollBar(QScrollBar *bar)
{
    QStyleOptionSlider option;
    option.initFrom(bar);
    option.orientation = bar->orientation();
    option.minimum = bar->minimum();
    option.maximum = bar->maximum();
    option.sliderPosition = bar->sliderPosition();
    option.sliderValue = bar->value();
    option.singleStep = bar->singleStep();
    option.pageStep = bar->pageStep();
    option.upsideDown = bar->invertedAppearance();
    const bool horizontal = option.orientation == Qt::Horizontal;
    if (horizontal) option.state |= QStyle::State_Horizontal;

    constexpr auto control = QStyle::CC_ScrollBar;
    constexpr auto handle = QStyle::SC_ScrollBarSlider;
    const QStyle *style = bar->style();
    option.subControls = QStyle::SC_All;
    const QRect handleRect = style->subControlRect(control, &option, handle, bar);
    if (bar->isSliderDown()) {
        option.activeSubControls = handle;
        option.state |= QStyle::State_Sunken;
    } else if (bar->underMouse()) {
        option.activeSubControls = style->hitTestComplexControl(
            control, &option, bar->mapFromGlobal(QCursor::pos()), bar);
    }

    QStylePainter painter(bar);
    option.subControls = QStyle::SC_All & ~QStyle::SubControls(handle);
    painter.drawComplexControl(control, option);

    const QRectF bounds = horizontal ? QRectF(handleRect).adjusted(2, 1, -2, -1)
                                     : QRectF(handleRect).adjusted(1, 2, -1, -2);
    if (bounds.isEmpty()) return;
    const QPointF center = bounds.center();
    // Equal offsets along and across the bar make both tip angles 90 degrees.
    const qreal tip = qMin(bounds.width(), bounds.height()) / 2;
    QPainterPath path;
    if (horizontal) {
        path.moveTo(bounds.left(), center.y());
        path.lineTo(bounds.left() + tip, center.y() - tip);
        path.lineTo(bounds.right() - tip, center.y() - tip);
        path.lineTo(bounds.right(), center.y());
        path.lineTo(bounds.right() - tip, center.y() + tip);
        path.lineTo(bounds.left() + tip, center.y() + tip);
    } else {
        path.moveTo(center.x(), bounds.top());
        path.lineTo(center.x() + tip, bounds.top() + tip);
        path.lineTo(center.x() + tip, bounds.bottom() - tip);
        path.lineTo(center.x(), bounds.bottom());
        path.lineTo(center.x() - tip, bounds.bottom() - tip);
        path.lineTo(center.x() - tip, bounds.top() + tip);
    }
    path.closeSubpath();

    // Use Qt's styled handle fill inside the pointed outline.
    option.rect = handleRect;
    option.subControls = handle;
    if (!(option.activeSubControls & handle)) option.state &= ~QStyle::State_MouseOver;
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setClipPath(path);
    painter.drawControl(QStyle::CE_ScrollBarSlider, option);
}

class ChamferOptionDelegate final : public QStyledItemDelegate
{
public:
    explicit ChamferOptionDelegate(QAbstractItemView *view, bool highlightHover = false)
        : QStyledItemDelegate(view), m_highlightHover(highlightHover) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const bool hovered = m_highlightHover && option.state.testFlag(QStyle::State_MouseOver)
            && option.state.testFlag(QStyle::State_Enabled);
        if (!selected && !hovered) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        QStyleOptionViewItem item(option);
        initStyleOption(&item, index);
        const auto group = !(item.state & QStyle::State_Enabled) ? QPalette::Disabled
            : (item.state & QStyle::State_Active) ? QPalette::Active : QPalette::Inactive;
        const QRectF background = QRectF(item.rect).adjusted(1, 1, -1, -1);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->fillPath(chamferPath(background, cut),
                          item.palette.brush(group, selected ? QPalette::Highlight : QPalette::AlternateBase));
        painter->restore();

        item.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver | QStyle::State_HasFocus);
        item.backgroundBrush = Qt::NoBrush;
        if (selected) {
            item.palette.setBrush(group, QPalette::Text,
                                  item.palette.brush(group, QPalette::HighlightedText));
        }
        const QStyle *style = item.widget ? item.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &item, painter, item.widget);
    }

private:
    bool m_highlightHover;
};

// Work with the styled source, so hover/focus/disabled colours and border
// widths come from Qt's actual rendering rather than an approximation of QSS.
class ChamferEffect final : public QGraphicsEffect
{
public:
    explicit ChamferEffect(QWidget *parent) : QGraphicsEffect(parent), m_squareTop(
        qobject_cast<QGroupBox *>(parent) || qobject_cast<QTabWidget *>(parent)) {}
private:
    bool m_squareTop;
protected:
    void draw(QPainter *painter) override
    {
        QPoint offset;
        const QPixmap source = sourcePixmap(Qt::LogicalCoordinates, &offset,
                                             QGraphicsEffect::NoPad);
        if (source.isNull()) return;
        const qreal dpr = source.devicePixelRatio();
        QImage result = source.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        result.setDevicePixelRatio(dpr);
        const QRectF bounds(QPointF(), source.deviceIndependentSize());
        const qreal corner = qMin(qreal(cut), qMin(bounds.width(), bounds.height()) / 3);
        QPainterPath outer = chamferPath(bounds, corner);
        if (m_squareTop) {
            QPainterPath top;
            top.addRect(QRectF(0, 0, bounds.width(), corner));
            outer = outer.united(top);
        }

        // Inspect the uninterrupted top edge, away from text and subcontrols.
        // A solid fill (primary buttons, borderless hover backgrounds) needs
        // only the silhouette; a contrasting edge needs a complete inset ring.
        const int x = result.width() / 2;
        for (bool bottom : {false, true}) {
            if (!bottom && m_squareTop) continue;
            const auto edgePixel = [&](int row) {
                return result.pixelColor(x, bottom ? result.height() - 1 - row : row);
            };
            const QColor edge = edgePixel(0);
            int borderPixels = 0;
            const int limit = qMin(result.height(), qCeil(4 * dpr));
            while (borderPixels < limit && edgePixel(borderPixels) == edge)
                ++borderPixels;
            const bool hasBorder = edge.alpha() > 0 && borderPixels > 0 && borderPixels < limit;
            if (hasBorder) {
                const QColor transition = edgePixel(borderPixels);
                const QColor interior = edgePixel(qMin(borderPixels + 1, result.height() - 1));
                const auto channels = [](const QColor &colour) {
                    return QList<qreal>{colour.redF(), colour.greenF(), colour.blueF(), colour.alphaF()};
                };
                const auto edgeChannels = channels(edge);
                const auto transitionChannels = channels(transition);
                const auto interiorChannels = channels(interior);
                qreal numerator = 0;
                qreal denominator = 0;
                for (int channel = 0; channel < 4; ++channel) {
                    const qreal delta = edgeChannels[channel] - interiorChannels[channel];
                    numerator += (transitionChannels[channel] - interiorChannels[channel]) * delta;
                    denominator += delta * delta;
                }
                const qreal coverage = denominator > 0 ? qBound(qreal(0), numerator / denominator, qreal(1)) : 0;
                const qreal width = (borderPixels + coverage) / dpr;
                // Offset the diagonal by the same perpendicular distance as the
                // horizontal edges, keeping all eight sides equally thick.
                const qreal innerCorner = qMax(qreal(0), corner - (2 - qSqrt(2.0)) * width);
                QPainterPath ring = outer;
                ring.addPath(chamferPath(bounds.adjusted(width, width, -width, -width), innerCorner));
                ring.setFillRule(Qt::OddEvenFill);
                QPainter imagePainter(&result);
                imagePainter.setRenderHint(QPainter::Antialiasing);
                imagePainter.setClipRect(QRectF(0, bottom ? bounds.height() / 2 : 0,
                                               bounds.width(), bounds.height() / 2));
                imagePainter.fillPath(ring, edge);
            }
        }
        // DestinationIn provides subpixel coverage at every diagonal, including
        // fractional display scales. No QWidget mask or extra child is needed.
        QImage coverage(result.size(), QImage::Format_ARGB32_Premultiplied);
        coverage.setDevicePixelRatio(dpr);
        coverage.fill(Qt::transparent);
        {
            QPainter maskPainter(&coverage);
            maskPainter.setRenderHint(QPainter::Antialiasing);
            maskPainter.fillPath(outer, Qt::white);
        }
        {
            QPainter imagePainter(&result);
            imagePainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            imagePainter.drawImage(QPointF(), coverage);
        }
        painter->drawImage(QPointF(offset), result);
    }
};

bool usesCutCorners(QWidget *widget)
{
    // Embedded line edits share their containing combo/spin box silhouette.
    if (widget->isWindow() || widget->inherits("darkeye::ColorPicker"))
        return false;
    const QString name = widget->objectName();
    // The borderless disclosure header can redraw directly while its body resizes.
    if (name == QLatin1String("DesignCollapsibleToggle"))
        return false;
    if (qobject_cast<QLineEdit *>(widget)
        && (qobject_cast<QAbstractSpinBox *>(widget->parentWidget())
            || qobject_cast<QComboBox *>(widget->parentWidget()))) return false;
    if (qobject_cast<QPushButton *>(widget) || qobject_cast<QToolButton *>(widget)
        || qobject_cast<QLineEdit *>(widget)
        || qobject_cast<QComboBox *>(widget) || qobject_cast<QTextEdit *>(widget)
        || qobject_cast<QPlainTextEdit *>(widget))
        return true;
    return chamferNames.contains(name)
        || name == QLatin1String("ShelfFilterBar")
        || name == QLatin1String("WorkTagPanel")
        || name == QLatin1String("WorkspaceSearch")
        || name == QLatin1String("DesignCard")
        || name == QLatin1String("DesignCollapsibleSection");
}

void updateCorners(QWidget *widget)
{
    if (!widget->graphicsEffect()) widget->setGraphicsEffect(new ChamferEffect(widget));
}

void updateSearchResults(QWidget *widget)
{
    if (widget->objectName() != QLatin1String("workspaceSearchResults")
        || !widget->parentWidget() || !widget->parentWidget()->property("searchPopup").toBool())
        return;

    auto *view = qobject_cast<QAbstractItemView *>(widget);
    if (!view) return;
    updateCorners(view);
    if (!dynamic_cast<ChamferOptionDelegate *>(view->itemDelegate())) {
        view->setMouseTracking(true);
        view->setItemDelegate(new ChamferOptionDelegate(view, true));
    }
}

void updateComboPopup(QWidget *widget)
{
    if (widget->windowType() != Qt::Popup) return;

    QAbstractItemView *view = nullptr;
    if (qobject_cast<QComboBox *>(widget->parentWidget())) {
        view = widget->findChild<QAbstractItemView *>(Qt::FindDirectChildrenOnly);
    } else if (widget->objectName() == QLatin1String("DesignComboBoxPopup")) {
        // The editable maker selector also opens a standalone completer view.
        view = qobject_cast<QAbstractItemView *>(widget);
    }
    if (!view) return;

    widget->setAttribute(Qt::WA_TranslucentBackground);
    // Let the effect capture the styled frame of a standalone popup view.
    widget->setAttribute(Qt::WA_NoSystemBackground, false);
    updateCorners(widget);
    if (!dynamic_cast<ChamferOptionDelegate *>(view->itemDelegate())) {
        constexpr auto name = "darkeyeComboOptionDelegate";
        auto *delegate = view->findChild<QStyledItemDelegate *>(
            QLatin1String(name), Qt::FindDirectChildrenOnly);
        if (!delegate) {
            delegate = new ChamferOptionDelegate(view);
            delegate->setObjectName(QLatin1String(name));
        }
        view->setItemDelegate(delegate);
    }
}
} // namespace

ChamferStyle::ChamferStyle(QObject *parent) : QObject(parent)
{
    qApp->installEventFilter(this);
}

QString ChamferStyle::flattenCorners(QString styleSheet)
{
    // Keep indicator, scrollbar track and slider styles intact.
    QStringList rules = styleSheet.split(QLatin1Char('}'));
    static const QRegularExpression radius(QStringLiteral(
        "border-(?:(?:top|bottom)-(?:left|right)-)?radius\\s*:\\s*[^;\\n}]+"));
    static const QRegularExpression id(QStringLiteral("#([A-Za-z_][A-Za-z0-9_]*)"));
    for (QString &rule : rules) {
        const auto brace = rule.indexOf(QLatin1Char('{'));
        if (brace < 0) continue;
        const QString selector = rule.left(brace);
        if (selector.contains(QStringLiteral("::indicator"))
            || selector.contains(QStringLiteral("QScrollBar"))
            || selector.contains(QStringLiteral("QSlider"))) continue;
        QString body = rule.mid(brace);
        if (body.contains(radius)) {
            auto matches = id.globalMatch(selector);
            while (matches.hasNext()) chamferNames.insert(matches.next().captured(1));
        }
        body.replace(radius, QStringLiteral("border-radius: 0"));
        rule = rule.left(brace) + body;
    }
    return rules.join(QLatin1Char('}'));
}

bool ChamferStyle::eventFilter(QObject *object, QEvent *event)
{
    switch (event->type()) {
    case QEvent::Paint:
        if (auto *bar = qobject_cast<QScrollBar *>(object)) {
            paintScrollBar(bar);
            return true;
        }
        return false;
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
        // Repaint after Qt has updated isSliderDown().
        if (auto *bar = qobject_cast<QScrollBar *>(object)) bar->update();
        return false;
    case QEvent::Polish:
    case QEvent::StyleChange:
    case QEvent::Show:
    case QEvent::Resize: break;
    default: return false;
    }
    auto *widget = qobject_cast<QWidget *>(object);
    if (!widget) return false;
    if (event->type() == QEvent::Polish || event->type() == QEvent::StyleChange) {
        const QString original = widget->styleSheet();
        if (original.contains(QLatin1String("radius"))) {
            const auto *cached = m_flattenedStyles.object(original);
            const QString flat = cached ? *cached : flattenCorners(original);
            if (!cached) m_flattenedStyles.insert(original, new QString(flat));
            if (flat != original) {
                m_flattenedStyles.insert(flat, new QString(flat));
                widget->setStyleSheet(flat);
            }
        }
    }
    if (event->type() == QEvent::Show || event->type() == QEvent::Polish) {
        updateComboPopup(widget);
        updateSearchResults(widget);
    }
    if (usesCutCorners(widget)) updateCorners(widget);
    return false;
}
} // namespace darkeye
