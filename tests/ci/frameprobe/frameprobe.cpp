// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "frameprobe.h"

#include <QAnimationDriver>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLoggingCategory>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMetaObject>
#include <QMetaProperty>
#include <QThread>
#include <QPoint>
#include <QPointer>
#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QQmlEngine>
#include <QWindow>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QTextStream>
#include <QVariantAnimation>

#include <functional>
#include <memory>

Q_LOGGING_CATEGORY(lcProbe, "krema.testing.frameprobe")

namespace krema::testing
{
namespace
{

/**
 * Animation clock driven by the capture loop instead of the wall clock.
 *
 * Verified against Qt 6 qabstractanimation.h: advance() is the public virtual
 * entry point and the protected advanceAnimation() takes no argument.
 */
class FixedStepDriver : public QAnimationDriver
{
public:
    explicit FixedStepDriver(qint64 stepMs, QObject *parent)
        : QAnimationDriver(parent)
        , m_step(stepMs)
    {
    }

    qint64 elapsed() const override
    {
        return m_virtual;
    }

    void advance() override
    {
        m_virtual += m_step;
        advanceAnimation();
    }

    qint64 virtualTime() const
    {
        return m_virtual;
    }

private:
    qint64 m_step;
    qint64 m_virtual = 0;
};

struct Action {
    int frame = 0;
    QString type;
    QPoint pos;
    QString button;
    QString key;
    QString window; // empty = the window resolved once at settle time
    QString item;   // objectName; when set, x/y are an offset from its centre
    QString name;   // setting name for type == "setting"
    QJsonValue value; // setting value for type == "setting"
};

QString typeName(const QObject *object)
{
    QString name = QString::fromLatin1(object->metaObject()->className());
    // The QML engine appends a per-run registration id (Foo_QMLTYPE_42,
    // Foo_QML_43). The number is not stable across runs, so strip it or the
    // capture stream stops being byte-comparable.
    for (const auto marker : {QLatin1String("_QMLTYPE_"), QLatin1String("_QML_")}) {
        const qsizetype at = name.indexOf(marker);
        if (at > 0) {
            name.truncate(at);
            break;
        }
    }
    return name;
}

QString windowKey(const QQuickWindow *window)
{
    if (!window->objectName().isEmpty()) {
        return window->objectName();
    }
    if (!window->title().isEmpty()) {
        return window->title();
    }
    return typeName(window);
}

void collectItems(QQuickItem *item, const QString &path, QJsonArray &out)
{
    if (!item) {
        return;
    }

    QJsonObject entry;
    entry[QStringLiteral("path")] = path;
    entry[QStringLiteral("type")] = typeName(item);
    if (!item->objectName().isEmpty()) {
        entry[QStringLiteral("name")] = item->objectName();
    }
    entry[QStringLiteral("x")] = item->x();
    entry[QStringLiteral("y")] = item->y();
    entry[QStringLiteral("w")] = item->width();
    entry[QStringLiteral("h")] = item->height();
    entry[QStringLiteral("scale")] = item->scale();
    entry[QStringLiteral("opacity")] = item->opacity();
    entry[QStringLiteral("rotation")] = item->rotation();
    entry[QStringLiteral("visible")] = item->isVisible();

    // QML-declared properties (currentScale, _showAttentionAnim, ...) live past
    // QQuickItem's own metaobject. They carry most of krema's animation state,
    // so a capture limited to base QQuickItem properties would see nothing move.
    const QMetaObject *meta = item->metaObject();
    QJsonObject props;
    for (int i = QQuickItem::staticMetaObject.propertyCount(); i < meta->propertyCount(); ++i) {
        const QMetaProperty property = meta->property(i);
        if (!property.isReadable()) {
            continue;
        }
        const QVariant value = property.read(item);
        switch (value.typeId()) {
        case QMetaType::Bool:
            props[QString::fromLatin1(property.name())] = value.toBool();
            break;
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
            props[QString::fromLatin1(property.name())] = value.toLongLong();
            break;
        case QMetaType::Double:
        case QMetaType::Float:
            props[QString::fromLatin1(property.name())] = value.toDouble();
            break;
        case QMetaType::QString:
            props[QString::fromLatin1(property.name())] = value.toString();
            break;
        default:
            break;
        }
    }
    if (!props.isEmpty()) {
        entry[QStringLiteral("props")] = props;
    }

    out.append(entry);

    const QList<QQuickItem *> children = item->childItems();
    for (qsizetype i = 0; i < children.size(); ++i) {
        collectItems(children.at(i), QStringLiteral("%1/%2").arg(path).arg(i), out);
    }
}

/**
 * The dock context menu is a native QMenu, i.e. a QWidget popup, so it never
 * appears in a QQuickItem walk. Without this a scenario cannot assert what the
 * right-click menu offers -- which is most of what a user does with it.
 */
QJsonValue activeMenu()
{
    auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    if (!popup) {
        // Krema builds the menu with `new QMenu()` and no parent
        // (dockcontextmenu.cpp:43), so on Wayland it has no transientParent and
        // the compositor refuses to map it unless the parent surface has
        // received real input, which synthetic input does not provide
        // ("Failed to create grabbing popup" in the log). The menu object still
        // exists and its actions are live, so fall back to it: composition and
        // action behaviour stay testable; only the mapping does not.
        const QWidgetList widgets = QApplication::topLevelWidgets();
        for (QWidget *widget : widgets) {
            if (auto *candidate = qobject_cast<QMenu *>(widget)) {
                popup = candidate;
                break;
            }
        }
    }
    if (!popup) {
        return QJsonValue();
    }
    QJsonObject out;
    out[QStringLiteral("title")] = popup->title();
    out[QStringLiteral("visible")] = popup->isVisible();
    out[QStringLiteral("mapped")] = popup->windowHandle() != nullptr && popup->windowHandle()->isVisible();
    QJsonArray entries;
    const QList<QAction *> actions = popup->actions();
    for (QAction *action : actions) {
        QJsonObject entry;
        if (action->isSeparator()) {
            entry[QStringLiteral("separator")] = true;
        } else {
            entry[QStringLiteral("text")] = action->text();
            entry[QStringLiteral("enabled")] = action->isEnabled();
            entry[QStringLiteral("visible")] = action->isVisible();
            entry[QStringLiteral("checkable")] = action->isCheckable();
            entry[QStringLiteral("checked")] = action->isChecked();
            entry[QStringLiteral("submenu")] = action->menu() != nullptr;
        }
        entries.append(entry);
    }
    out[QStringLiteral("entries")] = entries;
    return out;
}

QList<QQuickWindow *> quickWindows()
{
    QList<QQuickWindow *> result;
    const QWindowList topLevel = QGuiApplication::topLevelWindows();
    for (QWindow *candidate : topLevel) {
        if (auto *quick = qobject_cast<QQuickWindow *>(candidate)) {
            result.append(quick);
        }
    }
    return result;
}

QList<Action> loadScript(const QString &path)
{
    QList<Action> actions;
    if (path.isEmpty()) {
        return actions;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcProbe) << "cannot open scenario script" << path;
        return actions;
    }
    // A scenario file is either a bare array of actions or an object with an
    // "actions" array plus assertion metadata consumed by assert_frames.py.
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonArray array = document.isArray() ? document.array() : document.object().value(QStringLiteral("actions")).toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        Action action;
        action.frame = object.value(QStringLiteral("frame")).toInt();
        action.type = object.value(QStringLiteral("type")).toString();
        action.pos = QPoint(object.value(QStringLiteral("x")).toInt(), object.value(QStringLiteral("y")).toInt());
        action.button = object.value(QStringLiteral("button")).toString(QStringLiteral("left"));
        action.key = object.value(QStringLiteral("key")).toString();
        action.window = object.value(QStringLiteral("window")).toString();
        action.item = object.value(QStringLiteral("item")).toString();
        action.name = object.value(QStringLiteral("name")).toString();
        action.value = object.value(QStringLiteral("value"));
        actions.append(action);
    }
    return actions;
}

