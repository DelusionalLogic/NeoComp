# Use tab to indent recipe lines, spaces to indent other lines, otherwise
# GNU make may get unhappy.

CC ?= gcc

PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man/man1
CFGDIR ?= /etc/xdg/neocomp
ASTDIR ?= $(CFGDIR)/assets

OBJDIR ?= obj
SRCDIR ?= src
GENDIR ?= gen


LIBS = -lGL -lm -lrt -lJudy
INCS = -Isrc/ -Igen/ -I.

CFG = -std=gnu11 -fms-extensions -flto

PACKAGES = xcomposite xres xfixes xdamage xrender xext xrandr libpcre xinerama xcb x11-xcb xcb-composite
# Text rendering
PACKAGES += freetype2

SOURCES = $(shell find $(SRCDIR) -name "*.c")
INTERCEPT_SOURCES = gen/intercept/xorg.c

TEST_SOURCES = $(wildcard test/*.c)

SHADEGEN_SOURCES = $(wildcard shadegen/*.c)
SHADERTYPE_SOURCES = $(wildcard shadertypes/*.type)

INTGEN_SOURCES = $(wildcard intgen/*.c)

print-%  : ; @echo $* = $($*)

# === Configuration flags ===

ifneq "$(GLX_DEBUG)" ""
  CFG += -DDEBUG_GLX
  # CFG += -DDEBUG_GLX_PAINTREG
  CFG += -DDEBUG_GLX_GLSL
endif

ifneq "$(FRAMERATE_DISPLAY)" ""
  CFG += -DFRAMERATE_DISPLAY
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
endif

# === Version string ===
COMPTON_VERSION ?= git-$(shell git describe --always --dirty)-$(shell git log -1 --date=short --pretty=format:%cd)
CFG += -DCOMPTON_VERSION="\"$(COMPTON_VERSION)\""

LDFLAGS ?= -Wl,-O3 -Wl,--as-needed -Wl,--export-dynamic -flto

ifeq "$(CFG_DEV)" ""
  CFLAGS ?= -DNDEBUG -O3 -D_FORTIFY_SOURCE=2
else ifeq "$(CFG_DEV)" "p"
  CFLAGS += -O0 -g -Wshadow -Wno-microsoft-anon-tag
else
  # CFG += -DDEBUG_RESTACK
  LIBS += -lbfd
  CFLAGS += -O0 -g -Wshadow -Wno-microsoft-anon-tag -DDEBUG_WINDOWS 
  # CFLAGS += -fsanitize=address -fsanitize=leak -fsanitize=null
  # CFLAGS += -Weverything -Wno-disabled-macro-expansion -Wno-padded -Wno-gnu
endif

ifeq "$(PROFILE)" "prof"
    CFLAGS += -pg
endif

LIBS += $(shell pkg-config --libs $(PACKAGES))
INCS += $(shell pkg-config --cflags $(PACKAGES))

CFLAGS += -Wall -Wno-microsoft-anon-tag


OBJS_C = $(SOURCES:%.c=$(OBJDIR)/%.o)
INTERCEPT_OBJS_C = $(INTERCEPT_SOURCES:%.c=$(OBJDIR)/%.o)
TEST_OBJS_C = $(TEST_SOURCES:%.c=$(OBJDIR)/%.o)
SHADEGEN_OBJS_C = $(SHADEGEN_SOURCES:%.c=$(OBJDIR)/%.o)
INTGEN_OBJS_C = $(INTGEN_SOURCES:%.c=$(OBJDIR)/%.o)
# Generated shadertype source
OBJS_C += $(OBJDIR)/gen/shaders/include.o

DEPS_C = $(OBJS_C:%.o=%.d)
INTERCEPT_DEPS_C = $(INTERCEPT_OBJS_C:%.o=%.d)
TEST_DEPS_C = $(TEST_OBJS_C:%.o=%.d)
SHADEGEN_DEPS_C = $(SHADEGEN_OBJS_C:%.o=%.d)
INTGEN_DEPS_C = $(INTGEN_OBJS_C:%.o=%.d)

BINS = neocomp
MANPAGES = man/neocomp.1
MANPAGES_HTML = $(addsuffix .html,$(MANPAGES))

# === Recipes ===
.DEFAULT_GOAL := neocomp

src/.clang_complete: Makefile
	@(for i in $(filter-out -O% -DNDEBUG, $(CFG) $(CPPFLAGS) $(CFLAGS) $(INCS)); do echo "$$i"; done) > $@

neocomp: gen $(INTERCEPT_OBJS_C) $(OBJS_C)
	$(CC) $(CFG) $(CPPFLAGS) $(LDFLAGS) $(CFLAGS) -o $@ $(OBJS_C) $(INTERCEPT_OBJS_C) $(LIBS)

gen: gen/shaders/include.h gen/shaders/include.c gen/intercept/xorg.h gen/intercept/xorg.c

-include $(DEPS_C) $(INTERCEPT_DEPS_C) $(TEST_DEPS_C) $(SHADEGEN_DEPS_C) $(INTGEN_DEPS_C)

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFG) $(CPPFLAGS) $(CFLAGS) $(INCS) -MMD -o $@ -c $<

man/%.1: man/%.1.asciidoc
	a2x --format manpage $<

man/%.1.html: man/%.1.asciidoc
	asciidoc $<

docs: $(MANPAGES) $(MANPAGES_HTML)

install: $(BINS) docs
	@install -d "$(DESTDIR)$(BINDIR)" "$(DESTDIR)$(MANDIR)" "$(DESTDIR)$(ASTDIR)"
	@install -m755 $(BINS) "$(DESTDIR)$(BINDIR)"
	@install -m755 assets/* "$(DESTDIR)$(ASTDIR)"
ifneq "$(MANPAGES)" ""
	@install -m644 $(MANPAGES) "$(DESTDIR)$(MANDIR)"/
endif
ifneq "$(DOCDIR)" ""
	@install -d "$(DESTDIR)$(DOCDIR)"
	@install -m644 README.md "$(DESTDIR)$(DOCDIR)"/
endif

uninstall:
	@rm -f "$(DESTDIR)$(BINDIR)/neocomp"
	@rm -f $(addprefix "$(DESTDIR)$(MANDIR)"/, neocomp.1)
ifneq "$(DOCDIR)" ""
	@rm -f $(addprefix "$(DESTDIR)$(DOCDIR)"/, README.md)
endif

clean:
	@rm -rf $(OBJDIR) gen/
	@rm -f $(OBJDIR) neocomp $(MANPAGES) $(MANPAGES_HTML) .clang_complete
	@rm -f test/test test/test.o
	@rm -f shadegen/shadegen
	@rm -f intgen/intgen

version:
	@echo "$(COMPTON_VERSION)"

test/test: gen/intercept/xorg.h gen/shaders/include.h $(TEST_OBJS_C) $(filter-out $(OBJDIR)/$(SRCDIR)/main.o, $(OBJS_C))
	$(CC) $(CFG) $(CPPFLAGS) $(LDFLAGS) $(CFLAGS) -o $@ $(TEST_OBJS_C) $(filter-out $(OBJDIR)/$(SRCDIR)/main.o, $(OBJS_C)) $(LIBS)

test: test/test
	test/test $(TESTS)

$(OBJDIR)/shadegen/shadegen: $(SHADEGEN_OBJS_C)
	$(CC) $(CFG) $(CPPFLAGS) $(LDFLAGS) $(CFLAGS) -o $@ $^

$(OBJDIR)/intgen/intgen: $(INTGEN_OBJS_C)
	$(CC) $(CFG) $(CPPFLAGS) $(LDFLAGS) $(CFLAGS) -o $@ $^

gen/shaders/include.h: $(OBJDIR)/shadegen/shadegen $(SHADERTYPE_SOURCES)
	@mkdir -p $(dir $@)
	$(OBJDIR)/shadegen/shadegen $(SHADERTYPE_SOURCES) -o $@

gen/shaders/include.c: $(OBJDIR)/shadegen/shadegen $(SHADERTYPE_SOURCES)
	@mkdir -p $(dir $@)
	$(OBJDIR)/shadegen/shadegen $(SHADERTYPE_SOURCES) -c -o $@

gen/intercept/xorg.c: $(OBJDIR)/intgen/intgen intercept/xorg.int
	@mkdir -p $(dir $@)
	$(OBJDIR)/intgen/intgen -c intercept/xorg.int -o $@

gen/intercept/xorg.h: $(OBJDIR)/intgen/intgen intercept/xorg.int
	@mkdir -p $(dir $@)
	$(OBJDIR)/intgen/intgen intercept/xorg.int -o $@

.PHONY: test install uninstall clean docs version
