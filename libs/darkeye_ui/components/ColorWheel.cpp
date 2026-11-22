#include "darkeye_ui/components/ColorWheel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QtMath>
#include <QDebug>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QClipboard>
#include <QApplication>
#include <QSignalBlocker>
#include <cmath>
#include <QConicalGradient>
#include <QKeyEvent>



namespace darkeye {

HexColorLineEdit::HexColorLineEdit(QWidget *parent)
    : QLineEdit(parent)
{
    setPlaceholderText("");
    setMaxLength(7);
    QRegularExpression regex("[0-9a-fA-F]{0,6}");
    setValidator(new QRegularExpressionValidator(regex, this));
    setStyleSheet("border-radius: 6px; font-size: 16px; font-family: 'Consolas';");
    connect(this, &QLineEdit::textChanged, this, &HexColorLineEdit::onTextChanged);
}

void HexColorLineEdit::setText(const QString &text)
{
    m_settingText = true;
    QLineEdit::setText(text);
    m_settingText = false;
}

void HexColorLineEdit::onTextChanged(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    QString cleaned;
    cleaned.reserve(6);
    for (const QChar &ch : text) {
        if (ch.isDigit() || (ch >= QLatin1Char('a') && ch <= QLatin1Char('f')) || (ch >= QLatin1Char('A') && ch <= QLatin1Char('F'))) {
            cleaned.append(ch.toUpper());
        }
    }
    if (cleaned.size() > 6) {
        cleaned = cleaned.left(6);
    }
    cleaned.prepend("#");
    if (cleaned != text) {
        m_settingText = true;
        QLineEdit::setText(cleaned);
        setCursorPosition(cleaned.size());
        m_settingText = false;
        return;
    }
    if (m_settingText) {
        return;
    }
    if (cleaned.size() == 7) {
        emit validHexColor(cleaned);
    }
}

void HexColorLineEdit::focusOutEvent(QFocusEvent *event)
{
    QString text = this->text().toUpper();
    if (!text.isEmpty() && !text.startsWith("#")) {
        text.prepend("#");
    }
    if (text.size() > 7) {
        text = text.left(7);
    }
    if (text != this->text()) {
        m_settingText = true;
        QLineEdit::setText(text);
        m_settingText = false;
    }
    QLineEdit::focusOutEvent(event);
}

void HexColorLineEdit::keyPressEvent(QKeyEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->key() == Qt::Key_V) {
            QString clipboard = QApplication::clipboard()->text().trimmed().toUpper();
            QString cleaned;
            cleaned.reserve(6);
            for (const QChar &ch : clipboard) {
                if (QStringLiteral("0123456789ABCDEF").contains(ch)) {
                    cleaned.append(ch);
                }
            }
            if (cleaned.isEmpty()) {
                return;
            }
            if (cleaned.size() > 6) {
                cleaned = cleaned.left(6);
            }
            QString result = QStringLiteral("#") + cleaned;

            int current_pos = cursorPosition();
            QString current_text = this->text();
            if (hasSelectedText()) {
                QLineEdit::insert(result);
                return;
            }
            QString new_text = current_text.left(current_pos) + result + current_text.mid(current_pos);
            if (new_text.size() > 7) {
                new_text = new_text.left(7);
            }
            m_settingText = true;
            QLineEdit::setText(new_text);
            setCursorPosition(qMin(current_pos + result.size(), new_text.size()));
            m_settingText = false;
            return;
        }
        QLineEdit::keyPressEvent(event);
        return;
    }

    QString text = event->text().toUpper();

    if (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete ||
        event->key() == Qt::Key_Left || event->key() == Qt::Key_Right ||
        event->key() == Qt::Key_Home || event->key() == Qt::Key_End ||
        event->key() == Qt::Key_Tab) {
        QLineEdit::keyPressEvent(event);
        return;
    }

    if (!text.isEmpty() && QStringLiteral("0123456789ABCDEF").contains(text)) {
        QString current = this->text();
        current.remove('#');
        if (current.size() >= 6 && cursorPosition() > 0) {
            if (cursorPosition() <= 1) {
                QLineEdit::keyPressEvent(event);
            }
            return;
        }
        QLineEdit::keyPressEvent(event);
        return;
    }

    event->ignore();
}