Qt::MouseButton buttonFromName(const QString &name)
{
    if (name == QLatin1String("right")) {
        return Qt::RightButton;
    }
    if (name == QLatin1String("middle")) {
        return Qt::MiddleButton;
    }
    return Qt::LeftButton;
}

QQuickItem *findByName(QQuickItem *item, const QString &name)
{
    if (!item) {
        return nullptr;
    }
    if (item->objectName() == name) {
        return item;
    }
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *hit = findByName(child, name)) {
            return hit;
        }
    }
    return nullptr;
}

/**
 * Resolve an action's window coordinates. Naming an item keeps a scenario
 * valid when the icon size or panel width changes; a bare x/y would silently
 * start landing in the gap between items instead.
 */
bool resolvePos(QQuickWindow *window, const Action &action, QPoint &out)
{
    if (action.item.isEmpty()) {
        out = action.pos;
        return true;
    }
    QQuickItem *target = findByName(window->contentItem(), action.item);
    if (!target) {
        qCWarning(lcProbe) << "scenario frame" << action.frame << "targets unknown item" << action.item;
        return false;
    }
    const QPointF centre = target->mapToScene(QPointF(target->width() / 2, target->height() / 2));
    out = QPoint(qRound(centre.x()) + action.pos.x(), qRound(centre.y()) + action.pos.y());
    return true;
}

