#pragma once

#include <QFrame>
#include <QUrl>

class QLabel;

namespace darkeye {

class ThemeService;

class TokenLinkCard final : public QFrame
{
    Q_OBJECT

public:
    explicit TokenLinkCard(const QString &title, const QString &description,
                           const QString &url, ThemeService *themes = nullptr,
                           QWidget *parent = nullptr);
    QUrl url() const;
    void setUrl(const QUrl &url);

signals:
    void activated(const QUrl &url);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void activate();
    void refreshIcon();
    QUrl m_url;
    ThemeService *m_themes = nullptr;
    QLabel *m_icon = nullptr;
};

} // namespace darkeye
