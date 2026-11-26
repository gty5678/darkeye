#include "app/Resources.h"

#include <QResource>
#include <mutex>

void initializeDarkeyeResourceCollection()
{
    Q_INIT_RESOURCE(resources);
}

namespace darkeye {

void Resources::ensureInitialized()
{
    static std::once_flag initializationFlag;
    std::call_once(initializationFlag, initializeDarkeyeResourceCollection);
}

} // namespace darkeye
