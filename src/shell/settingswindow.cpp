// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "settingswindow.h"

#include "krema.h"
#include "multidockmanager.h"
#include "outputordermonitor.h"
#include "style/backgroundstyle.h"

#include <KConfigGroup>
#include <KLocalizedQmlContext>
#include <KSharedConfig>

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QPalette>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QScreen>
#include <QStandardPaths>

#include <utility>

Q_LOGGING_CATEGORY(lcSettingsWindow, "krema.settings.window")

namespace
{

// Fallback when no screen is known (also gives the 16:9 aspect).
constexpr QSizeF kFallbackScreenSize(1920.0, 1080.0);

// Picks the image of a wallpaper package (contents/images[_dark]/WxH.ext)
// closest to the screen: the smallest image covering it, else the largest.
QString packageImage(const QDir &package, const QSize &target, bool dark)
{
    static const QRegularExpression sizeName(QStringLiteral(R"(^(\d+)x(\d+)$)"));
    QStringList subdirs;
    if (dark) {
        subdirs.append(QStringLiteral("contents/images_dark"));
    }
    subdirs.append(QStringLiteral("contents/images"));

    for (const QString &subdir : std::as_const(subdirs)) {
        const QFileInfoList files = QDir(package.filePath(subdir)).entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
        if (files.isEmpty()) {
            continue;
        }
        QString covering;
        qint64 coveringArea = 0;
        QString largest;
        qint64 largestArea = -1;
        for (const QFileInfo &file : files) {
            const auto match = sizeName.match(file.completeBaseName());
            if (!match.hasMatch()) {
                continue;
            }
            const int width = match.captured(1).toInt();
            const int height = match.captured(2).toInt();
            const qint64 area = qint64(width) * height;
            if (width >= target.width() && height >= target.height() && (covering.isEmpty() || area < coveringArea)) {
                covering = file.absoluteFilePath();
                coveringArea = area;
            }
            if (area > largestArea) {
                largest = file.absoluteFilePath();
                largestArea = area;
            }
        }
        if (!covering.isEmpty()) {
            return covering;
        }
        if (!largest.isEmpty()) {
            return largest;
        }
        return files.first().absoluteFilePath();
    }
    return {};
}

// Local image file for an org.kde.image "Image" entry: a file path or URL,
// or a wallpaper package directory.
QString resolveWallpaperImage(const QString &entry, const QSize &target, bool dark)
{
    const QString path = entry.startsWith(QLatin1String("file:")) ? QUrl(entry).toLocalFile() : entry;
    const QFileInfo info(path);
    if (info.isFile()) {
        return info.absoluteFilePath();
    }
    if (info.isDir()) {
        return packageImage(QDir(info.absoluteFilePath()), target, dark);
    }
    return {};
}

// "Image" entry of the desktop containment shown on Plasma's first screen
// (lastScreen=0), else of any desktop containment. Plasma's default
// wallpaper when the desktop uses the image plugin without an explicit one.
QString plasmaWallpaperEntry()
{
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc"), KConfig::NoGlobals);
    config->reparseConfiguration();
    const KConfigGroup containments = config->group(QStringLiteral("Containments"));

    static const QStringList desktopPlugins{
        QStringLiteral("org.kde.desktopcontainment"),
        QStringLiteral("org.kde.desktop"),
        QStringLiteral("org.kde.plasma.folder"),
    };

    QString anyImage;
    bool usesImagePlugin = false;
    const QStringList ids = containments.groupList();
    for (const QString &id : ids) {
        const KConfigGroup containment = containments.group(id);
        if (!desktopPlugins.contains(containment.readEntry("plugin", QString()))) {
            continue;
        }
        if (containment.readEntry("wallpaperplugin", QStringLiteral("org.kde.image")) != QLatin1String("org.kde.image")) {
            continue;
        }
        usesImagePlugin = true;
        const QString image = containment.group(QStringLiteral("Wallpaper"))
                                  .group(QStringLiteral("org.kde.image"))
                                  .group(QStringLiteral("General"))
                                  .readEntry("Image", QString());
        if (image.isEmpty()) {
            continue;
        }
        if (containment.readEntry("lastScreen", -1) == 0) {
            return image;
        }
        if (anyImage.isEmpty()) {
            anyImage = image;
        }
    }
    if (anyImage.isEmpty() && (usesImagePlugin || ids.isEmpty())) {
        return QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("wallpapers/Next"), QStandardPaths::LocateDirectory);
    }
    return anyImage;
}

} // namespace

