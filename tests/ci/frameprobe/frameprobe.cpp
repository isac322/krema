// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "frameprobe.h"
#include "qwayland-fake-input.h"

#include <QAbstractEventDispatcher>
#include <QAction>
#include <QAnimationDriver>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLoggingCategory>
#include <QMargins>
#include <QMenu>
#include <QMetaObject>
#include <QMetaProperty>
#include <QPainter>
#include <QPoint>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QTest>
#include <QTextStream>
#include <QThread>
#include <QTimerEvent>
#include <QVariantAnimation>
#include <QWidget>
#include <QWindow>
#include <private/qabstractanimation_p.h>
#include <private/qgenericunixeventdispatcher_p.h>

#include <QtWaylandClient/QWaylandClientExtension>

#include <LayerShellQt/Window>
#include <QtGui/qguiapplication_platform.h>
#include <linux/input-event-codes.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

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

/**
 * Main-thread event dispatcher that puts timers on the probe's virtual clock.
 *
 * QTimer, QML Timer and QBasicTimer are delivered by the event dispatcher from
 * the wall clock, not by the animation driver. Left alone, a tooltip delay or
 * an auto-hide debounce fires on whichever captured frame the machine happened
 * to reach, so two passes of one scenario disagree at action boundaries.
 *
 * Everything except timers goes to the dispatcher QtWayland itself would have
 * created (createUnixEventDispatcher), so sockets, posted events and
 * window-system events flow as in production. It is created on first use,
 * which happens inside QApplication construction, so GLib binds to the default
 * main context exactly as it does without the probe.
 *
 * Until setVirtual() the wrapper only records timers. Startup and the settle
 * phase therefore run on real time; from frame 1 every positive-interval timer
 * fires only from advance(), at the virtual instant it is due. Zero-interval
 * timers mean "when idle" rather than a delay and stay with the platform.
 */
class VirtualTimerDispatcher final : public QAbstractEventDispatcherV2
{
public:
    bool processEvents(QEventLoop::ProcessEventsFlags flags) override
    {
        return platform()->processEvents(flags);
    }

    void registerSocketNotifier(QSocketNotifier *notifier) override
    {
        platform()->registerSocketNotifier(notifier);
    }

    void unregisterSocketNotifier(QSocketNotifier *notifier) override
    {
        platform()->unregisterSocketNotifier(notifier);
    }

    void registerTimer(Qt::TimerId id, Duration interval, Qt::TimerType type, QObject *object) override
    {
        if (interval <= Duration::zero()) {
            platform()->registerTimer(id, interval, type, object);
        } else if (m_virtual) {
            m_virtualTimers.push_back({id, interval, type, object, m_now + interval, m_sequence++});
        } else {
            m_realTimers.push_back({id, interval, type, object, {}, 0});
            platform()->registerTimer(id, interval, type, object);
        }
    }

    bool unregisterTimer(Qt::TimerId id) override
    {
        if (std::erase_if(m_virtualTimers,
                          [id](const Timer &timer) {
                              return timer.id == id;
                          })
            > 0) {
            return true;
        }
        std::erase_if(m_realTimers, [id](const Timer &timer) {
            return timer.id == id;
        });
        return platform()->unregisterTimer(id);
    }

    bool unregisterTimers(QObject *object) override
    {
        const auto owned = [object](const Timer &timer) {
            return timer.object == object;
        };
        const bool removed = std::erase_if(m_virtualTimers, owned) > 0;
        std::erase_if(m_realTimers, owned);
        return platform()->unregisterTimers(object) || removed;
    }

    QList<TimerInfoV2> timersForObject(QObject *object) const override
    {
        QList<TimerInfoV2> result = platform()->timersForObject(object);
        for (const Timer &timer : m_virtualTimers) {
            if (timer.object == object) {
                result.append({timer.interval, timer.id, timer.type});
            }
        }
        return result;
    }

    Duration remainingTime(Qt::TimerId id) const override
    {
        for (const Timer &timer : m_virtualTimers) {
            if (timer.id == id) {
                return std::max(timer.due - m_now, Duration::zero());
            }
        }
        return platform()->remainingTime(id);
    }

