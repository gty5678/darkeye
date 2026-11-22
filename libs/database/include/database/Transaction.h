#pragma once

#include <QSqlDatabase>
#include <QString>

namespace darkeye {

class Transaction final
{
public:
    explicit Transaction(QSqlDatabase database);
    ~Transaction();

    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;

    [[nodiscard]] bool isActive() const noexcept;
    [[nodiscard]] const QString &errorString() const noexcept;
    bool commit();
    void rollback();

private:
    QSqlDatabase m_database;
    bool m_active = false;
    QString m_errorString;
};

} // namespace darkeye

