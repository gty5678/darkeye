#include "database/Transaction.h"

#include <QSqlError>

namespace darkeye {

Transaction::Transaction(QSqlDatabase database)
    : m_database(std::move(database)), m_active(m_database.transaction())
{
    if (!m_active) {
        m_errorString = m_database.lastError().text();
    }
}

Transaction::~Transaction()
{
    if (m_active) {
        m_database.rollback();
    }
}

bool Transaction::isActive() const noexcept
{
    return m_active;
}

const QString &Transaction::errorString() const noexcept
{
    return m_errorString;
}

bool Transaction::commit()
{
    if (!m_active) {
        return false;
    }
    if (!m_database.commit()) {
        m_errorString = m_database.lastError().text();
        return false;
    }
    m_active = false;
    return true;
}

void Transaction::rollback()
{
    if (m_active) {
        m_database.rollback();
        m_active = false;
    }
}

} // namespace darkeye

