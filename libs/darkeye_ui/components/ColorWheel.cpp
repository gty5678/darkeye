#include "darkeye_ui/components/ColorWheel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QtMath>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QClipboard>
#include <QApplication>
#include <QColorSpace>
#include <QColorTransform>
#include <QLabel>
#include <QRgbaFloat>
#include <QSurfaceFormat>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <QConicalGradient>



namespace darkeye {

namespace {

constexpr float maximumP3Chroma = 0.37f;
constexpr float gamutTolerance = 1e-5f;

QRgbaFloat32 oklchToLinearSrgb(float l, float c, float h)
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
    return {
        4.0767416621f * l_3 - 3.3077115913f * m_3 + 0.2309699292f * s_3,
        -1.2684380046f * l_3 + 2.6097574011f * m_3 - 0.3413193965f * s_3,
        -0.0041960863f * l_3 - 0.7034186147f * m_3 + 1.7076147010f * s_3,
        1.0f,
    };
}

QRgbaFloat32 linearSrgbToLinearP3(const QRgbaFloat32 &linearSrgb)
{
    static const auto transform = QColorSpace(QColorSpace::SRgbLinear)
        .transformationToColorSpace(QColorSpace(QColorSpace::Primaries::DciP3D65,
                                                QColorSpace::TransferFunction::Linear));
    // Keep signed components when converting colors outside sRGB.
    return transform.map(linearSrgb);
}

QRgbaFloat32 oklchToLinearRgb(float l, float c, float h, QColorSpace::NamedColorSpace gamut)
{
    const auto srgb = oklchToLinearSrgb(l, c, h);
    return gamut == QColorSpace::DisplayP3 ? linearSrgbToLinearP3(srgb) : srgb;
}

bool isInGamut(const QRgbaFloat32 &linearRgb)
{
    // Ignore floating-point error at the primaries and the endpoints of the gray axis.
    const auto inRange = [](float channel) {
        return channel >= -gamutTolerance && channel <= 1.0f + gamutTolerance;
    };
    return inRange(linearRgb.r) && inRange(linearRgb.g) && inRange(linearRgb.b);
}

float gamutMargin(const QRgbaFloat32 &rgb)
{
    return std::min({rgb.r, rgb.g, rgb.b, 1.0f - rgb.r, 1.0f - rgb.g, 1.0f - rgb.b})
        + gamutTolerance;
}

void addGamutContour(QPainterPath &path, const QPointF &topLeft, const std::array<float, 4> &margins)
{
    constexpr std::array<QPointF, 4> corners{QPointF(0, 0), QPointF(1, 0),
                                            QPointF(1, 1), QPointF(0, 1)};
    std::array<QPointF, 4> crossings;
    int count = 0;
    for (int edge = 0; edge < 4; ++edge) {
        const int next = (edge + 1) % 4;
        if ((margins[edge] >= 0) == (margins[next] >= 0)) continue;
        const float fraction = margins[edge] / (margins[edge] - margins[next]);
        crossings[count++] = topLeft + corners[edge] + (corners[next] - corners[edge]) * fraction;
    }
    if (count == 2) {
        path.moveTo(crossings[0]);
        path.lineTo(crossings[1]);
    } else if (count == 4) {
        // Resolve the alternating-corners case using the interpolated center.
        const float center = margins[0] + margins[1] + margins[2] + margins[3];
        const int offset = (center >= 0) == (margins[0] >= 0) ? 0 : 1;
        path.moveTo(crossings[offset]);
        path.lineTo(crossings[offset + 1]);
        path.moveTo(crossings[offset + 2]);
        path.lineTo(crossings[(offset + 3) % 4]);
    }
}

QRgb encodeRgb(const QRgbaFloat32 &linearRgb)
{
    const auto encodeChannel = [](float linear) {
        const float clamped = qBound(0.0f, linear, 1.0f);
        const float encoded = clamped <= 0.0031308f ? 12.92f * clamped
            : 1.055f * std::pow(clamped, 1.0f / 2.4f) - 0.055f;
        return qRound(encoded * 255.0f);
    };
    return qRgb(encodeChannel(linearRgb.r), encodeChannel(linearRgb.g), encodeChannel(linearRgb.b));
}

} // namespace

QString OklchColor::toString() const
{
    constexpr int precision = std::numeric_limits<float>::max_digits10;
    return QStringLiteral("%1 %2 %3").arg(QString::number(lightness, 'g', precision),
                                          QString::number(chroma, 'g', precision),
                                          QString::number(hue, 'g', precision));
}

