#pragma once

#include <QPersistentModelIndex>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableView;

namespace darkeye {
class ThemeService;

// Python ModelSearch: locate matching rows without hiding the rest of the table.
class ModelSearchBar final : public QWidget
{
    Q_OBJECT

public:
    ModelSearchBar(QTableView *table, ThemeService &themes, QWidget *parent = nullptr);
    QLineEdit *input() const { return m_input; }
    void search();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void navigate(int direction);
    QTableView *m_table;
    QLineEdit *m_input;
    QLabel *m_result;
    QPushButton *m_previous;
    QPushButton *m_next;
    QList<QPersistentModelIndex> m_matches;
    int m_current = -1;
};
} // namespace darkeye
