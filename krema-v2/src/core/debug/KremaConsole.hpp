#pragma once

#include "krema_core_export.h"
#include <QString>

namespace Krema
{
namespace Debug
{

enum class Category {
    Core, // Cyan
    Protocol, // Magenta
    Interaction, // Yellow
    Error, // Red
    Performance // Green
};

class KREMA_CORE_EXPORT KremaConsole
{
public:
    static KremaConsole &instance();

    void log(Category category, const QString &message);

private:
    KremaConsole() = default;
    ~KremaConsole() = default;

    // Prevent copying
    KremaConsole(const KremaConsole &) = delete;
    KremaConsole &operator=(const KremaConsole &) = delete;

    QString colorize(Category category, const QString &message) const;
};

} // namespace Debug
} // namespace Krema

// Rule 13/26: Compile-Time Stripping
#ifdef KREMA_DEBUG
#define KREMA_DEBUG_LOG(msg) Krema::Debug::KremaConsole::instance().log(Krema::Debug::Category::Core, msg)
#define KREMA_PROTOCOL_LOG(msg) Krema::Debug::KremaConsole::instance().log(Krema::Debug::Category::Protocol, msg)
#define KREMA_GEOM_LOG(msg) Krema::Debug::KremaConsole::instance().log(Krema::Debug::Category::Interaction, msg)
#define KREMA_ERROR_LOG(msg) Krema::Debug::KremaConsole::instance().log(Krema::Debug::Category::Error, msg)
#define KREMA_PERF_LOG(msg) Krema::Debug::KremaConsole::instance().log(Krema::Debug::Category::Performance, msg)
#else
#define KREMA_DEBUG_LOG(msg)                                                                                                                                   \
    do {                                                                                                                                                       \
    } while (0)
#define KREMA_PROTOCOL_LOG(msg)                                                                                                                                \
    do {                                                                                                                                                       \
    } while (0)
#define KREMA_GEOM_LOG(msg)                                                                                                                                    \
    do {                                                                                                                                                       \
    } while (0)
#define KREMA_ERROR_LOG(msg)                                                                                                                                   \
    do {                                                                                                                                                       \
    } while (0)
#define KREMA_PERF_LOG(msg)                                                                                                                                    \
    do {                                                                                                                                                       \
    } while (0)
#endif