    void wakeUp() override
    {
        platform()->wakeUp();
    }

    void interrupt() override
    {
        platform()->interrupt();
    }

    void startingUp() override
    {
        platform()->startingUp();
    }

    void closingDown() override
    {
        platform()->closingDown();
    }

    /**
     * Move every pending wall-clock timer onto the virtual clock.
     *
     * A migrated timer restarts with its full interval: its remaining real
     * time depends on how long startup took, and keeping it would put the
     * firing frame right back at the mercy of machine speed.
     */
    void setVirtual()
    {
        if (m_virtual) {
            return;
        }
        m_virtual = true;
        for (const Timer &timer : std::exchange(m_realTimers, {})) {
            platform()->unregisterTimer(timer.id);
            m_virtualTimers.push_back({timer.id, timer.interval, timer.type, timer.object, m_now + timer.interval, m_sequence++});
        }
    }

    /**
     * Advance the virtual clock by one step, firing every timer that falls due
     * in order of due time, then registration order, and return how many
     * fired. A repeating timer due several times within one step fires each
     * time, as it would in real time. Single-shot owners (QTimer,
     * QSingleShotTimer) unregister themselves from their timerEvent.
     */
    int advance(Duration step)
    {
        const Duration target = m_now + step;
        int fired = 0;
        for (;;) {
            const auto next = std::min_element(m_virtualTimers.begin(), m_virtualTimers.end(), [](const Timer &left, const Timer &right) {
                return std::tie(left.due, left.sequence) < std::tie(right.due, right.sequence);
            });
            if (next == m_virtualTimers.end() || next->due > target) {
                break;
            }
            m_now = std::max(m_now, next->due);
            const Qt::TimerId id = next->id;
            QObject *object = next->object;
            next->due += next->interval;
            next->sequence = m_sequence++;
            ++fired;
            // The handler may unregister or re-register timers; nothing from
            // the vector is touched after this call.
            QTimerEvent event(id);
            QCoreApplication::sendEvent(object, &event);
        }
        m_now = target;
        return fired;
    }

private:
    struct Timer {
        Qt::TimerId id;
        Duration interval;
        Qt::TimerType type;
        QObject *object;
        Duration due;
        quint64 sequence;
    };

    QAbstractEventDispatcher *platform() const
    {
        if (!m_platform) {
            m_platform = QtGenericUnixDispatcher::createUnixEventDispatcher();
            m_platform->setParent(const_cast<VirtualTimerDispatcher *>(this));
            connect(m_platform, &QAbstractEventDispatcher::aboutToBlock, this, &QAbstractEventDispatcher::aboutToBlock);
            connect(m_platform, &QAbstractEventDispatcher::awake, this, &QAbstractEventDispatcher::awake);
        }
        return m_platform;
    }

    mutable QAbstractEventDispatcher *m_platform = nullptr;
    std::vector<Timer> m_realTimers;
    std::vector<Timer> m_virtualTimers;
    Duration m_now{};
    quint64 m_sequence = 0;
    bool m_virtual = false;
};

VirtualTimerDispatcher *g_timers = nullptr;

