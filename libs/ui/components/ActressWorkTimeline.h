#pragma once

#include "domain/Person.h"
#include "domain/Work.h"

#include <QPoint>
#include <QWidget>

class QScrollArea;

namespace darkeye {

class OctImage;
class ThemeService;
class TimelineCanvas;

class ActressWorkTimeline final : public QWidget
{
    Q_OBJECT

public:
    explicit ActressWorkTimeline(QWidget *parent = nullptr, QString coverDirectory = {},
                                 QString actressImageDirectory = {}, ThemeService *themes = nullptr);

    void setActress(const PersonDetails &actress);
    void setActresses(const QList<PersonDetails> &actresses);
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
    void updateTracks(QList<QList<PersonWorkSummary>> tracks, const QString &emptyMessage);
    void positionThumbnails();
    void fitDefaultWindow();

    QWidget *m_axisHead = nullptr;
    QString m_actressImageDirectory;
    QList<OctImage *> m_actressThumbnails;
    QScrollArea *m_scroll = nullptr;
    TimelineCanvas *m_canvas = nullptr;
    QWidget *m_empty = nullptr;
    bool m_initialFitPending = false;
    bool m_initialFitScheduled = false;
};

} // namespace darkeye