ColorLabel::ColorLabel(const QString &hexColor, bool showText, QWidget *parent)
    : QPushButton(parent), m_color(hexColor), m_showText(showText)
{
    connect(this, &QPushButton::clicked, this, &ColorLabel::handleClicked);
    setCursor(Qt::PointingHandCursor);
    updateAppearance();
}

QString ColorLabel::getColor() const
{
    return m_color;
}

void ColorLabel::setColor(const QString &color)
{
    m_color = color;
    updateAppearance();
}

void ColorLabel::handleClicked()
{
    emit colorClicked(m_color);
    QClipboard *clipboard = QApplication::clipboard();
    if (clipboard) {
        clipboard->setText(m_color);
    }
}

QColor ColorLabel::textColorForBackground(const QColor &color)
{
    float r = color.redF();
    float g = color.greenF();
    float b = color.blueF();
    float luminance = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    return luminance > 0.6f ? QColor(Qt::black) : QColor(Qt::white);
}

void ColorLabel::updateAppearance()
{
    QColor textColor = textColorForBackground(QColor(m_color));
    if (m_showText) {
        setText(m_color);
    } else {
        setText(QString());
    }
    setStyleSheet(
        QString("background-color: %1; border-radius: 6px; color: %2; font-size: 14px; padding: 6px;")
            .arg(m_color, textColor.name())
    );
}


OKLCHColorWheel::OKLCHColorWheel(float l, float c, float h, QWidget *parent): QWidget(parent)
{
    setMinimumSize(300, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true); // Needed for mouse move events
    m_L = l;
    m_C = c;
    m_H = h;
    updateLayout();
    generateOuterWheel();
    generateInnerWheel();
    generateSquare();
}


void OKLCHColorWheel::setL(float l)
{
    float rounded = std::round(l * 1000.0f) / 1000.0f;
    if (qAbs(m_L - rounded) > 0.0005f) {
        m_L = rounded;
        emit LChanged(m_L);
        generateInnerWheel();
        update();
        emit colorChanged(getCurrentColor());
    }
}

void OKLCHColorWheel::setC(float c)
{
    float rounded = std::round(c * 1000.0f) / 1000.0f;
    if (qAbs(m_C - rounded) > 0.0005f) {
        m_C = rounded;
        emit CChanged(m_C);
        generateInnerWheel();
        update();
        emit colorChanged(getCurrentColor());
    }
}

void OKLCHColorWheel::setH(float h)
{
    float rounded = std::round(h * 10.0f) / 10.0f;
    if (qAbs(m_H - rounded) > 0.05f) {
        m_H = rounded;
        emit HChanged(m_H);
        generateSquare();
        update();
        emit colorChanged(getCurrentColor());
    }
}

QColor OKLCHColorWheel::getCurrentColor() const
{
    return oklchToRgb(m_L, m_C, m_H);
}

QString OKLCHColorWheel::getHexColor() const
{
    QColor color = getCurrentColor();
    return QString("#%1%2%3")
        .arg(color.red(), 2, 16, QLatin1Char('0'))
        .arg(color.green(), 2, 16, QLatin1Char('0'))
        .arg(color.blue(), 2, 16, QLatin1Char('0'))
        .toUpper();
}

void OKLCHColorWheel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateLayout();
    generateOuterWheel();
    generateInnerWheel();
    generateSquare();
    update();
}

