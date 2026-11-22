#pragma once

#include <QWidget>

class QEvent;

namespace darkeye
{

class ClickableLabel;
class OctImage;

class PersonCard final : public QWidget
{
    Q_OBJECT

public:
    explicit PersonCard(qint64 personId, const QString &name, const QString &imagePath = {},
                        const QString &imageDirectory = {}, QWidget *parent = nullptr, bool deferImage = false);

    [[nodiscard]] qint64 personId() const noexcept;
    [[nodiscard]] QString name() const;
    [[nodiscard]] OctImage *avatar() const;
    void updateData(qint64 personId, const QString &name, const QString &imagePath);

signals:
    void activated(qint64 personId);
    void editRequested(qint64 personId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    qint64 m_personId = 0;
    OctImage *m_avatar = nullptr;
    ClickableLabel *m_nameLabel = nullptr;
};

} // namespace darkeye
