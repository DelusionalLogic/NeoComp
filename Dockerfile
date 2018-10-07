FROM base/devel:latest

VOLUME /app

RUN pacman -Syu && pacman --noconfirm -S git libx11 libxcomposite libxdamage libxinerama libxext libxrender libxrandr mesa libconfig dbus freetype2 asciidoc

RUN mkdir /dep
WORKDIR /dep
RUN useradd user && chmod o+rwx /dep /app

USER user
RUN git clone https://aur.archlinux.org/judy.git && cd judy && makepkg && mv *.pkg.tar.xz ../

USER root
RUN pacman --noconfirm -U *.pkg.tar.xz

USER user
WORKDIR /app
ENTRYPOINT [ "/usr/bin/makepkg" ]
