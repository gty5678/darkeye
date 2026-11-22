#pragma once

#include <QDialog>
#include <QSqlDatabase>

class QDateTimeEdit;
class QTextEdit;

namespace darkeye
{

class RatingSelector;

class AddMakeLoveDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit AddMakeLoveDialog(QSqlDatabase privateDatabase, QWidget *parent = nullptr);

signals:
    void recordAdded();

private slots:
    void commit();

private:
    QSqlDatabase m_privateDatabase;
    RatingSelector *m_rating = nullptr;
    QDateTimeEdit *m_dateTime = nullptr;
    QTextEdit *m_comment = nullptr;
};

} // namespace darkeye
