FROM archlinux/base

RUN pacman --noconfirm -Syu && pacman --noconfirm -S base-devel git libx11 libxcomposite libxdamage libxinerama libxext libxrender libxrandr mesa libconfig freetype2 asciidoc

RUN mkdir /dep && mkdir /app && mkdir /dst
WORKDIR /dep
RUN useradd user && chmod o+rwx /dep /app /dst

VOLUME /app
VOLUME /dst

USER user
RUN git clone https://aur.archlinux.org/judy.git && cd judy && makepkg && mv *.pkg.tar.xz ../

USER root
RUN pacman --noconfirm -U *.pkg.tar.xz

COPY archbuild.sh /bin/archbuild.sh
RUN chmod +x /bin/archbuild.sh

USER user
WORKDIR /app
ENTRYPOINT [ "/bin/archbuild.sh" ]
