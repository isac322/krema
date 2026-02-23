// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.settings as KirigamiSettings
import com.bhyoo.krema 1.0

// Minimal host window for ConfigurationView.
// ConfigurationView.open() creates its own ConfigWindow on desktop,
// so this window stays hidden and only provides the required `window` property.
Kirigami.ApplicationWindow {
    id: settingsHost
    visible: false
    width: 0
    height: 0

    KirigamiSettings.ConfigurationView {
        id: configuration
        objectName: "configuration"
        window: settingsHost

        modules: [
            KirigamiSettings.ConfigurationModule {
                moduleId: "appearance"
                text: i18n("Appearance")
                icon.name: "preferences-desktop-theme-global"
                page: () => Qt.createComponent(Qt.resolvedUrl("settings/AppearancePage.qml"))
            },
            KirigamiSettings.ConfigurationModule {
                moduleId: "behavior"
                text: i18n("Behavior")
                icon.name: "preferences-system"
                page: () => Qt.createComponent(Qt.resolvedUrl("settings/BehaviorPage.qml"))
            },
            KirigamiSettings.ConfigurationModule {
                moduleId: "preview"
                text: i18n("Window Preview")
                icon.name: "view-preview"
                page: () => Qt.createComponent(Qt.resolvedUrl("settings/PreviewPage.qml"))
            },
            KirigamiSettings.ConfigurationModule {
                moduleId: "about"
                text: i18n("About Krema")
                icon.name: "help-about"
                page: () => Qt.createComponent("org.kde.kirigamiaddons.formcard", "AboutPage")
                category: i18nc("@title:group", "About")
            },
            KirigamiSettings.ConfigurationModule {
                moduleId: "aboutkde"
                text: i18n("About KDE")
                icon.name: "kde"
                page: () => Qt.createComponent("org.kde.kirigamiaddons.formcard", "AboutKDE")
                category: i18nc("@title:group", "About")
            }
        ]
    }

}
