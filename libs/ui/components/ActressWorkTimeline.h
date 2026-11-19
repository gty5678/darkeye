#pragma once

#include "domain/Person.h"
#include "domain/Work.h"

#include <QPoint>
#include <QWidget>

class QScrollArea;

namespace darkeye {

class TimelineCanvas;

class ActressWorkTimeline final : public QWidget
{
    Q_OBJECT

public:
    explicit ActressWorkTimeline(QWidget *parent = nullptr, QString coverDirectory = {});

    void setWorks(const QList<PersonWorkSummary> &works);
    void setFilteredWorks(const QList<WorkSummary> &works);
    [[nodiscard]] QList<PersonWorkSummary> works() const;
    [[nodiscard]] int markerCount() const noexcept;
    [[nodiscard]] double pixelsPerDay() const noexcept;
    [[nodiscard]] QPoint markerCenter(qint64 workId) const;
    [[nodiscard]] bool hoverPreviewVisible() const noexcept;

signals:
    void workRequested(qint64 workId);

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void fitDefaultWindow();

    QScrollArea *m_scroll = nullptr;
    TimelineCanvas *m_canvas = nullptr;
    QWidget *m_empty = nullptr;
    bool m_initialFitPending = false;
    bool m_initialFitScheduled = false;
};

} // namespace darkeye
