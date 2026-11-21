#include "darkeye_ui/components/VerticalText.h"

#include "darkeye_ui/theme/ThemeService.h"
#include "darkeye_ui/layouts/VerticalTextLayout.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

namespace darkeye {
namespace {

ThemeTokens tokensFor(ThemeService *themes)
{
    return ThemeService::tokens(themes == nullptr ? ThemeId::Light
                                                   : themes->current(),
                                themes == nullptr ? QString()
                                                   : themes->customPrimary());
}

void drawVerticalText(QPainter &painter, const QRect &area, const QString &source)
{
    const QFontMetrics metrics(painter.font());
    const qreal lineSpacing = metrics.height() * 0.05;
    const qreal columnSpacing = metrics.height() * 0.1;
    const VerticalTextLayout layout(metrics, lineSpacing, columnSpacing);
    painter.save();
    painter.translate(area.topLeft());
    for (const VerticalTextBlock &block :
         layout.calculateLayout(source, area.width(), area.height())) {
        if (block.english) {
            painter.save();
            painter.translate(block.rect.center());
            painter.rotate(block.rotation);
            const qreal textWidth = metrics.horizontalAdvance(block.text);
            const qreal baselineOffset = (metrics.ascent() - metrics.descent()) / 2.0;
            painter.drawText(QPointF(-textWidth / 2.0, baselineOffset), block.text);
            painter.restore();
        } else {
            painter.drawText(block.rect, Qt::AlignCenter, block.text);
        }
    }
    painter.restore();
}

} // namespace

VerticalTextLabel::VerticalTextLabel(const QString &text, const QString &tone,
                                     ThemeService *themes, QWidget *parent)
    : QWidget(parent), m_text(VerticalTextLayout::replaceEllipsis(text)),
      m_tone(tone), m_themes(themes)
{
    setObjectName(QStringLiteral("DesignVerticalTextLabel"));
    setProperty("tone", tone);
    QFont value = font();
    value.setPointSize(value.pointSize() + 2);
    setFont(value);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    if (m_themes != nullptr) connect(m_themes, &ThemeService::themeChanged,
                                    this, [this] { update(); });
}
QString VerticalTextLabel::text() const { return m_text; }
void VerticalTextLabel::setText(const QString &text)
{
    m_text = VerticalTextLayout::replaceEllipsis(text);
    updateGeometry();
    update();
}
void VerticalTextLabel::setTextColor(const QColor &color) { m_overrideColor = color; update(); }
void VerticalTextLabel::setTone(const QString &tone)
{
    m_tone = tone; setProperty("tone", tone); update();
}
QSize VerticalTextLabel::minimumSizeHint() const
{
    const QFontMetrics metrics(font());
    const VerticalTextLayout layout(metrics, metrics.height() * 0.05,
                                    metrics.height() * 0.1);
    return layout.calculateSize(m_text, height());
}
QSize VerticalTextLabel::sizeHint() const { return minimumSizeHint(); }
void VerticalTextLabel::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setFont(font());
    painter.setPen(effectiveColor());
    drawVerticalText(painter, rect(), m_text);
}
QColor VerticalTextLabel::effectiveColor() const
{
    if (m_overrideColor.isValid()) return m_overrideColor;
    const ThemeTokens tokens = tokensFor(m_themes);
    return QColor(m_tone == QStringLiteral("inverse") ? tokens.textInverse : tokens.text);
}

TokenVLabel::TokenVLabel(const QString &text, ThemeService *themes, QWidget *parent)
    : QLabel(text, parent), m_themes(themes)
{
    setObjectName(QStringLiteral("DesignTokenVLabel"));
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    const ThemeTokens tokens = tokensFor(themes);
    m_background = QColor(tokens.pageBackground);
    m_textColor = QColor(tokens.text);
    m_border = QColor(tokens.border);
    m_hoverColor = QColor(tokens.primary);
    m_flashTimer = new QTimer(this);
    connect(m_flashTimer, &QTimer::timeout, this, [this] {
        if (QDateTime::currentMSecsSinceEpoch() >= m_flashEnd) {
            m_flashTimer->stop(); m_inverted = false; update(); return;
        }
        m_inverted = !m_inverted; update();
    });
    if (m_themes != nullptr) connect(m_themes, &ThemeService::themeChanged,
                                    this, [this] {
        const ThemeTokens values = tokensFor(m_themes);
        if (!m_explicitBackground) m_background = QColor(values.pageBackground);
        if (!m_explicitTextColor) m_textColor = QColor(values.text);
        if (!m_explicitBorder) m_border = QColor(values.border);
        if (!m_explicitHoverColor) m_hoverColor = QColor(values.primary);
        update();
    });
    updateSize();
}