void applyAction(QQuickWindow *window, const Action &action)
{
    QPoint pos;
    if (!resolvePos(window, action, pos)) {
        return;
    }
    if (action.type == QLatin1String("move")) {
        QTest::mouseMove(window, pos);
    } else if (action.type == QLatin1String("click")) {
        QTest::mouseClick(window, buttonFromName(action.button), Qt::NoModifier, pos);
    } else if (action.type == QLatin1String("press")) {
        QTest::mousePress(window, buttonFromName(action.button), Qt::NoModifier, pos);
    } else if (action.type == QLatin1String("release")) {
        QTest::mouseRelease(window, buttonFromName(action.button), Qt::NoModifier, pos);
    } else if (action.type == QLatin1String("key")) {
        const QKeySequence sequence(action.key);
        if (sequence.count() > 0) {
            const QKeyCombination combination = sequence[0];
            QTest::keyClick(window, combination.key(), combination.keyboardModifiers());
        }
    } else if (action.type == QLatin1String("leave")) {
        // Moving the synthetic pointer to a corner is not enough: the point is
        // still inside the dock window, so Qt keeps reporting the pointer as
        // inside and hover-driven auto-hide never fires. A real pointer leave
        // comes from the compositor, which synthetic input bypasses; sending
        // QEvent::Leave is the public equivalent.
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(window, &leave);
    } else if (action.type == QLatin1String("menuitem")) {
        // The context menu is a native QMenu, so a synthetic click on the Quick
        // window cannot reach it. Triggering the QAction by its visible label
        // is what the user's click ends up doing.
        auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!popup) {
            // Same reason as activeMenu(): the popup may exist without being
            // mapped by the compositor.
            const QWidgetList widgets = QApplication::topLevelWidgets();
            for (QWidget *widget : widgets) {
                if (auto *candidate = qobject_cast<QMenu *>(widget)) {
                    popup = candidate;
                    break;
                }
            }
        }
        if (!popup) {
            qCWarning(lcProbe) << "scenario frame" << action.frame << "has no context menu";
            return;
        }
        QAction *match = nullptr;
        QStringList labels;
        const QList<QAction *> entries = popup->actions();
        for (QAction *entry : entries) {
            if (entry->isSeparator()) {
                continue;
            }
            labels << entry->text();
            if (entry->text() == action.name) {
                match = entry;
            }
        }
        if (!match) {
            qCWarning(lcProbe) << "scenario frame" << action.frame << "menu has no entry" << action.name << "; has" << labels;
            return;
        }
        popup->close();
        match->trigger();
    } else if (action.type == QLatin1String("shortcut")) {
        // Krema's keyboard navigation is only reachable through the global
        // shortcut (focus-dock), and KGlobalAccel key delivery does not work in
        // this session. Triggering the registered QAction by name runs the
        // exact slot the shortcut fires. It does not cover the key-sequence to
        // action binding, which KGlobalAccel owns and is untestable here.
        const QList<QAction *> actions = qApp->findChildren<QAction *>();
        QAction *match = nullptr;
        for (QAction *candidate : actions) {
            if (candidate->objectName() == action.name) {
                match = candidate;
                break;
            }
        }
        if (!match) {
            QStringList names;
            for (QAction *candidate : actions) {
                if (!candidate->objectName().isEmpty()) {
                    names << candidate->objectName();
                }
            }
            qCWarning(lcProbe) << "scenario frame" << action.frame << "unknown shortcut" << action.name << "; registered:" << names;
            return;
        }
        match->trigger();
    } else if (action.type == QLatin1String("setting")) {
        // krema has no KConfigWatcher, so rewriting kremarc at runtime does
        // nothing. Driving the generated KConfigXT property instead follows the
        // real path a settings change takes: mutator -> *Changed signal -> QML
        // rebinding, which is exactly what "applies immediately" means.
        QQmlEngine *engine = qmlEngine(window->contentItem());
        QObject *settings = engine ? engine->singletonInstance<QObject *>(QStringLiteral("com.bhyoo.krema"), QStringLiteral("DockSettings")) : nullptr;
        if (!settings) {
            qCWarning(lcProbe) << "scenario frame" << action.frame << "cannot resolve the DockSettings singleton";
            return;
        }
        if (!settings->setProperty(action.name.toUtf8().constData(), action.value.toVariant())) {
            qCWarning(lcProbe) << "scenario frame" << action.frame << "unknown setting" << action.name;
        }
    } else {
        qCWarning(lcProbe) << "unknown scenario action" << action.type;
    }
}

} // namespace

