#pragma once

#include "database/repositories/WorkRepository.h"

#include <QWidget>

class QLineEdit;
class QTableView;

namespace darkeye
{

class ThemeService;
class WorkStateTableModel;

enum class WorkStateMode
{
    Active,
    RecycleBin,
};

class WorkBatchStateWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit WorkBatchStateWidget(WorkStateMode mode, QSqlDatabase database, ThemeService &themes,
                                  QString coverDirectory = {}, QString fanartDirectory = {},
                                  QWidget *parent = nullptr);

    void refresh();

signals:
    void worksChanged();

private:
    [[nodiscard]] QList<qint64> checkedIds() const;
    [[nodiscard]] QList<qint64> selectedIds() const;
    [[nodiscard]] QList<qint64> allIds() const;
    void setAllChecked(bool checked);
    void moveCheckedToRecycleBin();
    void restore(bool all);
    void removePermanently(bool all);
    static void removeManagedFiles(const QString &directory, const QStringList &relativeFiles);

    WorkStateMode m_mode;
    WorkRepository m_repository;
    ThemeService &m_themes;
    QString m_coverDirectory;
    QString m_fanartDirectory;
    QList<WorkStateRecord> m_records;
    QLineEdit *m_search = nullptr;
    QTableView *m_table = nullptr;
    WorkStateTableModel *m_model = nullptr;
};

} // namespace darkeye
