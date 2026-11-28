#include "UiResources.h"

#include <QResource>

// Q_INIT_RESOURCE must resolve the generated symbol in the global namespace.
static void registerUiResources()
{
    Q_INIT_RESOURCE(darkeye_ui_resources);
}

namespace darkeye {

void initializeUiResources()
{
    static const bool initialized = [] {
        registerUiResources();
        return true;
    }();
    Q_UNUSED(initialized);
}

} // namespace darkeye
