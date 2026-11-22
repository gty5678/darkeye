#include "darkeye_ui/components/TokenControls.h"

#include <QPainter>
#include <QStyle>
#include <QStyleOptionSpinBox>
#include <QTabBar>

namespace darkeye {

TokenCheckBox::TokenCheckBox(const QString &text, QWidget *parent)
    : QCheckBox(text, parent)
{
    setObjectName(QStringLiteral("DesignCheckBox"));
}

TokenRadioButton::TokenRadioButton(const QString &text, QWidget *parent)
    : QRadioButton(text, parent)
{
    setObjectName(QStringLiteral("DesignRadioButton"));
}

TokenSpinBox::TokenSpinBox(QWidget *parent) : QSpinBox(parent)
{
    setObjectName(QStringLiteral("DesignSpinBox"));
}

void TokenSpinBox::paintEvent(QPaintEvent *event)
{
    QSpinBox::paintEvent(event);

    QStyleOptionSpinBox option;
    initStyleOption(&option);
    QPainter painter(this);
    const auto colorGroup = isEnabled() ? QPalette::Active : QPalette::Disabled;
    painter.setPen(QPen(option.palette.color(colorGroup, QPalette::Text), 1.0,
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

    const auto drawChevron = [&painter, this, &option](QStyle::SubControl subControl,
                                                         bool pointsUp) {
        const QRect buttonRect = style()->subControlRect(QStyle::CC_SpinBox, &option,
                                                         subControl, this);
        constexpr int iconSize = 12;
        const QRect iconRect(QPoint(buttonRect.center().x() - iconSize / 2,
                                    buttonRect.center().y() - iconSize / 2),
                             QSize(iconSize, iconSize));
        const qreal centerX = iconRect.center().x() + 0.5;
        const qreal tipY = iconRect.top() + (pointsUp ? 4.5 : 7.5);
        const qreal baseY = iconRect.top() + (pointsUp ? 7.5 : 4.5);
        painter.drawPolyline(QPolygonF({QPointF(iconRect.left() + 3, baseY),
                                        QPointF(centerX, tipY),
                                        QPointF(iconRect.right() - 2, baseY)}));
    };

    // Matches darkeye_ui's 12 px SVG_CHEVRON_UP and SVG_CHEVRON_DOWN assets.
    drawChevron(QStyle::SC_SpinBoxUp, true);
    drawChevron(QStyle::SC_SpinBoxDown, false);
}

TokenGroupBox::TokenGroupBox(const QString &title, QWidget *parent)
    : QGroupBox(title, parent)
{
    setObjectName(QStringLiteral("DesignGroupBox"));
}

TokenTabWidget::TokenTabWidget(QWidget *parent) : QTabWidget(parent)
{
    setObjectName(QStringLiteral("DesignTabWidget"));
    tabBar()->setObjectName(QStringLiteral("DesignTabBar"));
}

ProgressBar::ProgressBar(QWidget *parent) : QProgressBar(parent)
{
    setObjectName(QStringLiteral("DesignProgressBar"));
    setTextVisible(true);
}

IndeterminateProgressBar::IndeterminateProgressBar(QWidget *parent)
    : ProgressBar(parent)
{
    start();
}

void IndeterminateProgressBar::start() { setRange(0, 0); }

void IndeterminateProgressBar::stop(int value)
{
    setRange(0, 100);
    setValue(qBound(0, value, 100));
}

TransparentWidget::TransparentWidget(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("TransparentWidget"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_StyledBackground);
    setAutoFillBackground(false);
}

} // namespace darkeye