QString OklchColor::hex(QColorSpace::NamedColorSpace gamut) const
{
    return OKLCHColorWheel::oklchToRgb(lightness, chroma, hue, true, gamut).name().toUpper();
}

std::optional<OklchColor> OklchColor::fromString(const QString &text)
{
    const auto values = text.split(u' ', Qt::SkipEmptyParts);
    if (values.size() != 3) return std::nullopt;
    bool lightnessOk, chromaOk, hueOk;
    const OklchColor color{values[0].toFloat(&lightnessOk), values[1].toFloat(&chromaOk),
                           values[2].toFloat(&hueOk)};
    if (lightnessOk && chromaOk && hueOk && color.lightness >= 0 && color.lightness <= 1
        && color.chroma >= 0 && color.chroma <= 1 && color.hue >= 0 && color.hue <= 360) {
        return color;
    }
    return std::nullopt;
}

std::optional<OklchColor> OklchColor::fromHex(const QString &hex, QColorSpace::NamedColorSpace gamut)
{
    OklchColor color{};
    if (OKLCHColorWheel::rgbHexToOklch(hex, color.lightness, color.chroma, color.hue, gamut))
        return color;
    return std::nullopt;
}

HexColorLineEdit::HexColorLineEdit(QWidget *parent)
    : QLineEdit(parent)
{
    setMaxLength(7);
    const QRegularExpression pattern(QStringLiteral("#?[0-9a-fA-F]{0,6}"));
    setValidator(new QRegularExpressionValidator(pattern, this));
    setStyleSheet("border-radius: 6px; font-size: 16px; font-family: 'Consolas';");
    connect(this, &QLineEdit::textEdited, this, &HexColorLineEdit::onTextEdited);
}

void HexColorLineEdit::onTextEdited(const QString &text)
{
    const QString digits = text.startsWith(u'#') ? text.mid(1) : text;
    if (digits.size() == 6 && hasAcceptableInput())
        emit validHexColor(QStringLiteral("#") + digits.toUpper());
}

ColorLabel::ColorLabel(const QString &hexColor, bool showText, QWidget *parent,
                       QColorSpace::NamedColorSpace gamut)
    : QPushButton(parent), m_color(hexColor), m_showText(showText), m_gamut(gamut)
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
    const QColorSpace output = QSurfaceFormat::defaultFormat().colorSpace();
    const auto transform = QColorSpace(m_gamut).transformationToColorSpace(
        output.isValid() ? output : QColorSpace(QColorSpace::SRgb));
    const QColor preview = QColor::fromRgb(transform.map(QColor(m_color).rgb()));
    QColor textColor = textColorForBackground(preview);
    if (m_showText) {
        setText(m_color);
    } else {
        setText(QString());
    }
    setStyleSheet(
        QString("background-color: %1; border-radius: 6px; color: %2; font-size: 14px; padding: 6px;")
            .arg(preview.name(), textColor.name())
    );
}


OKLCHColorWheel::OKLCHColorWheel(float l, float c, float h, QWidget *parent, RgbGamut gamut)
    : QWidget(parent), m_gamut(gamut),
      m_outputGamut(QSurfaceFormat::defaultFormat().colorSpace() == QColorSpace(QColorSpace::DisplayP3)
                        ? QColorSpace::DisplayP3 : QColorSpace::SRgb)
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

void OKLCHColorWheel::setOklch(const OklchColor &color)
{
    const bool lightnessChanged = m_L != color.lightness;
    const bool chromaChanged = m_C != color.chroma;
    const bool hueChanged = m_H != color.hue;
    if (!lightnessChanged && !chromaChanged && !hueChanged) return;

    m_L = color.lightness;
    m_C = color.chroma;
    m_H = color.hue;
    if (lightnessChanged || chromaChanged) generateInnerWheel();
    if (hueChanged) generateSquare();
    update();
    if (lightnessChanged) emit LChanged(m_L);
    if (chromaChanged) emit CChanged(m_C);
    if (hueChanged) emit HChanged(m_H);
    emit colorChanged(getCurrentColor());
}

QColor OKLCHColorWheel::getCurrentColor() const
{
    return oklchToRgb(m_L, m_C, m_H, true, m_gamut);
}

QString OKLCHColorWheel::getHexColor(RgbGamut gamut) const
{
    return oklchToRgb(m_L, m_C, m_H, true, gamut).name().toUpper();
}

