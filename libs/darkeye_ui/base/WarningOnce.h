#pragma once

#include <QString>

namespace darkeye
{

class WarningOnce final
{
public:
    static void warn(const QString &key, const QString &message);
};

} // namespace darkeye
