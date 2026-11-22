#pragma once

#include <QPixmap>
#include <QStringList>
#include <QWidget>

class QHBoxLayout;

namespace darkeye {

class ThemeService;

class Avatar final : public QWidget
{
    Q_OBJECT

public:
    explicit Avatar(const QString &text = {}, const QString &imagePath = {},
                    int size = 32, ThemeService *themes = nullptr,
                    QWidget *parent = nullptr);
    void setText(const QString &text);
    void setImagePath(const QString &imagePath);
    QString initials() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_text;
    int m_size = 32;
    QPixmap m_pixmap;
    ThemeService *m_themes = nullptr;
};

class AvatarGroup final : public QWidget
{
    Q_OBJECT

public:
    explicit AvatarGroup(const QStringList &avatars = {}, int avatarSize = 32,
                         int overlap = 10, int maxVisible = 5,
                         ThemeService *themes = nullptr,
                         QWidget *parent = nullptr);
    void setAvatars(const QStringList &avatars);

private:
    int m_avatarSize = 32;
    int m_maxVisible = 5;
    ThemeService *m_themes = nullptr;
    QHBoxLayout *m_layout = nullptr;
};

} // namespace darkeye