QColor OKLCHColorWheel::previewColor(float l, float c, float h) const
{
    const auto srgb = oklchToLinearSrgb(l, c, h);
    const auto p3 = linearSrgbToLinearP3(srgb);
    if (!isInGamut(p3)) return Qt::white;
    return QColor::fromRgb(encodeRgb(m_outputGamut == QColorSpace::DisplayP3 ? p3 : srgb));
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

QRectF OKLCHColorWheel::squareRect() const
{
    const qreal halfSide = m_side / 2.0;
    return {m_center.x() - halfSide, m_center.y() - halfSide, qreal(m_side), qreal(m_side)};
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
        painter.drawPixmap(squareRect().topLeft(), m_squarePixmap);
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

    const auto square = squareRect();
    const QPointF truePoint(square.left() + m_L * m_side,
                            square.bottom() - (m_C / maximumP3Chroma) * m_side);

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
        else if (squareRect().contains(pos)) {
            m_isChoosingSquare = true;
            updateLCFromPos(pos);
            handled = true;
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
    const auto square = squareRect();
    const float l = qBound(0.0, (pos.x() - square.left()) / m_side, 1.0);
    const float c = qBound(0.0, (square.bottom() - pos.y()) / m_side, 1.0) * maximumP3Chroma;
    setOklch({l, c, m_H});
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
        gradient.setColorAt(i / 360.0, previewColor(0.76f, 0.121f, 360.0f - i));
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
        gradient.setColorAt(i / 360.0, previewColor(m_L, m_C, 360.0f - i));
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
    QPainterPath boundary;
    QVector<float> previousMargins(m_side);
    QVector<float> currentMargins(m_side);
    for (int y = 0; y < m_side; ++y) {
        const float chroma = (1.0f - (y + 0.5f) / m_side) * maximumP3Chroma;
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < m_side; ++x) {
            const float lightness = (x + 0.5f) / m_side;
            const auto srgb = oklchToLinearSrgb(lightness, chroma, m_H);
            const auto p3 = linearSrgbToLinearP3(srgb);
            line[x] = isInGamut(p3) ? encodeRgb(m_outputGamut == QColorSpace::DisplayP3 ? p3 : srgb)
                                    : qRgb(255, 255, 255);
            currentMargins[x] = gamutMargin(srgb);
            if (x > 0 && y > 0) {
                addGamutContour(boundary, QPointF(x - 0.5, y - 0.5),
                                {previousMargins[x - 1], previousMargins[x],
                                 currentMargins[x], currentMargins[x - 1]});
            }
        }
        previousMargins.swap(currentMargins);
    }

    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(0, 0, 0, 128), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(boundary);
        painter.setPen(QPen(Qt::white, 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(boundary);
    }
    m_squarePixmap = QPixmap::fromImage(image);
}

bool OKLCHColorWheel::srgbHexToOklch(const QString &hex, float &L, float &C, float &H)
{
    return rgbHexToOklch(hex, L, C, H, RgbGamut::SRgb);
}

bool OKLCHColorWheel::rgbHexToOklch(const QString &hex, float &L, float &C, float &H,
                                   RgbGamut gamut)
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

    QRgbaFloat32 linear{srgbToLinear(sr), srgbToLinear(sg), srgbToLinear(sb), 1.0f};
    if (gamut == RgbGamut::DisplayP3) {
        static const auto toLinearSrgb = QColorSpace(QColorSpace::Primaries::DciP3D65,
                                                    QColorSpace::TransferFunction::Linear)
            .transformationToColorSpace(QColorSpace(QColorSpace::SRgbLinear));
        linear = toLinearSrgb.map(linear);
    }

    float l = 0.4122214708f * linear.r + 0.5363325363f * linear.g + 0.0514459929f * linear.b;
    float m = 0.2119034982f * linear.r + 0.6806995451f * linear.g + 0.1073969566f * linear.b;
    float s = 0.0883024619f * linear.r + 0.2817188376f * linear.g + 0.6299787005f * linear.b;

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

QColor OKLCHColorWheel::oklchToRgb(float l, float c, float h, bool autopair, RgbGamut gamut)
{
    const auto linearRgb = oklchToLinearRgb(l, c, h, gamut);
    if (!autopair && !isInGamut(linearRgb)) return Qt::white;

    return QColor::fromRgb(encodeRgb(linearRgb));
}

ColorWheelSimple::ColorWheelSimple(QWidget *parent, QColorSpace::NamedColorSpace gamut)
    : QWidget(parent), m_gamut(gamut)
{
    setupUi();
}

void ColorWheelSimple::setupUi()
{
    setStyleSheet("border-radius: 6px; font-size: 16px;");

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_wheel = new OKLCHColorWheel(0.70f, 0.12f, 100.0f, this, m_gamut);
    layout->addWidget(m_wheel, 1);

    m_bottomBar = new QWidget(this);
    m_bottomLayout = new QHBoxLayout(m_bottomBar);
    m_bottomLayout->setSpacing(3);
    m_bottomLayout->setContentsMargins(6, 6, 6, 6);

    m_historyColors = { "#FFFFFF", "#FFFFFF", "#FFFFFF", "#FFFFFF", "#FFFFFF" };
    for (int i = 0; i < m_historyColors.size(); ++i) {
        ColorLabel* label = new ColorLabel(m_historyColors[i], false, m_bottomBar, m_gamut);
        label->setFixedSize(30, 30);
        m_historyLabels.append(label);
        connect(label, &ColorLabel::colorClicked, this, [this, label](const QString&) {
            swapColor(label);
            });
        m_bottomLayout->addWidget(label);
    }

    m_syncLabel = new ColorLabel("#FFFFFF", false, m_bottomBar, m_gamut);
    m_syncLabel->setFixedHeight(30);
    m_bottomLayout->addWidget(m_syncLabel);
    layout->addWidget(m_bottomBar);

    auto *values = new QHBoxLayout;
    values->setContentsMargins(6, 0, 6, 6);
    values->setSpacing(6);
    const auto addValue = [this, values](const QString &name, QLineEdit *field) {
        auto *label = new QLabel(name, this);
        label->setBuddy(field);
        field->setAccessibleName(name);
        field->setFixedSize(80, 30);
        field->setAlignment(Qt::AlignCenter);
        values->addWidget(label);
        values->addWidget(field);
    };
    m_hexLineEdit = new HexColorLineEdit(this);
    addValue(QStringLiteral("sRGB"), m_hexLineEdit);
    values->addStretch();
    m_displayP3Hex = new HexColorLineEdit(this);
    addValue(QStringLiteral("Display P3"), m_displayP3Hex);

    layout->addLayout(values);

    m_gamutHint = new QLabel(QStringLiteral("P3 专属 · 超出 sRGB 色域"), this);
    m_gamutHint->setObjectName(QStringLiteral("colorWheelGamutHint"));
    m_gamutHint->setAlignment(Qt::AlignCenter);
    m_gamutHint->setStyleSheet(QStringLiteral("font-size: 13px; padding-bottom: 6px;"));
    auto hintPolicy = m_gamutHint->sizePolicy();
    hintPolicy.setRetainSizeWhenHidden(true);
    m_gamutHint->setSizePolicy(hintPolicy);
    layout->addWidget(m_gamutHint);

    connect(m_hexLineEdit, &HexColorLineEdit::validHexColor, this, [this](const QString &hex) {
        setHexColorInGamut(hex, QColorSpace::SRgb);
    });
    connect(m_displayP3Hex, &HexColorLineEdit::validHexColor, this, [this](const QString &hex) {
        setHexColorInGamut(hex, QColorSpace::DisplayP3);
    });
    connect(m_wheel, &OKLCHColorWheel::mousePressed, this, &ColorWheelSimple::replaceHistory);
    connect(m_wheel, &OKLCHColorWheel::mousePressed, m_hexLineEdit, &QWidget::clearFocus);
    connect(m_wheel, &OKLCHColorWheel::mousePressed, m_displayP3Hex, &QWidget::clearFocus);
    connect(m_wheel, &OKLCHColorWheel::colorChanged, this, [this] {
        onWheelChanged();
        emit colorChanged(getCurrentColor());
    });

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
    m_hexLineEdit->setText(m_wheel->getHexColor());
    m_displayP3Hex->setText(m_wheel->getHexColor(OKLCHColorWheel::RgbGamut::DisplayP3));
    const auto [l, c, h] = oklch();
    m_gamutHint->setVisible(!isInGamut(oklchToLinearRgb(l, c, h, QColorSpace::SRgb))
                            && isInGamut(oklchToLinearRgb(l, c, h, QColorSpace::DisplayP3)));
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
    return QColor(getHexColor());
}

QString ColorWheelSimple::getHexColor() const
{
    return m_wheel ? m_wheel->getHexColor(m_gamut) : QString();
}

void ColorWheelSimple::setHexColor(const QString& hex)
{
    setHexColorInGamut(hex, m_gamut);
}

void ColorWheelSimple::setHexColorInGamut(const QString &hex, QColorSpace::NamedColorSpace gamut)
{
    if (const auto color = OklchColor::fromHex(hex, gamut)) setOklch(*color);
}

void ColorWheelSimple::setInitialColor(const QString& hex)
{
    setHexColor(hex);
}

void ColorWheelSimple::setOklch(const OklchColor &color)
{
    m_wheel->setOklch(color);
}

OklchColor ColorWheelSimple::oklch() const
{
    return {getL(), getC(), getH()};
}

} // namespace darkeye