class NativeFakeInput final
    : public QWaylandClientExtensionTemplate<NativeFakeInput>
    , public QtWayland::org_kde_kwin_fake_input
{
public:
    NativeFakeInput()
        : QWaylandClientExtensionTemplate<NativeFakeInput>(6)
    {
    }

    bool moveTo(const QPointF &globalPosition)
    {
        if (!ensureAuthenticated()) {
            return false;
        }
        pointer_motion_absolute(
            wl_fixed_from_double(globalPosition.x()),
            wl_fixed_from_double(globalPosition.y()));
        return true;
    }


    bool sendButton(Qt::MouseButton button, bool pressed)
    {
        if (!ensureAuthenticated()) {
            return false;
        }
        uint32_t nativeButton = BTN_LEFT;
        if (button == Qt::RightButton) {
            nativeButton = BTN_RIGHT;
        } else if (button == Qt::MiddleButton) {
            nativeButton = BTN_MIDDLE;
        }
        QtWayland::org_kde_kwin_fake_input::button(
            nativeButton,
            pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
        return true;
    }

private:
    bool ensureAuthenticated()
    {
        if (!isActive()) {
            return false;
        }
        if (!m_authenticated) {
            QtWayland::org_kde_kwin_fake_input::authenticate(
                QStringLiteral("Krema frame probe"),
                QStringLiteral("Deterministic native input for CI"));
            m_authenticated = true;
        }
        return true;
    }

    bool m_authenticated = false;
};


struct Action {
    int frame = 0;
    QString type;
    bool native = false; // deliver through KWin so Wayland supplies a real input serial
    bool waitForMenu = false;
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
    // The rect this item actually occupies in the window, with every transform
    // on it and its ancestors applied. Summing x/y up the parent chain would
    // miss scale and rotation, and krema's zoom is precisely a scale effect --
    // a checker cropping the wrong rectangle would fail and pass the wrong
    // things.
    const QRectF scene = item->mapRectToScene(item->boundingRect());
    entry[QStringLiteral("sx")] = scene.x();
    entry[QStringLiteral("sy")] = scene.y();
    entry[QStringLiteral("sw")] = scene.width();
    entry[QStringLiteral("sh")] = scene.height();

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

QMenu *visibleMenu()
{
    if (auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
        return popup;
    }
    const QWidgetList widgets = QApplication::topLevelWidgets();
    for (QWidget *widget : widgets) {
        if (auto *candidate = qobject_cast<QMenu *>(widget);
            candidate && candidate->isVisible()) {
            return candidate;
        }
    }
    return nullptr;
}

QPoint requestedTopLeft(QWindow *window, const QSize &output);

QPoint popupTopLeft(const QPoint &anchor, const QSize &size, const QSize &output)
{
    const int maxX = qMax(0, output.width() - size.width());
    const int maxY = qMax(0, output.height() - size.height());
    const int x = qBound(0, anchor.x(), maxX);
    const int preferredY = anchor.y() + size.height() <= output.height()
        ? anchor.y()
        : anchor.y() - size.height();
    return {x, qBound(0, preferredY, maxY)};
}

void waitForMappedMenu(int frame, const QPoint &anchor)
{
    QElapsedTimer timeout;
    timeout.start();
    while (timeout.elapsed() < 2000) {
        if (QMenu *popup = visibleMenu()) {
            QWindow *handle = popup->windowHandle();
            if (handle && handle->isVisible() && handle->isExposed()) {
                const QSize output = handle->screen()
                    ? handle->screen()->geometry().size()
                    : popup->size();
                // Wayland does not expose a popup's compositor-chosen global
                // position. Retain the xdg-popup request so capture and NDJSON
                // place the mapped menu where KWin constrains it.
                handle->setProperty(
                    "_kremaProbeTopLeft",
                    popupTopLeft(anchor, popup->size(), output));
                return;
            }
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    qFatal("scenario frame %d timed out waiting for a mapped context menu", frame);
}

void waitForMenuClosed(int frame)
{
    QElapsedTimer timeout;
    timeout.start();
    while (timeout.elapsed() < 2000) {
        if (!visibleMenu()) {
            return;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    qFatal("scenario frame %d timed out waiting for the context menu to close", frame);
}

/**
 * The dock context menu is a native QMenu, i.e. a QWidget popup, so it never
 * appears in a QQuickItem walk. Record both its actions and its mapped window
 * state; a constructed-but-unmapped menu is a user-visible failure.
 */
QJsonValue activeMenu()
{
    QMenu *popup = visibleMenu();
    if (!popup) {
        return QJsonValue();
    }

    QJsonObject out;
    out[QStringLiteral("title")] = popup->title();
    out[QStringLiteral("visible")] = popup->isVisible();
    QWindow *handle = popup->windowHandle();
    out[QStringLiteral("mapped")] =
        handle != nullptr && handle->isVisible() && handle->isExposed();
    const QRect geometry = handle ? handle->geometry() : popup->geometry();
    const QSize output = handle && handle->screen()
        ? handle->screen()->geometry().size()
        : geometry.size();
    const QPoint topLeft = handle ? requestedTopLeft(handle, output) : geometry.topLeft();
    out[QStringLiteral("x")] = topLeft.x();
    out[QStringLiteral("y")] = topLeft.y();
    out[QStringLiteral("w")] = geometry.width();
    out[QStringLiteral("h")] = geometry.height();

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

/**
 * Where a window sits on the output, as the client asked for it.
 *
 * Wayland never tells a client its global position, so QWindow::geometry()
 * reports (0, 0) for the dock and compositing there would draw a bottom
 * anchored dock at the top-left -- a recording that looks authoritative and is
 * wrong about the one thing the edge scenarios are about. The layer-shell
 * anchors and margins are client-known, so derive the rect from those. This is
 * the requested placement, not a confirmation of what the compositor did.
 */
QPoint requestedTopLeft(QWindow *window, const QSize &output)
{
    const QVariant popupPosition = window->property("_kremaProbeTopLeft");
    if (popupPosition.isValid()) {
        return popupPosition.toPoint();
    }
    auto *layer = LayerShellQt::Window::get(window);
    if (!layer) {
        if (QWindow *parent = window->transientParent()) {
            return requestedTopLeft(parent, output) + window->geometry().topLeft();
        }
        return window->geometry().topLeft();
    }
    const auto anchors = layer->anchors();
    const QMargins margins = layer->margins();
    const QSize size = window->size();

    int x = (output.width() - size.width()) / 2;
    if (anchors.testFlag(LayerShellQt::Window::AnchorLeft)) {
        x = margins.left();
    } else if (anchors.testFlag(LayerShellQt::Window::AnchorRight)) {
        x = output.width() - size.width() - margins.right();
    }

    int y = (output.height() - size.height()) / 2;
    if (anchors.testFlag(LayerShellQt::Window::AnchorTop)) {
        y = margins.top();
    } else if (anchors.testFlag(LayerShellQt::Window::AnchorBottom)) {
        y = output.height() - size.height() - margins.bottom();
    }
    return {x, y};
}

/**
 * Capture everything the user would see, not just the dock.
 *
 * The settings dialog is a separate top-level window, so grabbing only the dock
 * would leave it out of the frame entirely and a scenario could assert it
 * opened while the recording showed nothing. Windows the compositor has not
 * mapped are skipped: an unmapped QMenu has no laid-out contents, and drawing
 * its placeholder would imply the menu appeared when it did not.
 */
QImage captureScreen(QQuickWindow *primary)
{
    QScreen *screen = primary->screen();
    const QSize output = screen ? screen->geometry().size() : primary->size();
    QImage canvas(output, QImage::Format_RGB32);
    canvas.fill(QColor(18, 18, 18));

    QPainter painter(&canvas);
    const QWindowList windows = QGuiApplication::topLevelWindows();
    for (QWindow *window : windows) {
        if (!window->isVisible() || !window->isExposed()) {
            continue;
        }
        QImage image;
        if (auto *quick = qobject_cast<QQuickWindow *>(window)) {
            image = quick->grabWindow();
        } else if (QWidget *widget = QWidget::find(window->winId())) {
            image = widget->grab().toImage();
        }
        if (!image.isNull()) {
            painter.drawImage(requestedTopLeft(window, output), image);
        }
    }
    return canvas;
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
        action.native = object.value(QStringLiteral("native")).toBool();
        action.waitForMenu = object.value(QStringLiteral("wait_for_menu")).toBool();
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
bool resolvePos(QQuickWindow *window, const Action &action, QPoint &out,
                QQuickItem **resolvedTarget = nullptr)
{
    if (resolvedTarget) {
        *resolvedTarget = nullptr;
    }
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
    if (resolvedTarget) {
        *resolvedTarget = target;
    }
    return true;
}

void waitForPointerAt(QQuickItem *target, int frame)
{
    if (!target || !target->property("panelMouseInside").isValid()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        return;
    }
    QElapsedTimer timeout;
    timeout.start();
    while (timeout.elapsed() < 2000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (target->property("panelMouseInside").toBool()) {
            return;
        }
        QThread::msleep(1);
    }
    qFatal("scenario frame %d timed out waiting for the pointer target", frame);
}

void appendHoverState(const QQuickItem *item, QString &state)
{
    for (const char *name : {"containsMouse", "panelMouseInside"}) {
        const QVariant value = item->property(name);
        if (value.isValid()) {
            state += value.toBool() ? QLatin1Char('1') : QLatin1Char('0');
        }
    }
    const QList<QQuickItem *> children = item->childItems();
    for (const QQuickItem *child : children) {
        appendHoverState(child, state);
    }
}

/// What compositor round trips can change: surfaces, their size and exposure, and hover.
QString compositorState()
{
    QString state;
    const QWindowList windows = QGuiApplication::topLevelWindows();
    for (const QWindow *window : windows) {
        state += QStringLiteral("%1:%2%3:%4x%5;")
                     .arg(QString::fromLatin1(window->metaObject()->className()))
                     .arg(int(window->isVisible()))
                     .arg(int(window->isExposed()))
                     .arg(window->width())
                     .arg(window->height());
        if (const auto *quick = qobject_cast<const QQuickWindow *>(window)) {
            appendHoverState(quick->contentItem(), state);
        }
    }
    return state;
}

/**
 * Let every compositor response to what the probe just did arrive before the
 * animation clock moves.
 *
 * A layer-shell configure after an edge change, the pointer leave when a popup
 * grabs, or a new surface being mapped otherwise reach the client whenever
 * KWin gets to them, so the resulting relayout or hover change landed a frame
 * early or late between passes. Each round is a wl_display round trip, so KWin
 * has processed every request sent so far, followed by event processing. Four
 * consecutive rounds with an unchanged surface/hover state end the wait; the
 * animation driver and virtual timers are frozen meanwhile, so only compositor
 * events can change that state. The two-second bound guards a compositor that
 * never settles.
 */
void syncWithCompositor()
{
    auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    wl_display *display = wayland ? wayland->display() : nullptr;
    QString previous = compositorState();
    int stable = 0;
    QElapsedTimer elapsed;
    elapsed.start();
    while (stable < 4) {
        if (elapsed.elapsed() >= 2000) {
            qCWarning(lcProbe) << "compositor state did not settle within 2 s";
            return;
        }
        if (display) {
            wl_display_roundtrip(display);
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        QThread::msleep(4);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        const QString current = compositorState();
        stable = current == previous ? stable + 1 : 0;
        previous = current;
    }
}

QPointF neutralPointerPosition(const QQuickWindow *window)
{
    const QSize output = window->screen() ? window->screen()->geometry().size() : window->size();
    return QPointF(output.width() / 2.0, output.height() / 2.0);
}

// Where the compositor's pointer is after the last native action, and the
// item it is over. QTest clicks on a QMenu never move it.
QPoint g_pointerGlobal;
QPointer<QQuickItem> g_pointerTarget;

QPoint moveNativePointer(QQuickWindow *window, const QPoint &pos, QQuickItem *target, NativeFakeInput *fakeInput, int frame)
{
    const QSize output = window->screen() ? window->screen()->geometry().size() : window->size();
    const QPoint globalPosition = requestedTopLeft(window, output) + pos;
    if (!fakeInput->moveTo(globalPosition)) {
        qFatal("scenario frame %d requires KWin fake-input support", frame);
    }
    waitForPointerAt(target, frame);
    g_pointerGlobal = globalPosition;
    g_pointerTarget = target;
    return globalPosition;
}

/**
 * Put the pointer back over the item it right-clicked once a popup closes.
 *
 * The real pointer never left that spot, but KWin re-enters the dock under it
 * only when it next updates pointer focus, on a schedule of its own, so the
 * zoom-in started a frame early or late between passes. Moving there (via a
 * one-pixel step, since a zero-length motion may not update focus) and
 * waiting for the dock to report it makes the re-entry part of this frame.
 */
void returnPointerAfterPopup(NativeFakeInput *fakeInput, int frame)
{
    if (!g_pointerTarget) {
        return;
    }
    if (!fakeInput->moveTo(g_pointerGlobal + QPoint(1, 0)) || !fakeInput->moveTo(g_pointerGlobal)) {
        qFatal("scenario frame %d requires KWin fake-input support", frame);
    }
    waitForPointerAt(g_pointerTarget, frame);
}

void applyAction(QQuickWindow *window, const Action &action, NativeFakeInput *fakeInput)
{
    QPoint pos;
    QQuickItem *target = nullptr;
    if (!resolvePos(window, action, pos, &target)) {
        return;
    }
    if (action.type == QLatin1String("move")) {
        if (action.native) {
            moveNativePointer(window, pos, target, fakeInput, action.frame);
        } else {
            QTest::mouseMove(window, pos);
        }
    } else if (action.type == QLatin1String("click")) {
        const Qt::MouseButton button = buttonFromName(action.button);
        if (action.native) {
            const QPoint globalPosition = moveNativePointer(window, pos, target, fakeInput, action.frame);
            if (!fakeInput->sendButton(button, true) || !fakeInput->sendButton(button, false)) {
                qFatal("scenario frame %d requires KWin fake-input support", action.frame);
            }
            if (action.waitForMenu) {
                waitForMappedMenu(action.frame, globalPosition);
            }
        } else {
            QTest::mouseClick(window, button, Qt::NoModifier, pos);
        }
    } else if (action.type == QLatin1String("press")) {
        if (action.native) {
            moveNativePointer(window, pos, target, fakeInput, action.frame);
            if (!fakeInput->sendButton(buttonFromName(action.button), true)) {
                qFatal("scenario frame %d requires KWin fake-input support", action.frame);
            }
        } else {
            QTest::mousePress(window, buttonFromName(action.button), Qt::NoModifier, pos);
        }
    } else if (action.type == QLatin1String("release")) {
        if (action.native) {
            moveNativePointer(window, pos, target, fakeInput, action.frame);
            if (!fakeInput->sendButton(buttonFromName(action.button), false)) {
                qFatal("scenario frame %d requires KWin fake-input support", action.frame);
            }
        } else {
            QTest::mouseRelease(window, buttonFromName(action.button), Qt::NoModifier, pos);
        }
    } else if (action.type == QLatin1String("key")) {
        const QKeySequence sequence(action.key);
        if (sequence.count() > 0) {
            const QKeyCombination combination = sequence[0];
            if (combination.key() == Qt::Key_Escape) {
                if (QMenu *popup = visibleMenu()) {
                    QTest::keyClick(popup, combination.key(), combination.keyboardModifiers());
                    waitForMenuClosed(action.frame);
                    returnPointerAfterPopup(fakeInput, action.frame);
                    return;
                }
            }
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
        // The popup itself is already proven mapped through KWin. QTest now
        // exercises QMenu's normal action hit-testing deterministically; the
        // product slot and resulting model/config changes remain the real ones.
        QMenu *popup = visibleMenu();
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
        QTest::mouseClick(
            popup, Qt::LeftButton, Qt::NoModifier,
            popup->actionGeometry(match).center());
        waitForMenuClosed(action.frame);
        returnPointerAfterPopup(fakeInput, action.frame);
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

void FrameProbe::installEventDispatcherIfEnabled()
{
    if (qEnvironmentVariableIsEmpty("KREMA_PROBE_NDJSON")) {
        return;
    }
    // QCoreApplication adopts a dispatcher set before it exists instead of
    // creating its own, so every main-thread timer, including ones started
    // during startup, is registered here.
    g_timers = new VirtualTimerDispatcher;
    QCoreApplication::setEventDispatcher(g_timers);
}

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
    // QTimer / QML Timer are not driven by the animation driver; the
    // dispatcher installed before QApplication puts them on the same virtual
    // clock (see VirtualTimerDispatcher).
    VirtualTimerDispatcher *timers = g_timers;
    if (!timers) {
        qFatal("frame probe needs FrameProbe::installEventDispatcherIfEnabled() before QApplication");
    }

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
    auto *fakeInput = new NativeFakeInput;
    fakeInput->setParent(app);
    auto frame = std::make_shared<int>(0);
    auto settled = std::make_shared<int>(0);
    // Resolved once, after settle: krema opens a second layer-shell surface for
    // the preview popup, so "first visible window" would otherwise flip
    // mid-scenario and send later actions to the wrong surface.
    auto target = std::make_shared<QPointer<QQuickWindow>>();
    auto tick = std::make_shared<std::function<void()>>();
    // Menu state is sampled live on every frame; retaining the creation-time
    // snapshot would let an unmapped or already-closed popup look successful.
    auto installed = std::make_shared<bool>(false);
    auto warmed = std::make_shared<bool>(false);
    auto pointerReset = std::make_shared<bool>(false);
    auto inputWait = std::make_shared<QElapsedTimer>();
    inputWait->start();
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

        // KWin preserves the pointer between compositor sessions often enough
        // for a new pass to begin hovered over a dock item. Put it at a neutral
        // output position before the settle interval so both passes start from
        // the same hover and animation state. Each pass also parks it there
        // before quitting, so the next one never maps its surfaces under a
        // stale pointer.
        if (!*pointerReset) {
            if (!fakeInput->moveTo(neutralPointerPosition(window))) {
                if (inputWait->elapsed() >= 5000) {
                    qFatal("KWin fake-input support did not become available");
                }
                QMetaObject::invokeMethod(
                    owner, [tick]() { (*tick)(); }, Qt::QueuedConnection);
                return;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            *pointerReset = true;
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
            // Without this, QUnifiedTimer measures each tick against the real
            // clock and compensates when it thinks it fell behind, so an
            // animation's first delta after registration is a coin flip:
            // measured on dock-visibility-autohide, frames 1-7 are identical
            // across runs and the 250 ms reveal's first tick lands at either
            // 0.080 or 0.095 of its range, after which the run either eases
            // over ~30 frames or completes in 2. Consistent timing makes every
            // tick advance by exactly the driver's step, which is the whole
            // premise of capturing animation state by frame number.
            QUnifiedTimer::instance()->setConsistentTiming(true);
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
            // From frame 1 on, every main-thread timer fires from the frame
            // loop at its virtual due time.
            timers->setVirtual();
        }

        ++*frame;

        // grabWindow() renders the scene synchronously, so the pixels and the
        // JSON row below always describe the same animation instant -- and,
        // critically, it is what guarantees exactly one render per captured
        // frame. Skipping it when screenshots are off would let the compositor
        // pace rendering instead, and animation state would then advance once
        // every few captured frames. Always capture; only PNG encoding is
        // gated, which is the expensive part.
        const QImage image = captureScreen(window);

        QJsonArray windowRows;
        for (QQuickWindow *quick : windows) {
            QJsonObject entry;
            entry[QStringLiteral("key")] = windowKey(quick);
            entry[QStringLiteral("target")] = quick == window;
            // Where this window was drawn on the composited frame, so a check
            // can map an item's rect back to pixels.
            QScreen *quickScreen = quick->screen();
            const QPoint origin = requestedTopLeft(
                quick, quickScreen ? quickScreen->geometry().size() : quick->size());
            entry[QStringLiteral("ox")] = origin.x();
            entry[QStringLiteral("oy")] = origin.y();
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
        const QJsonValue menu = activeMenu();
        if (!menu.isNull()) {
            event[QStringLiteral("menu")] = menu;
        }
        if (!image.isNull()) {
            image.save(QStringLiteral("%1/f%2.png").arg(frameDir).arg(*frame, 5, 10, QLatin1Char('0')));
        }
        *stream << QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact)) << "\n";
        stream->flush();

        bool acted = false;
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
            applyAction(destination, action, fakeInput);
            acted = true;
        }
        if (acted) {
            syncWithCompositor();
        }

        // Timers first, so state a timer changes starts its Behavior on the
        // same animation instant in every pass; the next captured row then
        // shows exactly one step of that animation.
        if (timers->advance(std::chrono::milliseconds(stepMs)) > 0) {
            syncWithCompositor();
        }
        driver->advance();

        if (maxFrames > 0 && *frame >= maxFrames) {
            stream->flush();
            out->close();
            fakeInput->moveTo(neutralPointerPosition(window));
            syncWithCompositor();
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