namespace krema
{

SettingsWindow::SettingsWindow(KremaSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    auto *outputOrder = OutputOrderMonitor::instance();
    connect(outputOrder, &OutputOrderMonitor::primaryOutputChanged, this, &SettingsWindow::updateAvailableScreens);
    connect(m_settings, &KremaSettings::SelectedOutputsChanged, this, &SettingsWindow::updateAvailableScreens);
    connect(m_settings, &KremaSettings::MonitorModeChanged, this, &SettingsWindow::updateAvailableScreens);

    if (qGuiApp) {
        connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen *screen) {
            watchScreen(screen);
            updateAvailableScreens();
        });
        connect(qGuiApp, &QGuiApplication::screenRemoved, this, &SettingsWindow::updateAvailableScreens);
        connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, &SettingsWindow::updateAvailableScreens);
    }
    for (auto *screen : QGuiApplication::screens()) {
        watchScreen(screen);
    }
    updateAvailableScreens();
}

SettingsWindow::~SettingsWindow()
{
    // Destroy the open window and any closed window still awaiting deferred
    // deletion (#27) here, while the engine and the "SettingsWindow" context
    // property are intact. Disconnect first so no close handling runs from
    // here.
    auto windows = std::exchange(m_closedWindows, {});
    windows.append(m_configWindow);
    m_configWindow = nullptr;
    for (const auto &win : std::as_const(windows)) {
        if (win) {
            disconnect(win, nullptr, this, nullptr);
            delete win.data();
        }
    }

    // Delete the engine while the "SettingsWindow" context property still
    // resolves to this object. m_engine is only a QObject child, so a default
    // destructor would destroy it after this object's QML bindings are gone
    // and teardown would log TypeError (e.g. "isStyleAvailable of null").
    delete m_component;
    m_component = nullptr;
    delete m_engine;
    m_engine = nullptr;
}

bool SettingsWindow::isVisible() const
{
    return m_visible;
}

QVariantList SettingsWindow::availableScreens() const
{
    return m_availableScreens;
}

bool SettingsWindow::hasSelectedMonitorFallback() const
{
    return m_hasSelectedMonitorFallback;
}

QUrl SettingsWindow::wallpaperUrl() const
{
    return m_wallpaperUrl;
}

qreal SettingsWindow::screenAspect() const
{
    const QSizeF size = screenSize();
    return size.width() / size.height();
}

QSizeF SettingsWindow::screenSize() const
{
    return m_screenSize.isEmpty() ? kFallbackScreenSize : m_screenSize;
}

void SettingsWindow::updateScreenGeometry()
{
    // May be null on headless sessions; the fallbacks of screenSize() apply.
    const QScreen *screen = OutputOrderMonitor::instance()->primaryScreen();
    const QSizeF size = screen ? QSizeF(screen->geometry().size()) : QSizeF();
    const QSize pixels = (size * (screen ? screen->devicePixelRatio() : 1.0)).toSize();
    const bool pixelsChanged = pixels != m_screenPixels;
    m_screenPixels = pixels;
    if (size != m_screenSize) {
        m_screenSize = size;
        Q_EMIT screenGeometryChanged();
    }
    // The best image of a wallpaper package depends on the screen size.
    if (pixelsChanged && m_configWindow) {
        updateWallpaper();
    }
}

void SettingsWindow::updateWallpaper()
{
    const QSize target = m_screenPixels.isEmpty() ? kFallbackScreenSize.toSize() : m_screenPixels;
    const bool dark = qGuiApp && qGuiApp->palette().color(QPalette::Window).lightnessF() < 0.5;
    const QString image = resolveWallpaperImage(plasmaWallpaperEntry(), target, dark);
    const QUrl url = image.isEmpty() ? QUrl() : QUrl::fromLocalFile(image);
    if (url != m_wallpaperUrl) {
        m_wallpaperUrl = url;
        Q_EMIT wallpaperUrlChanged();
    }
}

void SettingsWindow::watchScreen(QScreen *screen)
{
    connect(screen, &QScreen::geometryChanged, this, &SettingsWindow::updateAvailableScreens);
}

