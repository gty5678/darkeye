#pragma once

#include <QImage>
#include <QLabel>
#include <QPainterPath>

namespace darkeye {

class OctImage final : public QLabel
{
    Q_OBJECT

public:
    explicit OctImage(const QString &imagePath = {}, const QString &basePath = {},
                      int diameter = 150, bool shadow = true,
                      QWidget *parent = nullptr, bool deferImage = false);
    QString source() const;
    void setImageActive(bool active);
    void updateImage(const QString &imagePath);
    bool containsAvatarPoint(const QPointF &point) const;

signals:
    void imageReady(const QImage &image);
    void imageLoaded(const QString &path);

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    QString resolvedPath(const QString &imagePath) const;
    QPainterPath avatarPath() const;
    void startLoad();
    void applyImage(quint64 request, const QString &path, const QImage &image,
                    const QImage &rendered);

    int m_diameter = 150;
    QString m_basePath;
    QString m_source;
    quint64 m_request = 0;
    bool m_shadow = true;
    bool m_imageActive = true;
    bool m_loading = false;
    QPixmap m_renderCache;
};

} // namespace darkeye
