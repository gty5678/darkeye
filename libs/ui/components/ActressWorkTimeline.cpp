#include "ui/components/ActressWorkTimeline.h"

#include "darkeye_ui/components/DesignLabel.h"
#include "ui/components/WorkCard.h"

#include <QApplication>
#include <QDate>
#include <QHelpEvent>
#include <QFrame>
#include <QMap>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSet>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <functional>

namespace darkeye {
namespace {
constexpr int axisY = 38;
constexpr int markerSize = 16;
constexpr int laneStride = 20;
constexpr int trackPad = 14;
constexpr int unknownWidth = 120;
constexpr double minPpd = 0.001;
constexpr double maxPpd = 300.0;
const QDate defaultStart(2016, 1, 1);
const QDate defaultEnd(2026, 12, 31);

QColor tagColor(int tagId, const QPalette &palette)
{
    switch (tagId) {
    case 1: return QColor(QStringLiteral("#80B0F8"));
    case 2: return QColor(QStringLiteral("#F88441"));
    case 3: return QColor(QStringLiteral("#FDEB48"));
    default: return palette.color(QPalette::Highlight);
    }
}

class TimelineHoverPopup final : public QWidget
{
public:
    TimelineHoverPopup()
        : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setMouseTracking(true);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
    }

    std::function<void()> entered;
    std::function<void()> left;

protected:
    void enterEvent(QEnterEvent *event) override
    {
        if (entered) entered();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        if (left) left();
        QWidget::leaveEvent(event);
    }
};
}

