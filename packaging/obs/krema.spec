# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

Name:           krema
Version:        0.7.0
Release:        1%{?dist}
Summary:        A lightweight dock for KDE Plasma 6

License:        GPL-3.0-or-later
URL:            https://github.com/isac322/krema
Source0:        https://github.com/isac322/%{name}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.22
%if 0%{?suse_version}
BuildRequires:  ninja
%else
BuildRequires:  ninja-build
%endif
BuildRequires:  gcc-c++ >= 14
BuildRequires:  extra-cmake-modules >= 6.0.0

# Qt 6
BuildRequires:  cmake(Qt6Core) >= 6.8.0
BuildRequires:  cmake(Qt6Gui) >= 6.8.0
BuildRequires:  cmake(Qt6Widgets) >= 6.8.0
BuildRequires:  cmake(Qt6DBus) >= 6.8.0
BuildRequires:  cmake(Qt6Quick) >= 6.8.0
BuildRequires:  cmake(Qt6QuickControls2) >= 6.8.0
BuildRequires:  cmake(Qt6ShaderTools) >= 6.8.0

# KDE Frameworks 6
BuildRequires:  cmake(KF6WindowSystem) >= 6.0.0
BuildRequires:  cmake(KF6Config) >= 6.0.0
BuildRequires:  cmake(KF6CoreAddons) >= 6.0.0
BuildRequires:  cmake(KF6DBusAddons) >= 6.0.0
BuildRequires:  cmake(KF6I18n) >= 6.0.0
BuildRequires:  cmake(KF6GlobalAccel) >= 6.0.0
BuildRequires:  cmake(KF6ColorScheme) >= 6.0.0
BuildRequires:  cmake(KF6IconThemes) >= 6.0.0
BuildRequires:  cmake(KF6Crash) >= 6.0.0
BuildRequires:  cmake(KF6XmlGui) >= 6.0.0
BuildRequires:  cmake(KF6Service) >= 6.0.0
BuildRequires:  cmake(KF6Kirigami) >= 6.0.0
BuildRequires:  cmake(KF6KirigamiAddons)

# Transitive dependencies (required by LibTaskManager/LibNotificationManager)
BuildRequires:  cmake(KF6ItemModels) >= 6.0.0

# Plasma and system
BuildRequires:  cmake(LayerShellQt) >= 6.0.0
BuildRequires:  cmake(LibTaskManager)
BuildRequires:  cmake(LibNotificationManager)
BuildRequires:  cmake(KPipeWire)
BuildRequires:  pkgconfig(wayland-client) >= 1.22

Requires:       kf6-kirigami%{?_isa}
Requires:       kf6-kirigami-addons%{?_isa}
Requires:       kpipewire%{?_isa}
Requires:       plasma-workspace%{?_isa}
Requires:       layer-shell-qt%{?_isa}

%description
Krema is a lightweight dock for KDE Plasma 6, designed as a spiritual
successor to Latte Dock. It features parabolic zoom animations, window
previews via PipeWire, and deep integration with KDE Plasma desktop.

%prep
%autosetup -p1

%build
%if 0%{?suse_version}
%cmake -DBUILD_TESTING=OFF
%else
%cmake -G Ninja -DBUILD_TESTING=OFF
%endif
%cmake_build

%install
%cmake_install

%files
%license LICENSES/GPL-3.0-or-later.txt
%{_bindir}/krema
%{_datadir}/applications/com.bhyoo.krema.desktop
%if 0%{?suse_version}
%{_prefix}/etc/xdg/autostart/com.bhyoo.krema.autostart.desktop
%else
%{_sysconfdir}/xdg/autostart/com.bhyoo.krema.autostart.desktop
%endif
%{_datadir}/metainfo/com.bhyoo.krema.metainfo.xml
