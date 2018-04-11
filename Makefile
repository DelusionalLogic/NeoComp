# Use tab to indent recipe lines, spaces to indent other lines, otherwise
# GNU make may get unhappy.

CC ?= gcc

PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man/man1
APPDIR ?= $(PREFIX)/share/applications
ICODIR ?= $(PREFIX)/share/icons/hicolor/

PACKAGES = x11 xcomposite xfixes xdamage xrender xext xrandr
LIBS = -lm -lrt -lJudy
INCS =

OBJS = compton.o

# === Configuration flags ===
CFG = -std=gnu11 -fms-extensions

# -lGL must precede some other libraries, or it segfaults on FreeBSD (#74)
LIBS := -lGL $(LIBS)
OBJS += opengl.o

OBJS += vmath.o
OBJS += assets.o shader.o face.o shaderinfo.o blur.o shadow.o texture.o include.o
OBJS += renderutil.o textureeffects.o framebuffer.o zone.o render.o renderbuffer.o
OBJS += window.o windowlist.o
OBJS += xorg.o xtexture.o

ifneq "$(GLX_MARK)" ""
    CFG += -DDEBUG_GLX_MARK
endif

ifneq "$(GLX_CONTEXT_DEBUG)" ""
    CFG += -DDEBUG_GLX_DEBUG_CONTEXT
endif

ifneq "$(GLX_DEBUG)" ""
  CFG += -DDEBUG_GLX
  # CFG += -DDEBUG_GLX_PAINTREG
  CFG += -DDEBUG_GLX_GLSL
endif

ifneq "$(EVENTS_DEBUG)" ""
  CFG += -DDEBUG_EVENTS
endif

ifneq "$(PROFILE)" ""
    CFG += -DDEBUG_PROFILE
endif

# ==== Xinerama ====
# Enables support for --xinerama-shadow-crop
ifeq "$(NO_XINERAMA)" ""
  CFG += -DCONFIG_XINERAMA
  PACKAGES += xinerama
endif

# ==== libconfig ====
# Enables configuration file parsing support
ifeq "$(NO_LIBCONFIG)" ""
  CFG += -DCONFIG_LIBCONFIG
  PACKAGES += libconfig

  # libconfig-1.3* does not define LIBCONFIG_VER* macros, so we use
  # pkg-config to determine its version here
  CFG += $(shell pkg-config --atleast-version=1.4 libconfig || echo '-DCONFIG_LIBCONFIG_LEGACY')
endif

# ==== PCRE regular expression ====
# Enables support for PCRE regular expression pattern in window conditions
ifeq "$(NO_REGEX_PCRE)" ""
  CFG += -DCONFIG_REGEX_PCRE
  LIBS += $(shell pcre-config --libs)
  INCS += $(shell pcre-config --cflags)
  # Enables JIT support in libpcre
  ifeq "$(NO_REGEX_PCRE_JIT)" ""
    CFG += -DCONFIG_REGEX_PCRE_JIT
  endif
endif

# ==== DRM VSync ====
# Enables support for "drm" VSync method
ifeq "$(NO_VSYNC_DRM)" ""
  INCS += $(shell pkg-config --cflags libdrm)
  CFG += -DCONFIG_VSYNC_DRM
endif

# ==== D-Bus ====
# Enables support for --dbus (D-Bus remote control)
ifeq "$(NO_DBUS)" ""
  CFG += -DCONFIG_DBUS
  PACKAGES += dbus-1
  OBJS += dbus.o
endif

# ==== X Sync ====
# Enables support for --xrender-sync-fence
ifeq "$(NO_XSYNC)" ""
  CFG += -DCONFIG_XSYNC
endif

# ==== C2 ====
# Enable window condition support
ifeq "$(NO_C2)" ""
  CFG += -DCONFIG_C2
  OBJS += c2.o
endif

# ==== X resource checker ====
# Enable X resource leakage checking (Pixmap only, presently)
ifneq "$(ENABLE_XRESCHECK)" ""
  CFG += -DDEBUG_XRC
  OBJS += xrescheck.o
endif