class TimelineCanvas final : public QWidget
{
public:
    explicit TimelineCanvas(QScrollArea *scroll, QString coverDirectory,
                            QWidget *parent = nullptr)
        : QWidget(parent), m_scroll(scroll), m_coverDirectory(std::move(coverDirectory))
    {
        setObjectName(QStringLiteral("ActressWorkTimelineCanvas"));
        setMouseTracking(true);
        setCursor(Qt::ArrowCursor);
        m_popup = new TimelineHoverPopup;
        m_popup->entered = [this] { m_hideTimer->stop(); };
        m_popup->left = [this] { schedulePopupHide(); };
        m_hideTimer = new QTimer(this);
        m_hideTimer->setSingleShot(true);
        m_hideTimer->setInterval(280);
        connect(m_hideTimer, &QTimer::timeout, this, [this] { hidePopup(); });
        connect(m_scroll->horizontalScrollBar(), &QScrollBar::valueChanged,
                this, [this] { hidePopup(); });
        connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged,
                this, [this] { hidePopup(); });
    }

    ~TimelineCanvas() override { delete m_popup; }

    void setWorks(QList<PersonWorkSummary> works)
    {
        m_works = std::move(works);
        rebuild();
    }
    const QList<PersonWorkSummary> &works() const { return m_works; }
    int markerCount() const { return m_markers.size(); }
    double ppd() const { return m_ppd; }
    QDate dateMinimum() const { return m_dateMin; }
    QPoint markerCenter(qint64 workId) const
    {
        const auto marker = std::find_if(m_markers.cbegin(), m_markers.cend(),
            [workId](const Marker &item) { return item.work.id == workId; });
        return marker == m_markers.cend() ? QPoint{} : marker->rect.center();
    }
    bool hoverPreviewVisible() const { return m_popup->isVisible(); }
    void setPpd(double value)
    {
        m_ppd = qBound(minPpd, value, maxPpd);
        rebuild();
    }
    std::function<void(qint64)> activate;

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QPainter painter(this);
        const auto colors = palette();
        const QRect exposed = event->rect();
        painter.fillRect(exposed, colors.color(QPalette::Base));
        painter.fillRect(exposed.intersected(QRect(0, 0, width(), axisY)),
                         colors.color(QPalette::AlternateBase));
        painter.setRenderHint(QPainter::Antialiasing);

        if (m_dateMin.isValid() && m_numDays > 0)
            paintRuler(painter, exposed, colors);

        painter.setPen(colors.color(QPalette::Mid));
        painter.drawLine(exposed.left(), axisY, exposed.right(), axisY);
        if (m_unknownX >= 0) {
            painter.setPen(colors.color(QPalette::PlaceholderText));
            const QRect label(m_unknownX - unknownWidth / 2, 0, unknownWidth, axisY);
            painter.drawText(label, Qt::AlignCenter, QStringLiteral("日期未知"));
        }

        auto guideColor = colors.color(QPalette::Text);
        guideColor.setAlphaF(0.24);
        painter.setPen(guideColor);
        for (const QPoint &end : m_stems) {
            if (end.x() >= exposed.left() && end.x() <= exposed.right())
                painter.drawLine(end.x(), axisY, end.x(), end.y());
        }

        for (const Marker &marker : std::as_const(m_markers)) {
            if (!exposed.intersects(marker.rect)) continue;
            const QPointF center = marker.rect.center();
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors.color(QPalette::Base));
            painter.drawEllipse(center, 6.5, 6.5);
            painter.setBrush(tagColor(marker.work.highlightTagId, colors));
            painter.drawEllipse(center, 4.5, 4.5);
        }
    }

    void wheelEvent(QWheelEvent *event) override
    {
        hidePopup();
        if (event->modifiers().testFlag(Qt::ControlModifier)) {
            m_scroll->horizontalScrollBar()->setValue(
                m_scroll->horizontalScrollBar()->value() - event->angleDelta().y());
            event->accept();
            return;
        }
        if (event->modifiers().testFlag(Qt::ShiftModifier)) {
            m_scroll->verticalScrollBar()->setValue(
                m_scroll->verticalScrollBar()->value() - event->angleDelta().y());
            event->accept();
            return;
        }
        const int delta = event->angleDelta().y();
        if (delta == 0) {
            event->accept();
            return;
        }
        const double canvasX = event->position().x();
        const double viewportX = canvasX - m_scroll->horizontalScrollBar()->value();
        const double anchorDate = m_dateMin.toJulianDay() + (canvasX - trackPad) / m_ppd;
        const bool unknownAnchor = m_unknownX >= 0 && canvasX >= m_unknownX - markerSize / 2;
        const double unknownOffset = canvasX - m_unknownX;
        setPpd(m_ppd * qPow(1.2, delta / 120.0));
        const double newCanvasX = unknownAnchor ? m_unknownX + unknownOffset
            : trackPad + (anchorDate - m_dateMin.toJulianDay()) * m_ppd;
        m_scroll->horizontalScrollBar()->setValue(
            qRound(newCanvasX - viewportX));
        event->accept();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::MiddleButton) {
            m_middlePanning = true;
            m_lastGlobalPosition = event->globalPosition();
            setCursor(Qt::ClosedHandCursor);
            grabMouse();
            hidePopup();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_middlePanning) {
            const QPointF delta = event->globalPosition() - m_lastGlobalPosition;
            m_lastGlobalPosition = event->globalPosition();
            m_scroll->horizontalScrollBar()->setValue(
                m_scroll->horizontalScrollBar()->value() - qRound(delta.x()));
            m_scroll->verticalScrollBar()->setValue(
                m_scroll->verticalScrollBar()->value() - qRound(delta.y()));
            event->accept();
            return;
        }
        const Marker *marker = markerAt(event->position().toPoint());
        setCursor(marker ? Qt::PointingHandCursor : Qt::ArrowCursor);
        if (marker != nullptr) {
            m_hideTimer->stop();
            showPopup(*marker);
        } else {
            schedulePopupHide();
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::MiddleButton && m_middlePanning) {
            m_middlePanning = false;
            releaseMouse();
            setCursor(Qt::ArrowCursor);
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton) {
            if (const Marker *marker = markerAt(event->position().toPoint())) {
                if (activate) activate(marker->work.id);
                event->accept();
                return;
            }
        }
        QWidget::mouseReleaseEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        if (!m_middlePanning) schedulePopupHide();
        QWidget::leaveEvent(event);
    }

    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::ToolTip) {
            const auto *help = static_cast<QHelpEvent *>(event);
            if (const Marker *marker = markerAt(help->pos())) {
                m_hideTimer->stop();
                showPopup(*marker);
            } else {
                schedulePopupHide();
            }
            event->accept();
            return true;
        }
        return QWidget::event(event);
    }

