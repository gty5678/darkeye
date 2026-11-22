#pragma once

#include <QSqlDatabase>
#include <QString>

namespace darkeye {

class SqliteConnection final
{
public:
    SqliteConnection();
    ~SqliteConnection();

    SqliteConnection(const SqliteConnection &) = delete;
    SqliteConnection &operator=(const SqliteConnection &) = delete;
    SqliteConnection(SqliteConnection &&) = delete;
    SqliteConnection &operator=(SqliteConnection &&) = delete;

    bool open(const QString &databasePath, bool readOnly = false,
              QString *errorMessage = nullptr);
    void close();

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] bool isReadOnly() const noexcept;
    [[nodiscard]] const QString &databasePath() const noexcept;
    [[nodiscard]] QSqlDatabase database() const;
    [[nodiscard]] QString schemaVersion(QString *errorMessage = nullptr) const;

private:
    bool executePragma(const QString &statement, QString *errorMessage);

    QString m_connectionName;
    QSqlDatabase m_database;
    bool m_readOnly = false;
    QString m_databasePath;
};

} // namespace darkeye
