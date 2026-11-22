#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

namespace darkeye {

class EmptyState final : public QWidget
{
    Q_OBJECT

public:
    explicit EmptyState(const QString &title = {}, const QString &description = {},
                        const QString &actionText = {}, QWidget *parent = nullptr);

    void setTitle(const QString &title);
    void setDescription(const QString &description);
    void setIconText(const QString &text);
    void setActionText(const QString &text);

signals:
    void actionTriggered();

private:
    QLabel *m_icon = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_description = nullptr;
    QPushButton *m_action = nullptr;
};

} // namespace darkeye