void OKLCHColorWheel::updateLayout()
{
    float wheelRatio = 1.0f / 5.0f;
    float bigSmallRatio = 1.0f / 5.0f;

    m_wheelRect = rect();
    m_center = m_wheelRect.center();
    int size = qMin(static_cast<int>(m_wheelRect.width()), static_cast<int>(m_wheelRect.height()));

    m_outerRadius = size / 2.0f - 2.0f;
    m_midRadius = m_outerRadius * (1.0f - wheelRatio) + bigSmallRatio * m_outerRadius * wheelRatio;
    m_innerRadius = m_outerRadius * (1.0f - wheelRatio);

    // Side length for the square
    m_side = static_cast<int>(2.0f * m_innerRadius / 1.414f + 0.5f);
}

void OKLCHColorWheel::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.save();
    painter.setClipRect(m_wheelRect);

    // Draw Outer Wheel
    if (!m_outerWheelPixmap.isNull()) {
        painter.drawPixmap(
            static_cast<int>(m_center.x() - m_outerRadius),
            static_cast<int>(m_center.y() - m_outerRadius),
            m_outerWheelPixmap
        );
    }

    // Draw Inner Wheel
    if (!m_innerWheelPixmap.isNull()) {
        painter.drawPixmap(
            static_cast<int>(m_center.x() - m_midRadius),
            static_cast<int>(m_center.y() - m_midRadius),
            m_innerWheelPixmap
        );
    }

    // Draw Square
    if (!m_squarePixmap.isNull() && m_side > 0) {
        float halfSide = m_side / 2.0f;
        painter.drawPixmap(
            static_cast<int>(m_center.x() - halfSide),
            static_cast<int>(m_center.y() - halfSide),
            m_squarePixmap
        );
    }

    // Draw Indicators
    drawHuePointer(painter);
    drawInnerCircle(painter);

    painter.restore();
}

void OKLCHColorWheel::drawHuePointer(QPainter &painter)
{
    painter.save();

    double angleRad = qDegreesToRadians(90.0 - m_H);
    
    QPointF start(
        m_center.x() + m_innerRadius * std::cos(angleRad),
        m_center.y() - m_innerRadius * std::sin(angleRad)
    );
    
    QPointF end(
        m_center.x() + m_outerRadius * std::cos(angleRad),
        m_center.y() - m_outerRadius * std::sin(angleRad)
    );

    double angle = std::atan2(end.y() - start.y(), end.x() - start.x());
    double offsetX = std::sin(angle) * 1.0;
    double offsetY = -std::cos(angle) * 1.0;

    // White line
    painter.setPen(QPen(Qt::white, 1, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(
        QPointF(start.x() + offsetX, start.y() + offsetY),
        QPointF(end.x() + offsetX, end.y() + offsetY)
    );

    // Black line
    painter.setPen(QPen(Qt::black, 1, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(
        QPointF(start.x() - offsetX, start.y() - offsetY),
        QPointF(end.x() - offsetX, end.y() - offsetY)
    );

    painter.restore();
}

void OKLCHColorWheel::drawInnerCircle(QPainter &painter)
{
    painter.save();

    // Calculate position based on L and C
    // L goes from 0 (left) to 1 (right)
    // C goes from 0.37 (bottom) to 0 (top) -- Wait, Python says:
    // self.vm.L = (pos.x() - left) / self.side
    // self.vm.C = (bottom - pos.y()) / self.side * 0.37
    // So C=0 is bottom? No, bottom-y means distance from bottom.
    // If y is at bottom, distance is 0 -> C=0. 
    // If y is at top, distance is side -> C=0.37.
    // So Top is High Chroma, Bottom is Low Chroma (Gray).
    // Let's check Python draw_inner_circle:
    // relative_coordinate=QPointF((self.vm.L-0.5)*self.side,(0.5-(self.vm.C/0.37))*self.side)
    // If C=0.37 -> y = (0.5 - 1)*side = -0.5*side (Top)
    // If C=0    -> y = (0.5 - 0)*side = 0.5*side (Bottom)
    
    QPointF relativePos(
        (m_L - 0.5f) * m_side,
        (0.5f - (m_C / 0.37f)) * m_side
    );
    
    QPointF truePoint = m_center + relativePos;

    // White outer ring
    QPen outerPen(Qt::white);
    outerPen.setWidth(2);
    painter.setPen(outerPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(truePoint, 5, 5);

    // Black inner ring
    QPen innerPen(Qt::black);
    innerPen.setWidth(2);
    painter.setPen(innerPen);
    painter.drawEllipse(truePoint, 7, 7);

    painter.restore();
}

void OKLCHColorWheel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        QPointF pos = event->position();
        if (!m_wheelRect.contains(pos)) {
            QWidget::mousePressEvent(event);
            return;
        }

        float dx = pos.x() - m_center.x();
        float dy = pos.y() - m_center.y();
        float dist = std::sqrt(dx*dx + dy*dy);

        float tolerance = 3.0f;
        bool handled = false;

        // Check Ring
        if (dist >= m_innerRadius - tolerance && dist <= m_outerRadius + tolerance) {
            m_isChoosingRing = true;
            updateHFromPos(pos);
            handled = true;
        }
        // Check Square
        else {
            float halfSide = m_side / 2.0f;
            if (pos.x() >= m_center.x() - halfSide && pos.x() <= m_center.x() + halfSide &&
                pos.y() >= m_center.y() - halfSide && pos.y() <= m_center.y() + halfSide) {
                m_isChoosingSquare = true;
                updateLCFromPos(pos);
                handled = true;
            }
        }

        if (handled) {
            emit mousePressed();
        }
    }
}

void OKLCHColorWheel::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) {
        QPointF pos = event->position();
        if (m_isChoosingRing) {
            updateHFromPos(pos);
        } else if (m_isChoosingSquare) {
            updateLCFromPos(pos);
        }
    }
}

void OKLCHColorWheel::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    m_isChoosingRing = false;
    m_isChoosingSquare = false;
}

