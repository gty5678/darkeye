#pragma once

#include <QPointer>
#include <QWidget>

class QLabel;
class QTimer;

namespace darkeye {

class ThemeService;

class ToastNotification final : public QWidget
{
    Q_OBJECT

public:
    enum class Level { Info, Success, Warning, Error };

    explicit ToastNotification(QWidget *anchor, const QString &message,
                               Level level = Level::Info,
                               int durationMilliseconds = 2500,
                               ThemeService *themeService = nullptr);

    static ToastNotification *showMessage(QWidget *anchor, const QString &message,
                                          Level level = Level::Info,
                                          int durationMilliseconds = 2500,
                                          ThemeService *themeService = nullptr);
    static ToastNotification *showSuccess(QWidget *anchor, const QString &message,
                                          ThemeService *themeService = nullptr,
                                          int durationMilliseconds = 2500);
    static ToastNotification *showWarning(QWidget *anchor, const QString &message,
                                          ThemeService *themeService = nullptr,
                                          int durationMilliseconds = 3000);
    static ToastNotification *showError(QWidget *anchor, const QString &message,
                                        ThemeService *themeService = nullptr,
                                        int durationMilliseconds = 3500);

    [[nodiscard]] Level level() const noexcept;

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void reposition();
    static void repositionGroup(QWidget *anchor);

    QPointer<QWidget> m_anchor;
    QPointer<ThemeService> m_themeService;
    QLabel *m_label = nullptr;
    QTimer *m_timer = nullptr;
    Level m_level = Level::Info;
};

using Toast = ToastNotification;
using Notification = ToastNotification;

} // namespace darkeye
