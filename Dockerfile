FROM pritunl/archlinux:latest

VOLUME /app

RUN pacman --noconfirm -S base-devel git libx11 libxcomposite libxdamage libxinerama libxext libxrender libxrandr mesa libconfig dbus freetype2
RUN pacman --noconfirm -U http://mirror.pritunl.com/archlinux/all/judy-1.0.5-4

WORKDIR /app
ENTRYPOINT [ "/usr/bin/make" ]
