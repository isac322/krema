#include "KremaConsole.hpp"
#include <QDateTime>
#include <iostream>

namespace Krema
{
namespace Debug
{

KremaConsole &KremaConsole::instance()
{
    static KremaConsole instance;
    return instance;
}

QString KremaConsole::colorize(Category category, const QString &message) const
{
    // ANSI color codes
    const char *reset = "\033[0m";
    const char *color = reset;
    const char *prefix = "[UNKNOWN]";

    switch (category) {
    case Category::Core:
        color = "\033[36m"; // Cyan
        prefix = "[CORE]";
        break;
    case Category::Protocol:
        color = "\033[35m"; // Magenta
        prefix = "[PROTOCOL]";
        break;
    case Category::Interaction:
        color = "\033[33m"; // Yellow
        prefix = "[GEOM]";
        break;
    case Category::Error:
        color = "\033[31m"; // Red
        prefix = "[ERROR]";
        break;
    case Category::Performance:
        color = "\033[32m"; // Green
        prefix = "[PERF]";
        break;
    }

    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    return QString::fromLatin1("%1[%2]%3 %4%5").arg(color, timestamp, prefix, message, reset);
}

void KremaConsole::log(Category category, const QString &message)
{
    QString formatted = colorize(category, message);
    if (category == Category::Error) {
        std::cerr << formatted.toStdString() << std::endl;
    } else {
        std::cout << formatted.toStdString() << std::endl;
    }
}

} // namespace Debug
} // namespace Krema