void OKLCHColorWheel::updateHFromPos(const QPointF &pos)
{
    float dx = pos.x() - m_center.x();
    float dy = pos.y() - m_center.y();
    double angleRad = std::atan2(-dy, dx);
    double angleDeg = qRadiansToDegrees(angleRad);
    
    // Round to 1 decimal place like Python
    angleDeg = std::round(angleDeg * 10.0) / 10.0;
    
    if (angleDeg < 0) {
        angleDeg += 360.0;
    }
    
    float newH = std::fmod((90.0 - angleDeg), 360.0);
    if (newH < 0) newH += 360.0;
    
    setH(newH);
}

void OKLCHColorWheel::updateLCFromPos(const QPointF &pos)
{
    float halfSide = m_side / 2.0f;
    float left = m_center.x() - halfSide;
    float bottom = m_center.y() + halfSide; // Y increases downwards
    
    float l = (pos.x() - left) / m_side;
    float c = (bottom - pos.y()) / m_side * 0.37f;
    
    setL(qBound(0.0f, l, 1.0f));
    setC(qBound(0.0f, c, 0.37f));
}

void OKLCHColorWheel::generateOuterWheel()
{
    int size = static_cast<int>(m_outerRadius * 2);
    if (size <= 0) return;
    
    QImage image(size, size, QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);

    QPointF center(size / 2.0, size / 2.0);

    // Clip ring
    QPainterPath path;
    path.addEllipse(center, m_outerRadius, m_outerRadius);
    path.addEllipse(center, m_midRadius - 1, m_midRadius - 1); // Python: mid_radius-1
    painter.setClipPath(path);
    
    // Conical Gradient
    QConicalGradient gradient(center, 90);
    // Python: hues 0 to 360. L=0.76, C=0.121
    for (int i = 0; i < 360; ++i) {
        // Python code reverses the colors: colors_255 = colors_255[::-1]
        // Which means hue 0 is at 0, hue 360 is at 1, but order is reversed.
        // Actually, Python `np.linspace(0, 360, ...)` -> [0, 1, ..., 359]
        // `oklch_to_srgb` -> RGBs.
        // `[::-1]` reverses it -> [RGB(359), ..., RGB(0)]
        // So at stop 0.0 (angle 0), we have hue 359?
        // Wait, ConicalGradient starts at 'angle' (90 deg).
        // Let's stick to simple 0-360 mapping. If it's reversed, we can swap direction.
        // Python code: hues=0..360. Colors computed. Reversed.
        // So i=0 (0 deg in gradient) gets color from last hue.
        // Let's just try normal direction first.
        gradient.setColorAt(i / 360.0, oklchToRgb(0.76f, 0.121f, 360.0f - i, false));
    }
    
    painter.setBrush(gradient);
    painter.setPen(Qt::NoPen);
    painter.drawRect(0, 0, size, size); // Draw full rect, clipped by path
    
    m_outerWheelPixmap = QPixmap::fromImage(image);
}

