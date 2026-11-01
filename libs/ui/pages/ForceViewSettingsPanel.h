#pragma once

#include <QScrollArea>

class QColor;
class QFormLayout;
class QResizeEvent;
class QSlider;

namespace darkeye {

class ThemeService;

/** Floating controls for the force-directed graph, matching the Python view's
 * effect and display sections. */
class ForceViewSettingsPanel final : public QScrollArea
{
    Q_OBJECT

public:
    explicit ForceViewSettingsPanel(ThemeService &themeService, QWidget *parent = nullptr);
    [[nodiscard]] int preferredHeight() const;
    void setGraphNeighborDepthVisible(bool visible);

signals:
    void manyBodyStrengthChanged(float value);
    void centerStrengthChanged(float value);
    void linkStrengthChanged(float value);
    void linkDistanceChanged(float value);
    void radiusFactorChanged(float value);
    void textThresholdFactorChanged(float value);
    void linkWidthFactorChanged(float value);
    void neighborDepthChanged(int value);
    void graphNeighborDepthChanged(int value);
    void arrowEnabledChanged(bool enabled);
    void arrowScaleChanged(float value);
    void imageOverlayEnabledChanged(bool enabled);
    void nodeColorChanged(const QString &group, const QColor &color);
    void contentSizeChanged();

private:
    void resizeEvent(QResizeEvent *event) override;
    void updateContentWidth();
    static QSlider *slider(int minimum, int maximum, int value, QWidget *parent);
    QFormLayout *m_displayForm = nullptr;
    QSlider *m_graphNeighborDepth = nullptr;
};

} // namespace darkeye
