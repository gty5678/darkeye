#pragma once

#include "domain/Work.h"

#include <QColor>
#include <QWidget>

class QLabel;

namespace darkeye {

class AsyncImageLabel;

class WorkCard final : public QWidget
{
    Q_OBJECT

public:
    explicit WorkCard(const WorkSummary &work, const QString &coverDirectory,
                      bool largeCoverView = false, QWidget *parent = nullptr,
                      bool deferCoverLoad = false);

    [[nodiscard]] qint64 workId() const noexcept;
    void setCoverActive(bool active);

signals:
    void activated(qint64 workId);
    void editRequested(qint64 workId);
    void coverLoadFinished();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    qint64 m_workId = 0;
    QColor m_backgroundColor;
    bool m_largeCoverView = false;
    AsyncImageLabel *m_cover = nullptr;
    QLabel *m_title = nullptr;
};

} // namespace darkeye
