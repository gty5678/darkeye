#pragma once
#include <QFrame>
#include <QMap>
#include <QSqlDatabase>
class QAction;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
namespace darkeye {
class WorkspaceSearch final : public QFrame {
    Q_OBJECT
public:
    WorkspaceSearch(QSqlDatabase database, QMap<QString, QString> pages, QWidget *parent = nullptr);
    enum class Mode { Entities, Commands };
    void open(Mode mode, QList<QPair<QString, QString>> recent);
    void search(const QString &text);
    void setFullResults();
    void setBatchActions(const QList<QAction *> &actions);
signals:
    void openRequested(const QString &route);
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
private:
    void focusSearch();
    void positionPopup();
    void updateResults();
    void activate(QListWidgetItem *item);
    QSqlDatabase m_database;
    QMap<QString, QString> m_pages;
    QList<QAction *> m_batchActions;
    QList<QPair<QString, QString>> m_recent;
    QLineEdit *m_input;
    QListWidget *m_results;
    QPushButton *m_more;
    Mode m_mode = Mode::Entities;
    bool m_fullResults = false;
    int m_limit = 20;
    QString m_resultQuery;
};
}
