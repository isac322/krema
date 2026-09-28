# Headless Plasma session for Krema product clips (see ./NOTES.md).
# Same environment as branding/screenshots/regen plus ffmpeg (x11grab
# recording) and libfaketime (slowed session clock).
#
# Two ways to get Krema into the image:
#   local (default)  build from a source tree passed as the named context `krema-src`:
#       docker build --build-context krema-src=. -t krema-clips branding/clips
#   copr             install the released package from COPR isac322/krema:
#       docker build --target copr -t krema-clips branding/clips

FROM fedora:44 AS base
RUN dnf -y --setopt=install_weak_deps=False install \
      kwin-wayland kglobalacceld plasma-workspace plasma-workspace-wallpapers kactivitymanagerd \
      kf6-kirigami kf6-kirigami-addons kpipewire layer-shell-qt hicolor-icon-theme \
      plasma-breeze breeze-icon-theme kf6-qqc2-desktop-style google-noto-sans-fonts \
      dolphin konsole kate systemsettings okular gwenview kcalc elisa-player plasma-systemmonitor firefox \
      mesa-dri-drivers mesa-libEGL mesa-libgbm dbus-daemon dbus-tools \
      pipewire wireplumber xdg-desktop-portal-kde procps-ng python3 \
      xorg-x11-server-Xvfb xdotool ffmpeg libfaketime && \
    dnf clean all && \
    useradd -m -s /bin/bash alex && echo 'PS1="[\u@fedora \W]\$ "' >> /home/alex/.bashrc
RUN setcap -r /usr/sbin/kwin_wayland
ENV LANG=C.UTF-8 LC_ALL=C.UTF-8
WORKDIR /home/alex

FROM base AS copr
RUN dnf -y install dnf-plugins-core && dnf -y copr enable isac322/krema && \
    dnf -y --setopt=install_weak_deps=False install krema && dnf clean all
USER alex

FROM fedora:44 AS build
RUN dnf -y install dnf-plugins-core rpm-build
COPY --from=krema-src packaging/obs/krema.spec /src/packaging/obs/krema.spec
RUN dnf -y builddep /src/packaging/obs/krema.spec
COPY --from=krema-src CMakeLists.txt /src/CMakeLists.txt
COPY --from=krema-src src /src/src
RUN cmake -S /src -B /build -G Ninja -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF && \
    cmake --build /build && \
    DESTDIR=/stage cmake --install /build

FROM base AS local
COPY --from=build /stage/ /
USER alex