TokenVLabel::TokenVLabel(const QString &text, const QColor &background,
                         const QColor &textColor, int fixedWidth,
                         int fixedHeight, const QColor &border,
                         const QColor &hover, ThemeService *themes,
                         QWidget *parent)
    : TokenVLabel(text, themes, parent)
{
    if (background.isValid()) {
        m_background = background;
        m_explicitBackground = true;
    }
    if (textColor.isValid()) {
        m_textColor = textColor;
        m_explicitTextColor = true;
    }
    if (border.isValid()) {
        m_border = border;
        m_explicitBorder = true;
    }
    if (hover.isValid()) {
        m_hoverColor = hover;
        m_explicitHoverColor = true;
    }
    if (fixedWidth > 0 && fixedHeight > 0) setFixedSize(fixedWidth, fixedHeight);
    update();
}

void TokenVLabel::setTextDynamic(const QString &text) { setText(text); updateSize(); update(); }
void TokenVLabel::setColors(const QColor &background, const QColor &text,
                            const QColor &hover)
{
    m_background = background;
    m_textColor = text;
    m_explicitBackground = true;
    m_explicitTextColor = true;
    if (hover.isValid()) {
        m_hoverColor = hover;
        m_explicitHoverColor = true;
    }
    update();
}
void TokenVLabel::setBorderColor(const QColor &border)
{
    m_border = border;
    m_explicitBorder = border.isValid();
    update();
}
void TokenVLabel::setHoverColor(const QColor &hover)
{
    m_hoverColor = hover;
    m_explicitHoverColor = hover.isValid();
    update();
}
void TokenVLabel::flashInvert(int duration, int interval)
{
    m_flashEnd = QDateTime::currentMSecsSinceEpoch() + qMax(0, duration);
    m_flashTimer->start(qMax(16, interval));
}
QSize TokenVLabel::sizeHint() const { return size(); }
void TokenVLabel::updateSize()
{
    const QFontMetrics metrics(font());
    const int width = qRound(metrics.horizontalAdvance(QStringLiteral("中")) * 1.7);
    setFixedSize(width, qMax(width * 2, metrics.height() * text().size() + width));
}
void TokenVLabel::paintEvent(QPaintEvent *)
{
    const qreal cut = width() * 0.2;
    QPainterPath outer;
    outer.moveTo(cut, 0); outer.lineTo(width() - cut, 0);
    outer.lineTo(width(), cut); outer.lineTo(width(), height() - cut);
    outer.lineTo(width() - cut, height()); outer.lineTo(cut, height());
    outer.lineTo(0, height() - cut); outer.lineTo(0, cut); outer.closeSubpath();
    QPainterPath hole;
    const qreal radius = width() * 0.1;
    hole.addEllipse(QPointF(width() / 2.0, cut + radius), radius, radius);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor background = m_inverted ? m_textColor : m_background;
    QColor textColor = m_inverted ? m_background : (m_hovered ? m_hoverColor : m_textColor);
    painter.setPen(m_border); painter.setBrush(background); painter.drawPath(outer);
    painter.setBrush(Qt::NoBrush); painter.drawPath(hole);
    painter.setPen(textColor);
    drawVerticalText(painter, rect().adjusted(2, qRound(cut + radius * 2), -2, -2), text());
}
void TokenVLabel::enterEvent(QEnterEvent *event) { m_hovered = true; update(); QLabel::enterEvent(event); }
void TokenVLabel::leaveEvent(QEvent *event) { m_hovered = false; update(); QLabel::leaveEvent(event); }