# === Version string ===
COMPTON_VERSION ?= git-$(shell git describe --always --dirty)-$(shell git log -1 --date=short --pretty=format:%cd)
CFG += -DCOMPTON_VERSION="\"$(COMPTON_VERSION)\""

LDFLAGS ?= -Wl,-O1 -Wl,--as-needed -Wl,--export-dynamic

ifeq "$(CFG_DEV)" ""
  CFLAGS ?= -DNDEBUG -O2 -D_FORTIFY_SOURCE=2
else
  CC = clang
  export LD_ALTEXEC = /usr/bin/ld.gold
  CFG += -DDEBUG_RESTACK
  # OBJS += backtrace-symbols.o
  LIBS += -lbfd
  CFLAGS += -O0 -g -Wshadow -Wno-microsoft-anon-tag
  # CFLAGS += -Weverything -Wno-disabled-macro-expansion -Wno-padded -Wno-gnu
endif

LIBS += $(shell pkg-config --libs $(PACKAGES))
INCS += $(shell pkg-config --cflags $(PACKAGES))

CFLAGS += -Wall

BINS = compton bin/compton-trans
MANPAGES = man/compton.1 man/compton-trans.1
MANPAGES_HTML = $(addsuffix .html,$(MANPAGES))

# === Recipes ===
.DEFAULT_GOAL := compton

src/.clang_complete: Makefile
	@(for i in $(filter-out -O% -DNDEBUG, $(CFG) $(CPPFLAGS) $(CFLAGS) $(INCS)); do echo "$$i"; done) > $@

%.o: src/%.c src/%.h src/common.h
	$(CC) $(CFG) $(CPPFLAGS) $(CFLAGS) $(INCS) -c src/$*.c

%.o: src/*/%.c src/*/%.h
	$(CC) $(CFG) $(CPPFLAGS) $(CFLAGS) $(INCS) -c $<

compton: $(OBJS)
	$(CC) $(CFG) $(CPPFLAGS) $(LDFLAGS) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

man/%.1: man/%.1.asciidoc
	a2x --format manpage $<

man/%.1.html: man/%.1.asciidoc
	asciidoc $<

docs: $(MANPAGES) $(MANPAGES_HTML)

install: $(BINS) docs
	@install -d "$(DESTDIR)$(BINDIR)" "$(DESTDIR)$(MANDIR)" "$(DESTDIR)$(APPDIR)"
	@install -m755 $(BINS) "$(DESTDIR)$(BINDIR)"/
ifneq "$(MANPAGES)" ""
	@install -m644 $(MANPAGES) "$(DESTDIR)$(MANDIR)"/
endif
	@install -d \
		"$(DESTDIR)$(ICODIR)/scalable/apps" \
		"$(DESTDIR)$(ICODIR)/48x48/apps"
	@install -m644 media/compton.svg "$(DESTDIR)$(ICODIR)/scalable/apps"/
	@install -m644 media/icons/48x48/compton.png "$(DESTDIR)$(ICODIR)/48x48/apps"/
	@install -m644 compton.desktop "$(DESTDIR)$(APPDIR)"/
ifneq "$(DOCDIR)" ""
	@install -d "$(DESTDIR)$(DOCDIR)"
	@install -m644 README.md compton.sample.conf "$(DESTDIR)$(DOCDIR)"/
	@install -m755 dbus-examples/cdbus-driver.sh "$(DESTDIR)$(DOCDIR)"/
endif

uninstall:
	@rm -f "$(DESTDIR)$(BINDIR)/compton" "$(DESTDIR)$(BINDIR)/compton-trans"
	@rm -f $(addprefix "$(DESTDIR)$(MANDIR)"/, compton.1 compton-trans.1)
	@rm -f "$(DESTDIR)$(APPDIR)/compton.desktop"
ifneq "$(DOCDIR)" ""
	@rm -f $(addprefix "$(DESTDIR)$(DOCDIR)"/, README.md compton.sample.conf cdbus-driver.sh)
endif

clean:
	@rm -f $(OBJS) compton $(MANPAGES) $(MANPAGES_HTML) .clang_complete

version:
	@echo "$(COMPTON_VERSION)"

.PHONY: uninstall clean docs version
