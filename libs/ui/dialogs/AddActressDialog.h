#pragma once

#include <QDialog>
#include <QSqlDatabase>

class QLineEdit;

namespace darkeye
{

class AddActressDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit AddActressDialog(QSqlDatabase publicDatabase, QWidget *parent = nullptr);

signals:
    void personAdded(qint64 actressId);

private slots:
    void submit();
    void searchJapaneseName();

private:
    QSqlDatabase m_publicDatabase;
    QLineEdit *m_chineseName = nullptr;
    QLineEdit *m_japaneseName = nullptr;
};

} // namespace darkeye
