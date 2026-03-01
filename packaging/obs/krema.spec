# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

Name:           krema
Version:        0.1.0
Release:        1%{?dist}
Summary:        Latte Dock replacement for KDE Plasma 6
License:        GPL-3.0-or-later
URL:            https://github.com/isac322/krema
Source0:        %{name}-%{version}.tar.xz

BuildRequires:  cmake >= 3.22
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  extra-cmake-modules >= 6.0.0

BuildRequires:  cmake(Qt6Core) >= 6.6.0
BuildRequires:  cmake(Qt6Gui) >= 6.6.0
BuildRequires:  cmake(Qt6Widgets) >= 6.6.0
BuildRequires:  cmake(Qt6DBus) >= 6.6.0
BuildRequires:  cmake(Qt6Quick) >= 6.6.0

BuildRequires:  cmake(KF6WindowSystem) >= 6.0.0
BuildRequires:  cmake(KF6Config) >= 6.0.0
BuildRequires:  cmake(KF6CoreAddons) >= 6.0.0
BuildRequires:  cmake(KF6I18n) >= 6.0.0
BuildRequires:  cmake(KF6GlobalAccel) >= 6.0.0

BuildRequires:  cmake(LayerShellQt) >= 6.0.0
BuildRequires:  pkgconfig(wayland-client) >= 1.22

%description
Krema is a dock application for KDE Plasma 6, designed as a replacement
for the discontinued Latte Dock. It features parabolic zoom animations,
Wayland native support via LayerShellQt, and deep KDE integration.

%prep
%autosetup -p1

%build
%cmake -G Ninja
%cmake_build

%install
%cmake_install

%files
%license LICENSES/GPL-3.0-or-later.txt
%{_bindir}/krema
%{_datadir}/applications/com.bhyoo.krema.desktop
%{_sysconfdir}/xdg/autostart/com.bhyoo.krema.autostart.desktop
