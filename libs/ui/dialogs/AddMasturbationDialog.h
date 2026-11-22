#pragma once

#include <QDialog>
#include <QSqlDatabase>

class QDateTimeEdit;
class QLineEdit;
class QTextEdit;

namespace darkeye
{

class RatingSelector;

class AddMasturbationDialog final : public QDialog
{
    Q_OBJECT

public:
    AddMasturbationDialog(QSqlDatabase publicDatabase, QSqlDatabase privateDatabase,
                          QWidget *parent = nullptr);

signals:
    void recordAdded();

private slots:
    void commit();

private:
    QSqlDatabase m_publicDatabase;
    QSqlDatabase m_privateDatabase;
    QLineEdit *m_serialNumber = nullptr;
    RatingSelector *m_rating = nullptr;
    QLineEdit *m_tool = nullptr;
    QDateTimeEdit *m_dateTime = nullptr;
    QTextEdit *m_comment = nullptr;
};

} // namespace darkeye