void OKLCHColorWheel::generateInnerWheel()
{
    int size = static_cast<int>(m_midRadius * 2);
    if (size <= 0) return;

    QImage image(size, size, QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);

    QPointF center(size / 2.0, size / 2.0);

    // Clip ring
    QPainterPath path;
    path.addEllipse(center, m_midRadius, m_midRadius);
    path.addEllipse(center, m_innerRadius, m_innerRadius);
    painter.setClipPath(path);

    // Gradient
    QConicalGradient gradient(center, 90);
    for (int i = 0; i < 360; ++i) {
        // Uses current L and C
        gradient.setColorAt(i / 360.0, oklchToRgb(m_L, m_C, 360.0f - i, false));
    }

    painter.setBrush(gradient);
    painter.setPen(Qt::NoPen);
    painter.drawRect(0, 0, size, size);

    m_innerWheelPixmap = QPixmap::fromImage(image);
}

void OKLCHColorWheel::generateSquare()
{
    if (m_side <= 0) return;
    
    QImage image(m_side, m_side, QImage::Format_RGB32);
    
    // Pixel loop

    for (int y = 0; y < m_side; ++y) {
        float c = (m_side - y - 1) / (float)m_side * 0.37f; // Flip Y for C
        // Actually Python: (bottom - pos.y()) / side * 0.37
        // In image coord, y=0 is top.
        // If y=0 (top), distance from bottom (side) is 'side'. So C = 0.37.
        // If y=side (bottom), C = 0.
        // My code: (side - 0 - 1)/side = ~1 * 0.37. Correct.
        
        QRgb *line = reinterpret_cast<QRgb*>(image.scanLine(y));//��õ�ǰ�е�����ָ��x������
        for (int x = 0; x < m_side; ++x) {
            float l = x / (float)m_side;
            
            QColor col = oklchToRgb(l, c, m_H, false);
            line[x] = col.rgb();
        }
    }
    
    m_squarePixmap = QPixmap::fromImage(image);
}

bool OKLCHColorWheel::srgbHexToOklch(const QString &hex, float &L, float &C, float &H)
{
    QString cleaned = hex.trimmed();
    if (cleaned.startsWith("#")) {
        cleaned.remove(0, 1);
    }
    if (cleaned.size() != 6) {
        return false;
    }
    bool ok = false;
    int r = cleaned.mid(0, 2).toInt(&ok, 16);
    if (!ok) return false;
    int g = cleaned.mid(2, 2).toInt(&ok, 16);
    if (!ok) return false;
    int b = cleaned.mid(4, 2).toInt(&ok, 16);
    if (!ok) return false;

    float sr = r / 255.0f;
    float sg = g / 255.0f;
    float sb = b / 255.0f;

    auto srgbToLinear = [](float c) -> float {
        if (c <= 0.04045f) {
            return c / 12.92f;
        }
        return std::pow((c + 0.055f) / 1.055f, 2.4f);
    };

    float lr = srgbToLinear(sr);
    float lg = srgbToLinear(sg);
    float lb = srgbToLinear(sb);

    float l = 0.4122214708f * lr + 0.5363325363f * lg + 0.0514459929f * lb;
    float m = 0.2119034982f * lr + 0.6806995451f * lg + 0.1073969566f * lb;
    float s = 0.0883024619f * lr + 0.2817188376f * lg + 0.6299787005f * lb;

    float l_ = std::cbrt(l);
    float m_ = std::cbrt(m);
    float s_ = std::cbrt(s);

    float a = 1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_;
    float b2 = 0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_;

    L = 0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_;
    C = std::sqrt(a * a + b2 * b2);
    H = qRadiansToDegrees(std::atan2(b2, a));
    if (H < 0.0f) {
        H += 360.0f;
    }
    return true;
}

