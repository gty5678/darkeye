#pragma once

#include "domain/Work.h"

#include <QSqlDatabase>
#include <QVariantList>
#include <QWidget>

class QResizeEvent;
class QLabel;
class QMenu;
class QAction;
class QTimer;
class QScrollArea;
class QToolButton;

namespace darkeye
{

class FanartStripWidget;
class ThemeService;
class StateToggleButton;
class ForceViewSettingsPanel;
namespace dvd_scene { class DvdSceneWidget; }
namespace graph {
class GraphManager;
}
namespace graph_view {
class GraphViewWidget;
}

class DvdShelfView final : public QWidget
{
    Q_OBJECT

public:
    explicit DvdShelfView(QSqlDatabase database, QSqlDatabase privateDatabase,
                          graph::GraphManager *graphManager, const QString &coverDirectory,
                          const QString &fanartDirectory, QWidget *parent = nullptr);
    void setWorks(const QList<WorkSummary> &works);
    void enableGraphSettings(ThemeService &themes);
    bool openWork(qint64 workId);
    [[nodiscard]] qint64 currentWorkId() const;

    Q_PROPERTY(qreal cameraX READ cameraX NOTIFY cameraXChanged)
    Q_PROPERTY(qreal cameraTargetX READ cameraTargetX NOTIFY cameraTargetXChanged)
    Q_PROPERTY(QString expandedWorkTitle READ expandedWorkTitle NOTIFY expandedWorkMetaChanged)
    Q_PROPERTY(QString expandedWorkStory READ expandedWorkStory NOTIFY expandedWorkMetaChanged)
    Q_PROPERTY(QString expandedWorkCode READ expandedWorkCode NOTIFY expandedWorkMetaChanged)
    Q_PROPERTY(QString expandedWorkReleaseDate READ expandedWorkReleaseDate NOTIFY expandedWorkMetaChanged)
    Q_PROPERTY(QVariantList expandedWorkActresses READ expandedWorkActresses NOTIFY expandedWorkMetaChanged)
    Q_PROPERTY(QVariantList expandedWorkActors READ expandedWorkActors NOTIFY expandedWorkMetaChanged)
    Q_PROPERTY(QVariantList expandedWorkTags READ expandedWorkTags NOTIFY expandedWorkMetaChanged)
    Q_PROPERTY(QString expandedWorkDirector READ expandedWorkDirector NOTIFY expandedWorkMetaChanged)
    Q_PROPERTY(QString expandedWorkStudio READ expandedWorkStudio NOTIFY expandedWorkMetaChanged)
    Q_PROPERTY(QString expandedWorkLabel READ expandedWorkLabel NOTIFY expandedWorkMetaChanged)
    Q_PROPERTY(QString expandedWorkSeries READ expandedWorkSeries NOTIFY expandedWorkMetaChanged)
    Q_PROPERTY(qint64 expandedWorkMakerId READ expandedWorkMakerId NOTIFY expandedWorkMetaChanged)
    Q_PROPERTY(qint64 expandedWorkLabelId READ expandedWorkLabelId NOTIFY expandedWorkMetaChanged)
    Q_PROPERTY(qint64 expandedWorkSeriesId READ expandedWorkSeriesId NOTIFY expandedWorkMetaChanged)
    Q_PROPERTY(bool expandedWorkFavorited READ expandedWorkFavorited NOTIFY expandedWorkFavoritedChanged)
    Q_PROPERTY(bool expandedWorkHasLocalVideo READ expandedWorkHasLocalVideo NOTIFY expandedWorkMetaChanged)

    [[nodiscard]] qreal cameraX() const { return m_cameraX; }
    [[nodiscard]] qreal cameraTargetX() const { return m_cameraTargetX; }
    [[nodiscard]] QString expandedWorkTitle() const { return m_title; }
    [[nodiscard]] QString expandedWorkStory() const { return m_story; }
    [[nodiscard]] QString expandedWorkCode() const { return m_code; }
    [[nodiscard]] QString expandedWorkReleaseDate() const { return m_releaseDate; }
    [[nodiscard]] QVariantList expandedWorkActresses() const { return m_actresses; }
    [[nodiscard]] QVariantList expandedWorkActors() const { return m_actors; }
    [[nodiscard]] QVariantList expandedWorkTags() const { return m_tags; }
    [[nodiscard]] QString expandedWorkDirector() const { return m_director; }
    [[nodiscard]] QString expandedWorkStudio() const { return m_studio; }
    [[nodiscard]] QString expandedWorkLabel() const { return m_label; }
    [[nodiscard]] QString expandedWorkSeries() const { return m_series; }
    [[nodiscard]] qint64 expandedWorkMakerId() const { return m_makerId; }
    [[nodiscard]] qint64 expandedWorkLabelId() const { return m_labelId; }
    [[nodiscard]] qint64 expandedWorkSeriesId() const { return m_seriesId; }
    [[nodiscard]] bool expandedWorkFavorited() const { return m_favorited; }
    [[nodiscard]] bool expandedWorkHasLocalVideo() const { return m_hasLocalVideo; }

