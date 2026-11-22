#pragma once

#include <QLabel>

class QPropertyAnimation;

namespace darkeye {

class HeartLabel final : public QLabel
{
    Q_OBJECT
    Q_PROPERTY(qreal scale READ scale WRITE setScale)

public:
    explicit HeartLabel(QWidget *parent = nullptr);
    bool isChecked() const;
    bool state() const;
    bool getState() const { return state(); }
    void setState(bool state);
    qreal scale() const;
    void setScale(qreal scale);

signals:
    void clicked(bool checked);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    bool m_checked = false;
    qreal m_scale = 1.0;
    QPropertyAnimation *m_animation = nullptr;
};

} // namespace darkeye
