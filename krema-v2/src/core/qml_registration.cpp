#include "KremaGlobals.hpp"
#include "attention/AttentionManager.hpp"
#include "engine/BaseIsland.hpp"
#include "engine/BaseItem.hpp"
#include "engine/BasePanel.hpp"
#include "engine/LayoutManager.hpp"
#include "krema_core_export.h"
#include <QQmlEngine>
#include <QQmlExtensionPlugin>

namespace Krema
{

KREMA_CORE_EXPORT bool g_debugGeom = false;
KREMA_CORE_EXPORT bool g_debugHit = false;

KREMA_CORE_EXPORT void registerQmlTypes()
{
    qmlRegisterUncreatableMetaObject(Krema::staticMetaObject, "org.krema.ui", 1, 0, "Krema", "Access to Enums");

    qmlRegisterType<BaseItem>("org.krema.ui", 1, 0, "BaseItem");
    qmlRegisterType<BaseIsland>("org.krema.ui", 1, 0, "BaseIsland");
    qmlRegisterType<BasePanel>("org.krema.ui", 1, 0, "BasePanel");
    qmlRegisterType<LayoutManager>("org.krema.ui", 1, 0, "LayoutManager");
}

} // namespace Krema