QColor OKLCHColorWheel::oklchToRgb(float l, float c, float h, bool autopair)
{
    float h_rad = qDegreesToRadians(h);

    float a = c * std::cos(h_rad);
    float b = c * std::sin(h_rad);

    // 1. OKLCH -> LMS (pre-cube)
    // M1
    float l_ = l + 0.3963377774f * a + 0.2158037573f * b;
    float m_ = l - 0.1055613458f * a - 0.0638541728f * b;
    float s_ = l - 0.0894841775f * a - 1.2914855480f * b;

    // Cube
    float l_3 = l_ * l_ * l_;
    float m_3 = m_ * m_ * m_;
    float s_3 = s_ * s_ * s_;

    // 2. LMS -> Linear sRGB
    // M2
    float r_lin = 4.0767416621f * l_3 - 3.3077115913f * m_3 + 0.2309699292f * s_3;
    float g_lin = -1.2684380046f * l_3 + 2.6097574011f * m_3 - 0.3413193965f * s_3;
    float b_lin = -0.0041960863f * l_3 - 0.7034186147f * m_3 + 1.7076147010f * s_3;

    // 3. Gamma correction
    auto gamma = [](float x) -> float {
        if (x <= 0.0031308f) {
            return x * 12.92f;
        }
        else {
            return 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
        }
        };

    float r = gamma(r_lin);
    float g = gamma(g_lin);
    float b_val = gamma(b_lin);

    if (autopair) {
        r = qBound(0.0f, r, 1.0f);
        g = qBound(0.0f, g, 1.0f);
        b_val = qBound(0.0f, b_val, 1.0f);
    }
    else {
        // Python logic: if out of bounds ( > 1.0 or < 0.0 ), return white (1.0)
        if (r > 1.0f || g > 1.0f || b_val > 1.0f || r < 0.0f || g < 0.0f || b_val < 0.0f) {
            return QColor(255, 255, 255);
        }
        r = qBound(0.0f, r, 1.0f);
        g = qBound(0.0f, g, 1.0f);
        b_val = qBound(0.0f, b_val, 1.0f);
    }

    return QColor(
        static_cast<int>(r * 255 + 0.5f),
        static_cast<int>(g * 255 + 0.5f),
        static_cast<int>(b_val * 255 + 0.5f)
    );
}



ColorWheelSimple::ColorWheelSimple(QWidget* parent) : QWidget(parent)
{
    setupUi();
}

