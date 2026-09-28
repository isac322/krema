# Headless Plasma session for Krema product clips (see ./NOTES.md).
# Same environment as branding/screenshots/regen plus ffmpeg (x11grab
# recording) and libfaketime (slowed session clock).
#
# Two ways to get Krema into the image:
#   local (default)  build from a source tree passed as the named context `krema-src`:
#       docker build --build-context krema-src=. -t krema-clips branding/clips
#   copr             install the released package from COPR isac322/krema:
#       docker build --target copr -t krema-clips branding/clips
#   gpu              `local` plus the pieces for the GPU session (session-gpu.sh):
#                    Xwayland, the PipeWire GStreamer source and sessiontool.
#       docker build --build-context krema-src=. --target gpu -t krema-thumbs branding/clips

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

# GPU session: KWin's virtual backend composites with EGL on a render node, so
# its screencasts (window thumbnails, recording) work. See NOTES.md.
# libmali and jellyfin-ffmpeg are mounted at run time (thumbs.sh), not baked in.
FROM local AS gpu
USER root
RUN dnf -y --setopt=install_weak_deps=False install \
      xorg-x11-server-Xwayland pipewire-gstreamer gstreamer1-plugins-good gstreamer1-plugin-libav \
      plasma-wayland-protocols plasma-wayland-protocols-devel wayland-devel gcc python3-dbus python3-gobject && \
    dnf clean all
COPY sessiontool.c gles-shim.c /tmp/sessiontool/
# gles-shim links libmali (soname libmali.so.1), found at run time in
# /opt/mali; a stub with that soname stands in for it here.
RUN cd /tmp/sessiontool && \
    xml=/usr/share/plasma-wayland-protocols/zkde-screencast-unstable-v1.xml && \
    wayland-scanner client-header "$xml" zkde-screencast-unstable-v1.h && \
    wayland-scanner private-code "$xml" zkde-screencast-unstable-v1.c && \
    gcc -O2 -Wall -I. -o /usr/local/bin/sessiontool sessiontool.c zkde-screencast-unstable-v1.c -lwayland-client && \
    echo > stub.c && gcc -shared -fPIC -Wl,-soname,libmali.so.1 -o libmali.so.1 stub.c && \
    mkdir -p /usr/local/lib/krema-gles && \
    gcc -O2 -Wall -shared -fPIC -o /usr/local/lib/krema-gles/libGLESv2.so.2 gles-shim.c \
      -Wl,--no-as-needed ./libmali.so.1 -Wl,-rpath,/opt/mali -ldl && \
    rm -rf /tmp/sessiontool
USER alex
