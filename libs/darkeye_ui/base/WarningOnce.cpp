#include "darkeye_ui/base/WarningOnce.h"

#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

namespace darkeye
{

void WarningOnce::warn(const QString &key, const QString &message)
{
    static QMutex mutex;
    static QSet<QString> emittedKeys;
    const QMutexLocker locker(&mutex);
    if (emittedKeys.contains(key))
    {
        return;
    }
    emittedKeys.insert(key);
    qWarning().noquote() << message;
}

} // namespace darkeye