void SettingsWindow::updateAvailableScreens()
{
    const auto screens = QGuiApplication::screens();
    const auto selectedOutputs = m_settings->selectedOutputs();
    const auto *primary = OutputOrderMonitor::instance()->primaryScreen();
    QVariantList rows;
    rows.reserve(screens.size() + selectedOutputs.size());
    QStringList outputNames;
    outputNames.reserve(screens.size() + selectedOutputs.size());
    bool hasLiveSelection = false;
    for (auto *screen : screens) {
        const QString name = screen->name();
        const bool available = !screen->geometry().isEmpty();
        rows.append(QVariantMap{
            {QStringLiteral("name"), name},
            {QStringLiteral("label"), name},
            {QStringLiteral("available"), available},
            {QStringLiteral("primary"), screen == primary},
        });
        outputNames.append(name);
        hasLiveSelection |= available && selectedOutputs.contains(name);
    }
    for (const auto &name : selectedOutputs) {
        if (outputNames.contains(name)) {
            continue;
        }
        rows.append(QVariantMap{
            {QStringLiteral("name"), name},
            {QStringLiteral("label"), name},
            {QStringLiteral("available"), false},
            {QStringLiteral("primary"), false},
        });
        outputNames.append(name);
    }

    const bool fallback = m_settings->monitorMode() == MultiDockManager::SelectedScreens && !hasLiveSelection;
    const bool rowsChanged = rows != m_availableScreens;
    const bool fallbackChanged = fallback != m_hasSelectedMonitorFallback;
    m_availableScreens = std::move(rows);
    m_hasSelectedMonitorFallback = fallback;
    if (rowsChanged) {
        Q_EMIT availableScreensChanged();
    }
    if (fallbackChanged) {
        Q_EMIT hasSelectedMonitorFallbackChanged();
    }
    updateScreenGeometry();
}

bool SettingsWindow::isStyleAvailable(int styleType) const
{
    return krema::isStyleAvailable(static_cast<BackgroundStyleType>(styleType));
}

void SettingsWindow::show()
{
    open(QString());
}

void SettingsWindow::show(const QString &defaultModule)
{
    open(defaultModule);
}

void SettingsWindow::open(const QString &defaultModule)
{
    if (m_configWindow) {
        // Already open: switch to the requested page and raise it.
        if (!defaultModule.isEmpty()) {
            QMetaObject::invokeMethod(m_configWindow, "openModule", Q_ARG(QVariant, defaultModule));
        }
    } else {
        // Pick up wallpaper changes made since the last open.
        updateWallpaper();
        m_configWindow = createWindow(defaultModule);
        if (!m_configWindow) {
            return;
        }
    }

    m_configWindow->show();
    m_configWindow->raise();
    m_configWindow->requestActivate();

    // Emit open exactly once per open/close cycle to avoid double-counting
    // that breaks the dodge interacting refcount.
    if (!m_visible) {
        m_visible = true;
        Q_EMIT visibleChanged(true);
    }
}

QQuickWindow *SettingsWindow::createWindow(const QString &defaultModule)
{
    if (!m_engine) {
        m_engine = new QQmlEngine(this);
        KLocalization::setupLocalizedContext(m_engine);

        // Expose this object so settings QML can call
        // SettingsWindow.isStyleAvailable() without process-global singleton
        // registration or a per-dock object.
        m_engine->rootContext()->setContextProperty(QStringLiteral("SettingsWindow"), this);

        m_component = new QQmlComponent(m_engine, QUrl(QStringLiteral("qrc:/qml/SettingsDialog.qml")), QQmlComponent::PreferSynchronous, m_engine);
    }

    if (!m_component->isReady()) {
        qCWarning(lcSettingsWindow) << "Failed to load SettingsDialog.qml:" << m_component->errorString();
        return nullptr;
    }

    QObject *object = m_component->createWithInitialProperties({{QStringLiteral("defaultModule"), defaultModule}});
    if (!object) {
        qCWarning(lcSettingsWindow) << "Failed to create SettingsDialog.qml:" << m_component->errorString();
        return nullptr;
    }
    auto *win = qobject_cast<QQuickWindow *>(object);
    if (!win) {
        qCWarning(lcSettingsWindow) << "Root object of SettingsDialog.qml is not a QQuickWindow";
        delete object;
        return nullptr;
    }

    // This object owns the window: it is destroyed on close and at teardown,
    // never by the QML garbage collector.
    QQmlEngine::setObjectOwnership(win, QQmlEngine::CppOwnership);
    win->setIcon(QGuiApplication::windowIcon());

    connect(win, &QWindow::visibleChanged, this, [this, win](bool visible) {
        // Only forward close events — open is emitted by open().
        if (!visible) {
            onWindowHidden(win);
        }
    });

    qCDebug(lcSettingsWindow) << "Created settings window:" << win;
    return win;
}

void SettingsWindow::onWindowHidden(QQuickWindow *win)
{
    if (win != m_configWindow) {
        return;
    }

    // A closed window is destroyed rather than kept hidden; the next open
    // creates a fresh one.
    disconnect(win, nullptr, this, nullptr);
    m_configWindow = nullptr;
    m_closedWindows.removeIf([](const QPointer<QQuickWindow> &closed) {
        return closed.isNull();
    });
    m_closedWindows.append(win);
    win->deleteLater();

    if (m_visible) {
        m_visible = false;
        Q_EMIT visibleChanged(false);
    }
}

} // namespace krema
