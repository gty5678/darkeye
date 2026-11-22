#pragma once

#include <QWidget>

class QLabel;

namespace darkeye {

class RatingSelector final : public QWidget
{
    Q_OBJECT

public:
    explicit RatingSelector(QWidget *parent = nullptr);

    int rating() const;
    int getRating() const { return rating(); }
    void setRating(int rating, bool notify = false);

signals:
    void ratingChanged(int rating);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refresh();

    QList<QLabel *> m_hearts;
    int m_rating = 0;
    int m_hoverRating = -1;
};

using HeartRatingWidget = RatingSelector;

} // namespace darkeye