private:
    struct Marker { QRect rect; PersonWorkSummary work; };

    void paintRuler(QPainter &painter, const QRect &exposed, const QPalette &colors) const
    {
        const auto firstDay = qBound(0, qFloor((exposed.left() - trackPad - 65) / m_ppd), m_numDays - 1);
        const auto lastDay = qMin(m_numDays - 1, qCeil((exposed.right() - trackPad + 65) / m_ppd));
        const QDate firstDate = m_dateMin.addDays(firstDay);
        const QDate lastDate = m_dateMin.addDays(lastDay);
        const auto minor = colors.color(QPalette::PlaceholderText);
        const auto major = colors.color(QPalette::Text);
        auto grid = major;
        grid.setAlphaF(0.09);
        const auto drawTick = [&](const QDate &date, const QString &label, bool emphasized) {
            const int x = qRound(trackPad + m_dateMin.daysTo(date) * m_ppd);
            if (exposed.bottom() > axisY) {
                painter.setPen(grid);
                painter.drawLine(x, axisY, x, exposed.bottom());
            }
            painter.setPen(emphasized ? major : minor);
            painter.drawLine(x, axisY - (emphasized ? 6 : 4), x, axisY);
            painter.drawText(QRect(x - 60, 0, 120, axisY - 7), Qt::AlignCenter, label);
        };

        const int dayLabelSpacing = painter.fontMetrics().horizontalAdvance(QStringLiteral("0000-00-00")) + 12;
        if (15 * m_ppd >= dayLabelSpacing) {
            int step = 1;
            for (const int days : {1, 2, 5, 10, 15}) {
                step = days;
                if (days * m_ppd >= dayLabelSpacing) break;
            }
            for (QDate month(firstDate.year(), firstDate.month(), 1); month <= lastDate;
                 month = month.addMonths(1)) {
                for (int day = 1; day <= month.daysInMonth(); day += step) {
                    const QDate date = month.addDays(day - 1);
                    if (date < firstDate || date > lastDate) continue;
                    // Skip the final subdivision when it sits too close to the next month.
                    if (day > 1 && (month.daysInMonth() + 1 - day) * m_ppd < dayLabelSpacing) continue;
                    drawTick(date, date.toString(QStringLiteral("yyyy-MM-dd")), day == 1);
                }
            }
            return;
        }

        const int monthLabelSpacing = painter.fontMetrics().horizontalAdvance(QStringLiteral("0000-00")) + 16;
        int step = 1;
        for (const int months : {1, 2, 3, 6, 12, 24, 60, 120}) {
            step = months;
            if (months * 28.0 * m_ppd >= monthLabelSpacing) break;
        }
        if (step == 120)
            step *= qMax(1, qCeil(monthLabelSpacing / (step * 28.0 * m_ppd)));
        const QDate epoch(2000, 1, 1);
        const int monthIndex = (firstDate.year() - epoch.year()) * 12 + firstDate.month() - 1;
        const int aligned = monthIndex - ((monthIndex % step) + step) % step;
        for (QDate date = epoch.addMonths(aligned); date <= lastDate; date = date.addMonths(step)) {
            if (date < firstDate) continue;
            const bool january = date.month() == 1;
            const QString format = step >= 12 ? QStringLiteral("yyyy") : QStringLiteral("yyyy-MM");
            drawTick(date, date.toString(format), january);
        }
    }

    const Marker *markerAt(const QPoint &position) const
    {
        const auto marker = std::find_if(m_markers.cbegin(), m_markers.cend(),
            [&position](const Marker &item) { return item.rect.contains(position); });
        return marker == m_markers.cend() ? nullptr : &*marker;
    }

    void showPopup(const Marker &marker)
    {
        if (m_popupWorkId == marker.work.id && m_popup->isVisible()) return;
        hidePopup();
        WorkSummary summary;
        summary.id = marker.work.id;
        summary.serialNumber = marker.work.serialNumber;
        summary.chineseTitle = marker.work.title;
        summary.releaseDate = marker.work.releaseDate;
        summary.imageUrl = marker.work.imageUrl;
        summary.highlightTagId = marker.work.highlightTagId;
        summary.standard = marker.work.standard;
        auto *card = new WorkCard(summary, m_coverDirectory, false, m_popup);
        connect(card, &WorkCard::activated, this, [this](qint64 workId) {
            hidePopup();
            if (activate) activate(workId);
        });
        connect(card, &WorkCard::editRequested, this, [this](qint64 workId) {
            hidePopup();
            if (activate) activate(workId);
        });
        m_popup->layout()->addWidget(card);
        m_popupWorkId = marker.work.id;
        m_popup->adjustSize();
        QPoint position = mapToGlobal(QPoint(marker.rect.center().x() - m_popup->width() / 2,
                                             marker.rect.bottom() + 8));
        if (QScreen *screen = QApplication::screenAt(mapToGlobal(marker.rect.center()))) {
            const QRect available = screen->availableGeometry();
            position.setX(qBound(available.left(), position.x(),
                                 available.right() - m_popup->width() + 1));
            if (position.y() + m_popup->height() > available.bottom()) {
                position.setY(mapToGlobal(QPoint(0, marker.rect.top() - m_popup->height() - 8)).y());
            }
        }
        m_popup->move(position);
        m_popup->show();
        m_popup->raise();
    }

    void schedulePopupHide()
    {
        if (m_popup->isVisible()) m_hideTimer->start();
    }

    void hidePopup()
    {
        m_hideTimer->stop();
        m_popup->hide();
        m_popupWorkId = 0;
        if (QLayoutItem *item = m_popup->layout()->takeAt(0)) {
            delete item->widget();
            delete item;
        }
    }

    void rebuild()
    {
        hidePopup();
        m_markers.clear();
        m_stems.clear();
        QMap<QDate, QList<const PersonWorkSummary *>> dates;
        QList<const PersonWorkSummary *> unknown;
        for (const auto &work : std::as_const(m_works)) {
            const QDate date = QDate::fromString(work.releaseDate.left(10), Qt::ISODate);
            if (date.isValid()) dates[date].append(&work);
            else unknown.append(&work);
        }
        if (!m_works.isEmpty()) {
            QDate coreMin = defaultStart;
            QDate coreMax = defaultEnd;
            if (!dates.isEmpty()) {
                coreMin = qMin(coreMin, dates.firstKey());
                coreMax = qMax(coreMax, dates.lastKey());
            }
            m_dateMin = coreMin.addDays(-120).addYears(-90);
            const QDate end = coreMax.addDays(120).addYears(90);
            m_numDays = m_dateMin.daysTo(end) + 1;
            // Respect QWidget's width limit, including the unknown-date section.
            m_ppd = qMin(m_ppd, (QWIDGETSIZE_MAX - unknownWidth - 2.0 * trackPad - 32)
                                      / m_numDays);
            // Keep room to pan and preserve the pointer anchor even at minimum zoom.
            const int extraDays = qMax(0, qCeil((3.0 * m_scroll->viewport()->width()
                                                / m_ppd - m_numDays) / 2.0));
            m_dateMin = m_dateMin.addDays(-extraDays);
            m_numDays += 2 * extraDays;
        } else {
            m_dateMin = {};
            m_numDays = 0;
        }
        m_unknownX = unknown.isEmpty() ? -1 : qRound(trackPad + m_numDays * m_ppd + 24);
        QList<int> laneEnds;
        int bottom = axisY;
        const auto placeGroup = [&](int x, const QList<const PersonWorkSummary *> &group, int lane) {
            int y = axisY;
            for (const auto *work : group) {
                // Near dates can share a rounded x coordinate at low zoom.
                while (lane < laneEnds.size() && x - laneEnds.at(lane) < markerSize)
                    ++lane;
                if (lane >= laneEnds.size()) laneEnds.append(x);
                else laneEnds[lane] = x;
                y = axisY + lane * laneStride;
                m_markers.append({QRect(x - markerSize / 2 + 1, y - markerSize / 2 + 1,
                                        markerSize, markerSize), *work});
                ++lane;
            }
            bottom = qMax(bottom, y);
            if (y > axisY) m_stems.append(QPoint(x, y));
        };

        // Reserve the axis lane so same-day groups start below it.
        laneEnds.append(-markerSize);
        m_markers.reserve(m_works.size());
        for (auto it = dates.cbegin(); it != dates.cend(); ++it) {
            const int x = qRound(trackPad + m_dateMin.daysTo(it.key()) * m_ppd);
            placeGroup(x, it.value(), it.value().size() == 1 ? 0 : 1);
        }
        if (!unknown.isEmpty()) placeGroup(m_unknownX, unknown, 1);
        const int width = m_unknownX >= 0 ? m_unknownX + unknownWidth
                                          : qRound(trackPad + m_numDays * m_ppd + 30);
        setFixedWidth(qMax(200, width));
        setMinimumHeight(qMax(96, bottom + 22));
        update();
    }

    QScrollArea *m_scroll = nullptr;
    QString m_coverDirectory;
    TimelineHoverPopup *m_popup = nullptr;
    QTimer *m_hideTimer = nullptr;
    QList<PersonWorkSummary> m_works;
    QList<Marker> m_markers;
    QList<QPoint> m_stems;
    QDate m_dateMin;
    int m_numDays = 0;
    int m_unknownX = -1;
    double m_ppd = 0.25;
    qint64 m_popupWorkId = 0;
    bool m_middlePanning = false;
    QPointF m_lastGlobalPosition;
};