void ColorWheelSimple::setupUi()
{
    setStyleSheet("border-radius: 6px; font-size: 16px;");

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_wheel = new OKLCHColorWheel(0.70f, 0.12f, 100.0f, this);
    layout->addWidget(m_wheel, 1);

    m_bottomBar = new QWidget(this);
    m_bottomLayout = new QHBoxLayout(m_bottomBar);
    m_bottomLayout->setSpacing(3);
    m_bottomLayout->setContentsMargins(6, 6, 6, 6);

    m_historyColors = { "#FFFFFF", "#FFFFFF", "#FFFFFF", "#FFFFFF", "#FFFFFF" };
    for (int i = 0; i < m_historyColors.size(); ++i) {
        ColorLabel* label = new ColorLabel(m_historyColors[i], false, m_bottomBar);
        label->setFixedSize(30, 30);
        m_historyLabels.append(label);
        connect(label, &ColorLabel::colorClicked, this, [this, label](const QString&) {
            swapColor(label);
            });
        m_bottomLayout->addWidget(label);
    }

    m_syncLabel = new ColorLabel("#FFFFFF", false, m_bottomBar);
    m_syncLabel->setFixedHeight(30);
    m_bottomLayout->addWidget(m_syncLabel);

    m_hexLineEdit = new HexColorLineEdit(m_bottomBar);
    m_hexLineEdit->setFixedSize(80, 30);
    m_bottomLayout->addWidget(m_hexLineEdit);

    layout->addWidget(m_bottomBar);

    connect(m_hexLineEdit, &HexColorLineEdit::validHexColor, this, &ColorWheelSimple::setHexColor);
    connect(m_wheel, &OKLCHColorWheel::LChanged, this, &ColorWheelSimple::onWheelChanged);
    connect(m_wheel, &OKLCHColorWheel::CChanged, this, &ColorWheelSimple::onWheelChanged);
    connect(m_wheel, &OKLCHColorWheel::HChanged, this, &ColorWheelSimple::onWheelChanged);
    connect(m_wheel, &OKLCHColorWheel::mousePressed, this, &ColorWheelSimple::replaceHistory);
    connect(m_wheel, &OKLCHColorWheel::mousePressed, m_hexLineEdit, &QWidget::clearFocus);
    connect(m_wheel, &OKLCHColorWheel::colorChanged, this, &ColorWheelSimple::colorChanged);

    onWheelChanged();
}

void ColorWheelSimple::replaceHistory()
{
    if (m_historyLabels.isEmpty()) {
        return;
    }
    QString current = getHexColor();
    m_historyColors.append(current);
    while (m_historyColors.size() > m_historyLabels.size()) {
        m_historyColors.removeFirst();
    }
    for (int i = 0; i < m_historyLabels.size(); ++i) {
        m_historyLabels[i]->setColor(m_historyColors[i]);
    }
}

void ColorWheelSimple::swapColor(ColorLabel* label)
{
    if (!label) {
        return;
    }
    QString current = getHexColor();
    QString clicked = label->getColor();
    label->setColor(current);
    setHexColor(clicked);
    int idx = m_historyLabels.indexOf(label);
    if (idx >= 0 && idx < m_historyColors.size()) {
        m_historyColors[idx] = current;
    }
}

void ColorWheelSimple::onWheelChanged()
{
    if (!m_syncLabel || !m_hexLineEdit) {
        return;
    }
    QString hex = getHexColor();
    m_syncLabel->setColor(hex);
    QSignalBlocker blocker(m_hexLineEdit);
    m_hexLineEdit->setText(hex);
}

void ColorWheelSimple::setL(float l)
{
    if (m_wheel) {
        m_wheel->setL(l);
    }
}

void ColorWheelSimple::setC(float c)
{
    if (m_wheel) {
        m_wheel->setC(c);
    }
}

void ColorWheelSimple::setH(float h)
{
    if (m_wheel) {
        m_wheel->setH(h);
    }
}

float ColorWheelSimple::getL() const
{
    return m_wheel ? m_wheel->getL() : 0.0f;
}

float ColorWheelSimple::getC() const
{
    return m_wheel ? m_wheel->getC() : 0.0f;
}

float ColorWheelSimple::getH() const
{
    return m_wheel ? m_wheel->getH() : 0.0f;
}

QColor ColorWheelSimple::getCurrentColor() const
{
    return m_wheel ? m_wheel->getCurrentColor() : QColor();
}

QString ColorWheelSimple::getHexColor() const
{
    return m_wheel ? m_wheel->getHexColor() : QString();
}

void ColorWheelSimple::setHexColor(const QString& hex)
{
    if (!m_wheel) {
        return;
    }
    float l = 0.0f;
    float c = 0.0f;
    float h = 0.0f;
    if (OKLCHColorWheel::srgbHexToOklch(hex, l, c, h)) {
        m_wheel->setL(l);
        m_wheel->setC(c);
        m_wheel->setH(h);
    }
}

void ColorWheelSimple::setInitialColor(const QString& hex)
{
    setHexColor(hex);
}

} // namespace darkeye


