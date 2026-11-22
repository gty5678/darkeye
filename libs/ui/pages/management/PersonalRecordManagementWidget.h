#pragma once

#include <QSqlDatabase>
#include <QWidget>

class QComboBox;
class QLineEdit;
class QSqlTableModel;
class QTableView;

namespace darkeye
{

class ThemeService;

/** Editable private personal-record tables, matching Python's “综合管理” tab. */
class PersonalRecordManagementWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit PersonalRecordManagementWidget(QSqlDatabase privateDatabase, ThemeService &themes,
                                            QWidget *parent = nullptr);

    void refresh();

private:
    void loadTable(const QString &tableName);
    void addRow();
    void deleteSelectedRows();
    void saveChanges();
    void revertChanges();
    void exportCsv();

    QSqlDatabase m_privateDatabase;
    ThemeService &m_themes;
    QComboBox *m_tableChoice = nullptr;
    QLineEdit *m_search = nullptr;
    QTableView *m_table = nullptr;
    QSqlTableModel *m_model = nullptr;
};

} // namespace darkeye
