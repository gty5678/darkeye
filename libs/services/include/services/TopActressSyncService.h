#pragma once

#include "database/repositories/PersonRepository.h"

#include <QObject>
#include <QUrl>

namespace darkeye
{

class CollectorClient;

struct TopActressSyncResult final
{
    bool succeeded = false;
    int received = 0;
    int inserted = 0;
    int existing = 0;
    int failed = 0;
    QString errorMessage;

    [[nodiscard]] QString summary() const;
};

class TopActressSyncService final : public QObject
{
    Q_OBJECT

public:
    explicit TopActressSyncService(
        QSqlDatabase database,
        QUrl endpoint = QUrl(QStringLiteral("http://127.0.0.1:56790/api/v1/top-actresses")),
        QObject *parent = nullptr);

    bool start();
    bool cancel();
    [[nodiscard]] bool isBusy() const noexcept;

signals:
    void finished(const darkeye::TopActressSyncResult &result);

private:
    PersonRepository m_people;
    CollectorClient *m_collector = nullptr;
    quint64 m_requestId = 0;
};

} // namespace darkeye

Q_DECLARE_METATYPE(darkeye::TopActressSyncResult)