    Q_INVOKABLE void selection_changed(int selectedDelegateIndex, int expandedDelegateIndex);
    Q_INVOKABLE void scroll_camera_by(qreal delta, qreal shelfLength);
    Q_INVOKABLE void set_camera_x(qreal cameraX);
    Q_INVOKABLE void on_cd_clicked(int virtualIndex);
    Q_INVOKABLE void refresh_expanded_work_meta(int virtualIndex);
    Q_INVOKABLE void refresh_expanded_favorite_state(int virtualIndex);
    Q_INVOKABLE void on_heart_clicked(int virtualIndex);
    Q_INVOKABLE void on_edit_clicked(int virtualIndex);
    Q_INVOKABLE void on_delete_clicked(int virtualIndex);
    Q_INVOKABLE void copy_to_clipboard(const QString &text) const;
    Q_INVOKABLE void on_actress_clicked(qint64 actressId);
    Q_INVOKABLE void on_actor_clicked(qint64 actorId);
    Q_INVOKABLE void on_tag_clicked(qint64 tagId);
    Q_INVOKABLE void on_director_clicked();
    Q_INVOKABLE void on_studio_clicked();
    Q_INVOKABLE void on_label_clicked();
    Q_INVOKABLE void on_series_clicked();
    Q_INVOKABLE void set_force_view_overlay(qreal centerX, qreal centerY);
    Q_INVOKABLE void set_fanart_strip_layout(qreal left, qreal top, qreal width);

signals:
    void workSelected(qint64 workId);
    void editRequested(qint64 workId);
    void workDeleted(qint64 workId);
    void favoriteChanged(qint64 workId, bool favorite);
    void actressRequested(qint64 actressId);
    void actorRequested(qint64 actorId);
    void tagRequested(qint64 tagId);
    void directorRequested(const QString &director);
    void makerRequested(qint64 makerId);
    void labelRequested(qint64 labelId);
    void seriesRequested(qint64 seriesId);
    void cameraXChanged();
    void cameraTargetXChanged();
    void expandedWorkMetaChanged();
    void expandedWorkFavoritedChanged();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void showContextMenu(const QPoint &position);
    void updateInitialCamera();
    [[nodiscard]] qreal initialCameraX() const;
    void updateSelection();
    void updateActions();
    void updateCoverInfo();
    void layoutOverlays();
    [[nodiscard]] int selectedIndex() const;
    [[nodiscard]] qint64 workIdAt(int virtualIndex) const;
    void clearExpandedMeta();
    void clearFanartOverlay();
    void showRelationGraph(qint64 workId);
    void hideOverlays();
    void updateRelationGraphGeometry(qreal centerX, qreal centerY);
    void updateRelationGraphControls();
    void updateFanartGeometry(qreal left, qreal top, qreal width);
    void persistFanartPreview();

    QSqlDatabase m_database;
    QSqlDatabase m_privateDatabase;
    graph::GraphManager *m_graphManager = nullptr;
    QString m_coverDirectory;
    QString m_fanartDirectory;
    QList<WorkSummary> m_works;
    qreal m_cameraX = 0.0;
    qreal m_cameraTargetX = 0.0;
    bool m_useInitialCamera = true;
    bool m_settingWorks = false;
    QString m_pendingOpen;
    dvd_scene::DvdSceneWidget *m_scene = nullptr;
    QMenu *m_contextMenu = nullptr;
    QAction *m_favoriteAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_playAction = nullptr;
    QPointF m_rightPress;
    bool m_rightDragged = false;
    QLabel *m_sceneError = nullptr;
    QScrollArea *m_coverInfo = nullptr;
    QWidget *m_coverContent = nullptr;
    QWidget *m_coverActions = nullptr;
    QToolButton *m_coverFavorite = nullptr;
    QByteArray m_coverInfoSignature;
    int m_coverTextWidth = 320;
    QString m_title, m_story, m_code, m_releaseDate, m_director, m_studio, m_label, m_series;
    QVariantList m_actresses, m_actors, m_tags;
    qint64 m_makerId = -1, m_labelId = -1, m_seriesId = -1;
    bool m_favorited = false;
    bool m_hasLocalVideo = false;
    graph_view::GraphViewWidget *m_relationGraph = nullptr;
    FanartStripWidget *m_fanartStrip = nullptr;
    QWidget *m_relationGraphContainer = nullptr;
    StateToggleButton *m_relationGraphSettingsButton = nullptr;
    ForceViewSettingsPanel *m_relationGraphSettingsPanel = nullptr;
    QWidget *m_fanartContainer = nullptr;
    qint64 m_overlayWorkId = 0;
    qint64 m_relationGraphWorkId = 0;
    qreal m_relationGraphCenterX = -1;
    qreal m_relationGraphCenterY = -1;
    qreal m_fanartLeft = -1;
    qreal m_fanartTop = -1;
    qreal m_fanartWidth = 0;
};

} // namespace darkeye