ActressWorkTimeline::ActressWorkTimeline(QWidget *parent, QString coverDirectory) : QWidget(parent)
{
    setObjectName(QStringLiteral("ActressWorkTimeline"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_canvas = new TimelineCanvas(m_scroll, std::move(coverDirectory), m_scroll);
    m_canvas->activate = [this](qint64 id) { emit workRequested(id); };
    m_scroll->setWidget(m_canvas);
    m_empty = new DesignLabel(QStringLiteral("暂无关联作品"), this);
    static_cast<DesignLabel *>(m_empty)->setTone(QStringLiteral("muted"));
    layout->addWidget(m_empty, 0, Qt::AlignCenter);
    layout->addWidget(m_scroll, 1);
    setMinimumHeight(130);
    setWorks({});
}

void ActressWorkTimeline::setWorks(const QList<PersonWorkSummary> &works)
{
    m_canvas->setWorks(works);
    m_empty->setVisible(works.isEmpty());
    m_scroll->setVisible(!works.isEmpty());
    m_initialFitPending = !works.isEmpty();
    if (isVisible()) fitDefaultWindow();
}

void ActressWorkTimeline::setFilteredWorks(const QList<WorkSummary> &works)
{
    QList<PersonWorkSummary> summaries;
    QSet<qint64> seen;
    summaries.reserve(works.size());
    for (const auto &work : works) {
        if (seen.contains(work.id)) continue;
        seen.insert(work.id);
        summaries.append({work.id, work.serialNumber, work.chineseTitle, work.releaseDate,
                          work.imageUrl, work.highlightTagId, work.standard});
    }
    setWorks(summaries);
}

QList<PersonWorkSummary> ActressWorkTimeline::works() const { return m_canvas->works(); }
int ActressWorkTimeline::markerCount() const noexcept { return m_canvas->markerCount(); }
double ActressWorkTimeline::pixelsPerDay() const noexcept { return m_canvas->ppd(); }
QPoint ActressWorkTimeline::markerCenter(qint64 workId) const
{
    return m_canvas->markerCenter(workId);
}
bool ActressWorkTimeline::hoverPreviewVisible() const noexcept
{
    return m_canvas->hoverPreviewVisible();
}

void ActressWorkTimeline::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    fitDefaultWindow();
}

void ActressWorkTimeline::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_initialFitPending) fitDefaultWindow();
}

