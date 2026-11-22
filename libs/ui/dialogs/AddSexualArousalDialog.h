#pragma once

#include <QDialog>
#include <QSqlDatabase>

class QDateTimeEdit;
class QTextEdit;

namespace darkeye
{

class AddSexualArousalDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit AddSexualArousalDialog(QSqlDatabase privateDatabase, QWidget *parent = nullptr);

signals:
    void recordAdded();

private slots:
    void commit();

private:
    QSqlDatabase m_privateDatabase;
    QDateTimeEdit *m_dateTime = nullptr;
    QTextEdit *m_comment = nullptr;
};

} // namespace darkeye
