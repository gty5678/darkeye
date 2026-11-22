#pragma once
#include <QFrame>
#include <QMap>
#include <QSet>
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
    enum class Presentation { Popup, ResultsPage };
    WorkspaceSearch(QSqlDatabase database, QMap<QString, QString> pages, QWidget *parent = nullptr,
                    Presentation presentation = Presentation::Popup);
    enum class Mode { Entities, Commands, IndependentActress };
    inline static const auto independentActressCommand = QStringLiteral("actress_detail_independent");
    void open(Mode mode, QList<QPair<QString, QString>> recent);
    void search(const QString &text);
    void addCommands(const QMap<QString, QString> &commands);
    void setBatchActions(const QList<QAction *> &actions);
signals:
    void openRequested(const QString &route);
    void independentActressRequested(qint64 personId);
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
private:
    void buildUi();
    void focusSearch();
    void positionPopup();
    void updateResults();
    [[nodiscard]] QString queryText() const;
    void activate(QListWidgetItem *item);
    QSqlDatabase m_database;
    QMap<QString, QString> m_entries;
    QSet<QString> m_commandRoutes;
    QList<QAction *> m_batchActions;
    QList<QPair<QString, QString>> m_recent;
    QLineEdit *m_input = nullptr;
    QListWidget *m_results = nullptr;
    QPushButton *m_more = nullptr;
    Mode m_mode = Mode::Entities;
    const bool m_fullResults;
    int m_limit = 20;
    QString m_pageQuery;
    QString m_resultQuery;
};
}
