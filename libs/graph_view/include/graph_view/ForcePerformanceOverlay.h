#pragma once

#include <QFrame>
#include <QMap>
#include <QVariantMap>

class QCheckBox;
class QComboBox;
class QLabel;

// Owned by one graph view; never participates in the canvas layout.
class ForcePerformanceOverlay final : public QFrame
{
    Q_OBJECT
public:
    explicit ForcePerformanceOverlay(QWidget* parent);
    void setFps(float value) { m_fps = value; }
    void setTickTime(float value) { m_tick = value; }
    void setPaintTime(float value) { m_paint = value; }
    void setScale(float value) { m_scale = value; }
    void setAlpha(float value) { m_alpha = value; }
    void setDetails(const QVariantMap& details) { m_details = details; }
    void setBackend(const QString& backend);
    void setBackendMode(const QString& mode);
    void setProfilingEnabled(bool enabled);
signals:
    void closeRequested();
    void backendModeChanged(const QString& mode);
    void profilingChanged(bool enabled);
protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
private:
    void refresh();
    QComboBox* m_mode = nullptr;
    QCheckBox* m_profile = nullptr;
    QLabel* m_tickCaption = nullptr;
    QVariantMap m_details;
    QMap<QString, QLabel*> m_values;
    QString m_backend = QStringLiteral("cpu");
    float m_fps = -1, m_tick = -1, m_paint = -1, m_scale = 1, m_alpha = -1;
};
