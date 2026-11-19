#pragma once
#include "DvdItemViewModel.h"
#include "DvdInteractionController.h"
#include "render3d/RhiSceneWidget.h"
#include <QWidget>
#include <QRectF>
#include <memory>

namespace darkeye::dvd_scene {
class DvdSceneWidget final:public QWidget {
    Q_OBJECT
public:
    explicit DvdSceneWidget(QWidget *parent=nullptr);
    ~DvdSceneWidget() override;
    bool setModel(std::shared_ptr<const render3d::ModelAsset> model);
    bool setItems(QList<DvdItemViewModel> items);
    bool updateItem(const DvdItemViewModel &item);
    bool focusItem(const QString &id);
    void returnToShelf();
    void toggleOpen();
    [[nodiscard]] bool canExpand() const;
    void scrollToIndex(int index);
    void setCameraX(float x, bool immediate=false);
    [[nodiscard]] float cameraX() const;
    [[nodiscard]] float cameraTargetX() const;
    [[nodiscard]] bool isExpanded() const;
    [[nodiscard]] bool isFullyExpanded() const;
    // Both lid hinges are within 8 degrees of their closed pose.
    [[nodiscard]] bool isNearlyClosed() const;
    [[nodiscard]] std::optional<QRectF> selectedFrontRect() const;
    [[nodiscard]] std::optional<QRectF> selectedCaseRect() const;
    [[nodiscard]] std::optional<QPointF> selectedDiscCenter() const;
    [[nodiscard]] std::optional<QPointF> selectedSpineCenter() const;
    [[nodiscard]] std::optional<QPointF> selectedInfoAnchor() const;
    void resetCamera();
    void setDarkTheme(bool dark);
    void setCoverBudget(quint64 bytes);
    [[nodiscard]] QString selectedId() const;
    [[nodiscard]] DvdInteractionController::State state() const;
    [[nodiscard]] int itemCount() const;
    [[nodiscard]] int visibleItemCount() const;
    [[nodiscard]] int pendingCovers() const;
    [[nodiscard]] quint64 cachedCoverBytes() const;
    [[nodiscard]] render3d::RhiSceneWidget *renderWidget() const;
    [[nodiscard]] const QList<DvdItemViewModel> &items() const;
signals:
    void dvdClicked(const QString &stableId);
    void dvdDoubleClicked(const QString &stableId);
    void selectionChanged(const QString &stableId);
    void stateChanged(const QString &state);
    void expandedChanged(bool expanded);
    void cameraPositionChanged();
    void discClicked(const QString &stableId);
    void presentationChanged();
    void sceneError(const QString &message);
    void coverWarning(const QString &message);
    void expansionBlocked();
protected:
    bool eventFilter(QObject *object,QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
} // namespace darkeye::dvd_scene