TokenVerticalTabBar::TokenVerticalTabBar(ThemeService *themes, QWidget *parent)
    : QTabBar(parent), m_themes(themes)
{
    setObjectName(QStringLiteral("VerticalTabBar"));
    setShape(QTabBar::RoundedWest);
    setDrawBase(false);
    const auto applyTokenFont = [this] {
        const ThemeTokens tokens = tokensFor(m_themes);
        QFont tokenFont(tokens.fontFamilyBase);
        QString pixelSize = tokens.fontSizeMiddle;
        pixelSize.remove(QStringLiteral("px"), Qt::CaseInsensitive);
        tokenFont.setPixelSize(qMax(1, pixelSize.toInt()));
        setFont(tokenFont);
        updateGeometry();
        update();
    };
    applyTokenFont();
    if (m_themes != nullptr) connect(m_themes, &ThemeService::themeChanged,
                                    this, applyTokenFont);
}
QSize TokenVerticalTabBar::tabSizeHint(int index) const
{
    const QString text = VerticalTextLayout::replaceEllipsis(tabText(index));
    const QFontMetrics metrics(font());
    const qreal lineSpacing = metrics.height() * 0.05;
    const int characterHeight = metrics.height();
    const int characterWidth = metrics.maxWidth();
    const int verticalPadding = int(characterHeight * 0.2);
    const int horizontalPadding = int(characterWidth * 0.3);
    qreal totalHeight = qMax(16, verticalPadding * 2);
    for (const VerticalTextLayout::TextRun &run :
         VerticalTextLayout::splitTextBlocks(text)) {
        if (run.english)
            totalHeight += metrics.boundingRect(run.text).width() + lineSpacing;
        else
            totalHeight += (characterHeight + lineSpacing) * run.text.size();
    }
    int maximumCharacterWidth = 0;
    for (const QChar character : text)
        if (!character.isSpace())
            maximumCharacterWidth = qMax(maximumCharacterWidth,
                                         metrics.horizontalAdvance(character));
    const int minimumWidth = maximumCharacterWidth > 0
        ? int(maximumCharacterWidth * 1.5) : 40;
    const int idealWidth = maximumCharacterWidth > 0
        ? maximumCharacterWidth + horizontalPadding * 2 : 60;
    const int maximumWidth = maximumCharacterWidth > 0
        ? int(maximumCharacterWidth * 2.5) : 120;
    return {qMax(minimumWidth, qMin(idealWidth, maximumWidth)), int(totalHeight) + (index == 0 ? 14 : 0)};
}
void TokenVerticalTabBar::paintEvent(QPaintEvent *)
{
    const ThemeTokens tokens = tokensFor(m_themes);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor page(tokens.pageBackground);
    const QColor primary(tokens.primary);
    const QColor activeText = qGray(primary.rgb()) >= 128 ? Qt::black : Qt::white;
    const QColor rail = page.lightness() > 128 ? page.darker(103) : page.lighter(115);
    painter.fillRect(rect(), rail);
    // The active tab has a cut at the left edge and
    // inverse shoulders where it meets the right edge of the rail.
    QRectF active = tabRect(currentIndex());
    active.setRight(width());
    if (currentIndex() == 0)
        active.setTop(active.top() + 14);
    if (currentIndex() >= 0) {
        const qreal cut = qMin(8.0, active.width() / 3.0);
        QPainterPath path;
        path.moveTo(active.left(), active.top() + cut);
        path.lineTo(active.left() + cut, active.top());
        path.lineTo(active.right() - cut, active.top());
        path.lineTo(active.right(), active.top() - cut);
        path.lineTo(active.right(), active.bottom() + cut);
        path.lineTo(active.right() - cut, active.bottom());
        path.lineTo(active.left() + cut, active.bottom());
        path.lineTo(active.left(), active.bottom() - cut);
        path.closeSubpath();
        painter.fillPath(path, primary);
    }
    for (int index = 0; index < count(); ++index) {
        const QString text = tabText(index);
        const bool selected = index == currentIndex();
        // Short dividers keep the inactive tabs on one continuous rail.
        if (index > 0 && !selected && index - 1 != currentIndex()) {
            QRect bounds = tabRect(index);
            bounds.setWidth(width());
            painter.setPen(QColor(tokens.border));
            painter.drawLine(QPointF(bounds.center().x() - 4, bounds.top()),
                             QPointF(bounds.center().x() + 4, bounds.top()));
        }
        const QColor labelColor = selected ? activeText
                                           : QColor(isTabEnabled(index) ? tokens.text : tokens.textDisabled);
        painter.setPen(labelColor);
        QFont labelFont = font();
        labelFont.setBold(selected);
        painter.setFont(labelFont);
        painter.setRenderHint(QPainter::TextAntialiasing);
        const QFontMetrics metrics(painter.font());
        const qreal lineSpacing = metrics.height() * .05;
        const qreal charHeight = metrics.height() + lineSpacing;
        const int charWidth = metrics.maxWidth();
        QRect rect = tabRect(index);
        rect.setWidth(width());
        const int x = rect.left() + (rect.width() - charWidth) / 2;
        qreal y = rect.top() + int(metrics.height() * .3) + (index == 0 ? 14 : 0);
        for (const auto &run : VerticalTextLayout::splitTextBlocks(VerticalTextLayout::replaceEllipsis(text))) {
            if (run.english) {
                const QRect bounds = metrics.boundingRect(run.text);
                const qreal width = bounds.width();
                painter.save();
                painter.translate(x + charWidth / 2.0, y + width / 2.0);
                painter.rotate(90);
                painter.drawText(QPointF(-width / 2.0, metrics.ascent() - bounds.height() / 2.0), run.text);
                painter.restore();
                y += width + lineSpacing;
            } else {
                for (QChar character : run.text) {
                    painter.drawText(QRect(x, int(y), charWidth, int(charHeight)), Qt::AlignCenter, QString(character));
                    y += charHeight;
                }
            }
        }
    }
}

} // namespace darkeye
