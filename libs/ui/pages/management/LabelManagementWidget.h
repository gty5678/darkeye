#pragma once

#include "database/repositories/ReferenceRepository.h"
#include "services/ReferenceJsonService.h"

#include <QWidget>
#include <QSet>

class QLineEdit;
class QLabel;
class QPushButton;
class QTableWidget;

namespace darkeye
{

class ThemeService;

class LabelManagementWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit LabelManagementWidget(ReferenceKind kind, QSqlDatabase database, ThemeService &themes,
                                  QWidget *parent = nullptr);

    void refresh();

signals:
    void referencesChanged(ReferenceKind kind);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void performSearch(bool selectFirst = true);
    void navigateSearch(int direction);
    void storeEditorEdits();
    void populateTable();
    void selectRow(int row);
    void beginNew();
    void saveCurrent();
    void removeCurrent();
    void redirectCurrent();
    void importJson();
    void exportJson();

    QSqlDatabase m_database;
    ReferenceKind m_kind;
    ReferenceRepository m_repository;
    ReferenceJsonService m_jsonService;
    ThemeService &m_themes;
    QList<ReferenceRecord> m_records;
    bool m_loading = false;
    QSet<qint64> m_removedIds;
    QSet<int> m_dirtyRows;
    QTableWidget *m_table = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_searchResult = nullptr;
    QPushButton *m_previousSearch = nullptr;
    QPushButton *m_nextSearch = nullptr;
    QList<int> m_searchRows;
    int m_searchIndex = -1;
    QLineEdit *m_chineseName = nullptr;
    QLineEdit *m_japaneseName = nullptr;
    QLineEdit *m_aliases = nullptr;
};

} // namespace darkeye
