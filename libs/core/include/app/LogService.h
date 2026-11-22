#pragma once

#include <QString>

namespace darkeye {

class LogService final
{
public:
    static bool initialize(const QString &filePath);
    static void shutdown();
};

} // namespace darkeye