void ActressWorkTimeline::fitDefaultWindow()
{
    if (!m_initialFitPending || m_initialFitScheduled || !isVisible()) return;
    m_initialFitScheduled = true;
    // The workspace layout and QScrollArea geometry settle after show/resize.
    QTimer::singleShot(0, this, [this] {
        m_initialFitScheduled = false;
        if (!m_initialFitPending || !isVisible() || m_scroll->viewport()->width() < 100) return;
        const int viewportWidth = m_scroll->viewport()->width();
        const int span = defaultStart.daysTo(defaultEnd) + 1;
        m_scroll->verticalScrollBar()->setValue(0);
        m_canvas->setPpd(qBound(minPpd,
            (viewportWidth - 64.0) / static_cast<double>(span), maxPpd));
        m_initialFitScheduled = true;
        // Resizing the canvas can change scrollbar ranges and viewport geometry.
        QTimer::singleShot(0, this, [this, viewportWidth] {
            m_initialFitScheduled = false;
            if (!m_initialFitPending || !isVisible()) return;
            if (viewportWidth != m_scroll->viewport()->width()) {
                fitDefaultWindow();
                return;
            }
            const int day = m_canvas->dateMinimum().daysTo(defaultStart);
            m_scroll->horizontalScrollBar()->setValue(
                qMax(0, qRound(trackPad + day * m_canvas->ppd() - 32)));
            m_initialFitPending = false;
        });
    });
}

} // namespace darkeye
