#pragma once

#include <QModelIndex>
#include <QSqlDatabase>
#include <QWidget>

class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QSqlQueryModel;
class QSortFilterProxyModel;
class QStyledItemDelegate;
class QTableView;

namespace darkeye
{

class ThemeService;

/** Read-only work, actress, and actor summary queries, matching Python's
 *  "汇总查询表" management tab. */
class SummaryQueryWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit SummaryQueryWidget(QSqlDatabase database, ThemeService &themes,
                                QWidget *parent = nullptr);

    void refresh();

signals:
    void workRequested(qint64 workId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void clearCompletenessPresentation();
    void loadQuery();
    void exportCsv();
    void performSearch(const QString &text);
    void navigateSearch(int offset);

    QSqlDatabase m_database;
    ThemeService &m_themes;
    QComboBox *m_queryType = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_searchResult = nullptr;
    QPushButton *m_previousSearch = nullptr;
    QPushButton *m_nextSearch = nullptr;
    QTableView *m_table = nullptr;
    QSqlQueryModel *m_model = nullptr;
    QSortFilterProxyModel *m_filterModel = nullptr;
    QStyledItemDelegate *m_completenessDelegate = nullptr;
    QStyledItemDelegate *m_editDelegate = nullptr;
    int m_completenessBitsColumn = -1;
    int m_completenessScoreColumn = -1;
    QModelIndexList m_searchResults;
    int m_currentSearchResult = -1;
};

} // namespace darkeye