void FrameProbe::installIfEnabled()
{
    const QString ndjsonPath = QString::fromLocal8Bit(qgetenv("KREMA_PROBE_NDJSON"));
    if (ndjsonPath.isEmpty()) {
        return;
    }

    const QString frameDir = QString::fromLocal8Bit(qgetenv("KREMA_PROBE_DIR"));
    const qint64 stepMs = qEnvironmentVariableIsSet("KREMA_PROBE_STEP_MS") ? qEnvironmentVariableIntValue("KREMA_PROBE_STEP_MS") : 16;
    const int maxFrames = qEnvironmentVariableIsSet("KREMA_PROBE_MAX_FRAMES") ? qEnvironmentVariableIntValue("KREMA_PROBE_MAX_FRAMES") : 600;
    const int settleFrames = qEnvironmentVariableIsSet("KREMA_PROBE_SETTLE_FRAMES") ? qEnvironmentVariableIntValue("KREMA_PROBE_SETTLE_FRAMES") : 30;
    // Frames alone are not enough to settle: with a virtual clock the loop
    // renders as fast as it can, so 40 frames can pass in a few milliseconds of
    // real time while Wayland and D-Bus state (window rows, virtual-desktop
    // info) is still arriving. Without a wall-clock floor those arrivals land
    // on an arbitrary frame and every early assertion becomes a race.
    const int settleMs = qEnvironmentVariableIsSet("KREMA_PROBE_SETTLE_MS") ? qEnvironmentVariableIntValue("KREMA_PROBE_SETTLE_MS") : 1500;
    const QString scriptPath = QString::fromLocal8Bit(qgetenv("KREMA_PROBE_SCRIPT"));
    // QTimer / QML Timer are NOT driven by the animation driver. Pacing the
    // frame loop so each tick consumes stepMs of wall clock keeps timer-gated
    // state (tooltip delay, preview hover delay, drag hold) aligned with the
    // virtual clock, at the cost of the run taking real time.
    const bool paced = qEnvironmentVariableIntValue("KREMA_PROBE_PACE") != 0;

    if (!frameDir.isEmpty()) {
        QDir().mkpath(frameDir);
    }

    auto *app = QCoreApplication::instance();
    auto *driver = new FixedStepDriver(stepMs, app);
    // Installed later, from the first tick: the Qt Quick render loop installs
    // its own QSGAnimationDriver when the window is created, and the last
    // driver installed wins. Installing here would be silently overridden, and
    // animations would then advance per rendered frame instead of per captured
    // frame -- which looks correct only while something forces a render every
    // tick (grabWindow did, until screenshots became opt-in).

    auto *out = new QFile(ndjsonPath, app);
    if (!out->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(lcProbe) << "cannot write" << ndjsonPath;
        return;
    }
    auto *stream = new QTextStream(out);

    const QList<Action> script = loadScript(scriptPath);

    auto *owner = new QObject(app);
    auto frame = std::make_shared<int>(0);
    auto settled = std::make_shared<int>(0);
    // Resolved once, after settle: krema opens a second layer-shell surface for
    // the preview popup, so "first visible window" would otherwise flip
    // mid-scenario and send later actions to the wrong surface.
    auto target = std::make_shared<QPointer<QQuickWindow>>();
    auto tick = std::make_shared<std::function<void()>>();
    auto lastMenu = std::make_shared<QJsonValue>();
    auto installed = std::make_shared<bool>(false);
    auto warmed = std::make_shared<bool>(false);
    auto pace = std::make_shared<QElapsedTimer>();
    pace->start();

    *tick = [=]() {
        const QList<QQuickWindow *> windows = quickWindows();

        if (target->isNull()) {
            for (QQuickWindow *candidate : windows) {
                if (candidate->isVisible()) {
                    *target = candidate;
                    break;
                }
            }
        }
        QQuickWindow *window = target->data();
        if (!window) {
            QMetaObject::invokeMethod(owner, [tick]() { (*tick)(); }, Qt::QueuedConnection);
            return;
        }

        // Let the surface configure and the initial layout settle before the
        // recorded scenario starts, so a frame number means the same thing on
        // every run regardless of compositor handshake timing.
        if (!*installed) {
            driver->install();
            // QUnifiedTimer only refreshes its internal lastTick while at least
            // one animation is running. Without a permanently running animation
            // the first animation started after an idle gap is charged the
            // entire elapsed virtual time as a single delta and completes in
            // one frame instead of animating.
            auto *keepAlive = new QVariantAnimation(app);
            keepAlive->setStartValue(0.0);
            keepAlive->setEndValue(1.0);
            keepAlive->setDuration(1000);
            keepAlive->setLoopCount(-1);
            keepAlive->start();
            *installed = true;
        }

        if (*settled < settleFrames) {
            ++*settled;
            driver->advance();
            window->update();
            QMetaObject::invokeMethod(owner, [tick]() { (*tick)(); }, Qt::QueuedConnection);
            return;
        }
        if (!*warmed) {
            // Wall clock alone is not a deterministic settle condition: async
            // Wayland/D-Bus state lands at a different point relative to it on
            // every run, which shows up as a whole-frame phase difference in
            // the capture. Additionally require the layout to have stopped
            // changing before frame 1.
            QString signature;
            int stable = 0;
            while (stable < 20 || pace->elapsed() < settleMs) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                QThread::msleep(5);
                QString current = QString::number(windows.size());
                for (QQuickWindow *quick : quickWindows()) {
                    QJsonArray items;
                    collectItems(quick->contentItem(), QStringLiteral("0"), items);
                    current += QStringLiteral("|%1:%2x%3").arg(items.size()).arg(quick->width()).arg(quick->height());
                }
                stable = (current == signature) ? stable + 1 : 0;
                signature = current;
            }
            // Wait out the remaining wall clock WITHOUT advancing the driver.
            // Burning virtual time here instead would push the animation clock
            // hundreds of thousands of frames ahead, and QUnifiedTimer then
            // stops delivering a tick per advance -- animations end up updating
            // once every ~10 captured frames instead of every frame.
            *warmed = true;
            pace->restart();
        }

        ++*frame;

        // grabWindow() renders the scene synchronously, so the pixels and the
        // JSON row below always describe the same animation instant -- and,
        // critically, it is what guarantees exactly one render per captured
        // frame. Skipping it when screenshots are off would let the compositor
        // pace rendering instead, and animation state would then advance once
        // every few captured frames. Always grab; only PNG encoding is gated,
        // which is the expensive part.
        const QImage image = window->grabWindow();

        QJsonArray windowRows;
        for (QQuickWindow *quick : windows) {
            QJsonObject entry;
            entry[QStringLiteral("key")] = windowKey(quick);
            entry[QStringLiteral("target")] = quick == window;
            entry[QStringLiteral("visible")] = quick->isVisible();
            entry[QStringLiteral("w")] = quick->width();
            entry[QStringLiteral("h")] = quick->height();
            QJsonArray items;
            collectItems(quick->contentItem(), QStringLiteral("0"), items);
            entry[QStringLiteral("items")] = items;
            windowRows.append(entry);
        }

        QJsonObject event;
        event[QStringLiteral("frame")] = *frame;
        event[QStringLiteral("vt")] = driver->virtualTime();
        event[QStringLiteral("windows")] = windowRows;
        if (!lastMenu->isNull()) {
            event[QStringLiteral("menu")] = *lastMenu;
        }
        if (!image.isNull()) {
            image.save(QStringLiteral("%1/f%2.png").arg(frameDir).arg(*frame, 5, 10, QLatin1Char('0')));
        }
        *stream << QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact)) << "\n";
        stream->flush();

        for (const Action &action : script) {
            if (action.frame != *frame) {
                continue;
            }
            QQuickWindow *destination = window;
            if (!action.window.isEmpty()) {
                destination = nullptr;
                for (QQuickWindow *quick : windows) {
                    if (windowKey(quick) == action.window) {
                        destination = quick;
                        break;
                    }
                }
                if (!destination) {
                    qCWarning(lcProbe) << "scenario frame" << action.frame << "targets unknown window" << action.window;
                    continue;
                }
            }
            applyAction(destination, action);

            // The menu must be sampled here, not at the next frame's write:
            // krema's QMenu is WA_DeleteOnClose and the Wayland popup creation
            // fails (no transientParent), so Qt closes and deletes it within
            // this same event-loop turn. The snapshot then stays until the menu
            // is dismissed or an entry is activated.
            if (action.type == QLatin1String("click") && action.button == QLatin1String("right")) {
                *lastMenu = activeMenu();
            } else if (action.type == QLatin1String("menuitem")
                       || (action.type == QLatin1String("key") && action.key == QLatin1String("Esc"))) {
                *lastMenu = QJsonValue();
            }
        }

        driver->advance();

        if (paced) {
            const qint64 due = static_cast<qint64>(*frame) * stepMs;
            const qint64 behind = due - pace->elapsed();
            if (behind > 0) {
                QThread::msleep(static_cast<unsigned long>(behind));
            }
        }

        if (maxFrames > 0 && *frame >= maxFrames) {
            stream->flush();
            out->close();
            QCoreApplication::quit();
            return;
        }
        window->update();
        QMetaObject::invokeMethod(owner, [tick]() { (*tick)(); }, Qt::QueuedConnection);
    };

    QMetaObject::invokeMethod(owner, [tick]() { (*tick)(); }, Qt::QueuedConnection);
    qCInfo(lcProbe) << "frame probe active:" << ndjsonPath << "step" << stepMs << "ms, max" << maxFrames << "frames";
}

} // namespace krema::testing
