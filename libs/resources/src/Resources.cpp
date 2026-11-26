#include "app/Resources.h"

#include <QResource>
#include <mutex>

// Q_INIT_RESOURCE must resolve the generated symbol in the global namespace.
static void initializeDarkeyeResourceCollection()
{
    Q_INIT_RESOURCE(resources);
}

namespace darkeye {

void initializeResources()
{
    static std::once_flag initializationFlag;
    std::call_once(initializationFlag, initializeDarkeyeResourceCollection);
}

} // namespace darkeye
