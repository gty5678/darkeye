#pragma once

#include "ui/layouts/myads/DockTheme.h"

#include <QIcon>
#include <QHash>
#include <QPoint>
#include <QWidget>

class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QHBoxLayout;
class QMenu;
class QStackedWidget;
class QTabBar;

namespace darkeye::myads {

// KeepAlive detaches the closed page; its caller must reuse or destroy it.
enum class ClosePolicy { DeleteLater, KeepAlive };

struct PaneContent
{
    QString contentId;
    QString title;
    QWidget *widget = nullptr;
    QIcon icon;
    bool closeable = true;
    ClosePolicy closePolicy = ClosePolicy::DeleteLater;
    int index = -1;
    QString currentContentId;
};

class PaneWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit PaneWidget(const QString &paneId, QWidget *parent = nullptr,
                        bool retainFocusOnTabChange = false);

    [[nodiscard]] QString paneId() const;
    void applyTheme(const DockTheme &theme);
    void setIconOnly(bool iconOnly);
    [[nodiscard]] bool iconOnly() const noexcept;
    bool addContent(const QString &contentId, const QString &title, QWidget *widget,
                    const QIcon &icon = {}, bool closeable = true, int index = -1,
                    ClosePolicy closePolicy = ClosePolicy::DeleteLater);
    [[nodiscard]] PaneContent takeContent(const QString &contentId);
    bool restoreContent(const PaneContent &content);
    // Sends QCloseEvent to the page before removing it. ignore() vetoes the
    // request. Non-closeable tabs cannot be removed; use takeContent to move.
    bool removeContent(const QString &contentId);
    [[nodiscard]] QStringList contentIds() const;
    [[nodiscard]] int contentCount() const;
    [[nodiscard]] QString currentContentId() const;
    bool setCurrentContentId(const QString &contentId);
    void focusTabBar();
    void addTabBarButton(QWidget *button, Qt::Edge edge);
    [[nodiscard]] QString contentTitle(const QString &contentId) const;
    [[nodiscard]] QWidget *contentWidget(const QString &contentId) const;
    [[nodiscard]] bool isContentCloseable(const QString &contentId) const;
    [[nodiscard]] ClosePolicy contentClosePolicy(const QString &contentId) const;
    // The caller owns the returned menu; unknown content returns nullptr.
    [[nodiscard]] QMenu *createTabContextMenu(const QString &contentId);
    // Returns a gap in [0, contentCount()], or -1 outside the tab strip.
    [[nodiscard]] int tabInsertionIndex(const QPoint &globalPosition) const;
    [[nodiscard]] QRect tabInsertionRect(int index) const;
    [[nodiscard]] QRect contentRect() const;

signals:
    void currentContentChanged();
    void layoutPreferencesChanged();
    void activationRequested();
    void contentClosed(const QString &contentId);
    void paneEmpty(darkeye::myads::PaneWidget *pane);
    void dragMoved(darkeye::myads::PaneWidget *target, const QPoint &globalPosition);
    void tabDropped(darkeye::myads::PaneWidget *target, const QPoint &globalPosition,
                    const QString &sourcePaneId, const QString &contentId);
    void dragFinished();
    void tabContextMenuCreated(QMenu *menu, const QString &contentId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;

private:
    friend class WorkspaceWidget;
    bool requestContentClose(const QString &contentId);
    bool disposeContent(const QString &contentId);
    [[nodiscard]] int indexOf(const QString &contentId) const;
    void updateCloseButton(int index);
    void clearWindowFocus();
    void switchContent(int index);

    QString m_paneId;
    QHBoxLayout *m_tabRow = nullptr;
    QTabBar *m_tabBar = nullptr;
    QStackedWidget *m_stack = nullptr;
    bool m_retainFocusOnTabChange;
    bool m_iconOnly = false;
    QHash<QString, QString> m_titles;
    QHash<QString, bool> m_closeable;
    QHash<QString, ClosePolicy> m_closePolicies;
    DockTheme m_theme;
};

inline constexpr auto MyAdsTabMimeType = "application/x-darkeye-myads-tab";

} // namespace darkeye::myads


