# Use tab to indent recipe lines, spaces to indent other lines, otherwise
# GNU make may get unhappy.

CC ?= gcc

PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man/man1
APPDIR ?= $(PREFIX)/share/applications
ICODIR ?= $(PREFIX)/share/icons/hicolor/

OBJDIR ?= obj
SRCDIR ?= src


LIBS = -lGL -lm -lrt -lJudy
INCS = -Isrc/

CFG = -std=gnu11 -fms-extensions -flto

PACKAGES = x11 xcomposite xfixes xdamage xrender xext xrandr libpcre xinerama

MAIN_SOURCE = main.c

SOURCES = compton.c opengl.c vmath.c bezier.c timer.c swiss.c vector.c atoms.c
SOURCES += assets/assets.c assets/shader.c assets/face.c
SOURCES += shaders/shaderinfo.c shaders/include.c
SOURCES += blur.c shadow.c texture.c renderutil.c textureeffects.c
SOURCES += framebuffer.c renderbuffer.c window.c windowlist.c xorg.c xtexture.c
SOURCES += profiler/zone.c profiler/render.c

TEST_SOURCES = $(wildcard test/*.c)

# Text rendering
SOURCES += text.c
PACKAGES += freetype2

# === Configuration flags ===

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

# ==== libconfig ====
# Enables configuration file parsing support
ifeq "$(NO_LIBCONFIG)" ""
  CFG += -DCONFIG_LIBCONFIG
  PACKAGES += libconfig

  # libconfig-1.3* does not define LIBCONFIG_VER* macros, so we use
  # pkg-config to determine its version here
  CFG += $(shell pkg-config --atleast-version=1.4 libconfig || echo '-DCONFIG_LIBCONFIG_LEGACY')
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
  SOURCES += dbus.c
endif

# ==== C2 ====
# Enable window condition support
ifeq "$(NO_C2)" ""
  CFG += -DCONFIG_C2
  SOURCES += c2.c
endif

# ==== X resource checker ====
# Enable X resource leakage checking (Pixmap only, presently)
ifneq "$(ENABLE_XRESCHECK)" ""
  CFG += -DDEBUG_XRC
  SOURCES += xrescheck.c
endif

# === Version string ===
COMPTON_VERSION ?= git-$(shell git describe --always --dirty)-$(shell git log -1 --date=short --pretty=format:%cd)
CFG += -DCOMPTON_VERSION="\"$(COMPTON_VERSION)\""

LDFLAGS ?= -Wl,-O3 -Wl,--as-needed -Wl,--export-dynamic -flto

ifeq "$(CFG_DEV)" ""
  CFLAGS ?= -DNDEBUG -O3 -D_FORTIFY_SOURCE=2
else
  CC = clang
  export LD_ALTEXEC = /usr/bin/ld.gold
  # CFG += -DDEBUG_RESTACK
  LIBS += -lbfd
  CFLAGS += -O0 -g -Wshadow -Wno-microsoft-anon-tag
  # CFLAGS += -fsanitize=address -fsanitize=leak -fsanitize=null
  # CFLAGS += -Weverything -Wno-disabled-macro-expansion -Wno-padded -Wno-gnu
endif

LIBS += $(shell pkg-config --libs $(PACKAGES))
INCS += $(shell pkg-config --cflags $(PACKAGES))

CFLAGS += -Wall -Wno-microsoft-anon-tag

MAIN_SOURCE_C = $(MAIN_SOURCE:%.c=$(SRCDIR)/%.c)
SOURCES_C = $(SOURCES:%.c=$(SRCDIR)/%.c)
TEST_SOURCES_C = $(TEST_SOURCES)

MAIN_OBJS_C = $(MAIN_SOURCE:%.c=$(OBJDIR)/%.o)
OBJS_C = $(SOURCES:%.c=$(OBJDIR)/%.o)
TEST_OBJS_C = $(TEST_SOURCES_C:%.c=%.o)

MAIN_DEPS_C = $(MAIN_OBJS_C:%.o=%.d)
DEPS_C = $(OBJS_C:%.o=%.d)
TEST_DEPS_C = $(TEST_OBJS_C:%.o=%.d)

BINS = compton
MANPAGES = man/compton.1
MANPAGES_HTML = $(addsuffix .html,$(MANPAGES))

# === Recipes ===
.DEFAULT_GOAL := compton

src/.clang_complete: Makefile
	@(for i in $(filter-out -O% -DNDEBUG, $(CFG) $(CPPFLAGS) $(CFLAGS) $(INCS)); do echo "$$i"; done) > $@

compton: $(MAIN_OBJS_C) $(OBJS_C)
	$(CC) $(CFG) $(CPPFLAGS) $(LDFLAGS) $(CFLAGS) -o $@ $^ $(LIBS)

-include $(MAIN_DEPS_C) $(DEPS_C)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFG) $(CPPFLAGS) $(CFLAGS) $(INCS) -MMD -o $@ -c $<

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
ifneq "$(DOCDIR)" ""
	@install -d "$(DESTDIR)$(DOCDIR)"
	@install -m644 README.md "$(DESTDIR)$(DOCDIR)"/
endif

uninstall:
	@rm -f "$(DESTDIR)$(BINDIR)/compton"
	@rm -f $(addprefix "$(DESTDIR)$(MANDIR)"/, compton.1 compton-trans.1)
ifneq "$(DOCDIR)" ""
	@rm -f $(addprefix "$(DESTDIR)$(DOCDIR)"/, README.md)
endif

clean:
	@rm -f $(OBJS_C) $(MAIN_OBJS_C) compton $(MANPAGES) $(MANPAGES_HTML) .clang_complete
	@rm -f test/test test/test.o

version:
	@echo "$(COMPTON_VERSION)"

test/%.o: test/%.c
	$(CC) $(CFG) $(CPPFLAGS) $(CFLAGS) $(INCS) -MMD -o $@ -c $<

test/test: $(TEST_OBJS_C) $(OBJS_C)
	$(CC) $(CFG) $(CPPFLAGS) $(LDFLAGS) $(CFLAGS) -o $@ $^ $(LIBS)

test: test/test
	test/test

.PHONY: uninstall clean docs version
