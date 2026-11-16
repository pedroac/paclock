# The code shared by the pac tools (config_file, fontfind, ...)
PACOMMON ?= pacommon
ifeq ($(wildcard $(PACOMMON)/pac.mk),)
$(error pacommon is not at $(PACOMMON): run: git submodule update --init, or set PACOMMON=path/to/pacommon)
endif
include $(PACOMMON)/pac.mk

CC ?= gcc

PREFIX ?= /usr/local
SYSCONFDIR ?= /etc
BINDIR ?= $(PREFIX)/bin
DOCDIR ?= $(PREFIX)/share/doc/paclock
LOCALEDIR ?= $(PREFIX)/share/locale
# I18N=0 builds an English-only binary: no gettext calls and no catalogs.
I18N ?= 1
# LAYOUTS=0 builds without the choice of keyboard layout: the compositor's keymap is the only one.
LAYOUTS ?= 1

# HarfBuzz and FriBidi (shaping, right-to-left text: pacommon's textlayout.c) are for translated builds; I18N=0 has neither.
PKGS = wayland-client cairo freetype2 xkbcommon $(PAC_SPEECH_PKGS) $(if $(filter 0,$(I18N)),,harfbuzz fribidi)
# CFLAGS, LDFLAGS and LDLIBS belong to the user (or the distro's packaging) and can be
# overridden freely; what the build itself needs is added separately below.
CFLAGS ?= -Os -flto -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-plt -Wall -Wextra -Wshadow -Wvla -Wformat-security
LDFLAGS ?= -s -flto
# Hardening: fortified libc calls, stack protector, PIE, full RELRO and immediate binding.
# Control-flow protection only exists on x86 compilers, so ask the compiler.
CET_CFLAGS := $(shell echo 'int main(void){return 0;}' | $(CC) -x c - -fcf-protection=full \
                -o /dev/null 2>/dev/null && echo -fcf-protection=full)
ALL_CFLAGS := $(CFLAGS) -ffunction-sections -fdata-sections -std=gnu11 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 -fstack-protector-strong \
              -fstack-clash-protection $(CET_CFLAGS) -fPIE -DSYSCONFDIR='"$(SYSCONFDIR)"' -DLOCALEDIR='"$(LOCALEDIR)"' $(if $(filter 0,$(I18N)),-DNO_I18N) $(if $(filter 0,$(LAYOUTS)),-DNO_LAYOUTS) $(PAC_SPEECH_CFLAGS) -MMD -MP -Ibuild -I$(PACOMMON)/src \
              $(shell pkg-config --cflags $(PKGS))
# Packed relative relocations (DT_RELR) shrink the binary by ~15 KiB, but need glibc 2.36 / musl 1.2.4
# at run time: only use them if a trivial program linked with them links and runs here. (build/ is made
# first: the probe runs when the Makefile is read, before any rule, and on a clean tree it is not there.)
RELR_LDFLAGS := $(shell mkdir -p build; echo 'int main(void){return 0;}' | $(CC) -x c - -fPIE -pie -Wl,-z,pack-relative-relocs \
                  -o build/.relr-probe 2>/dev/null && build/.relr-probe 2>/dev/null && echo -Wl,-z,pack-relative-relocs; rm -f build/.relr-probe)
ALL_LDFLAGS := $(LDFLAGS) $(RELR_LDFLAGS) -Wl,--as-needed -Wl,--gc-sections -pie -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack
ALL_LIBS := $(LDLIBS) $(shell pkg-config --libs $(PKGS)) -lpam -lpthread -lm

WL_PROTOCOLS = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
PROTOCOLS = $(WL_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml \
            $(WL_PROTOCOLS)/stable/viewporter/viewporter.xml \
            $(WL_PROTOCOLS)/staging/fractional-scale/fractional-scale-v1.xml \
            $(WL_PROTOCOLS)/staging/single-pixel-buffer/single-pixel-buffer-v1.xml \
            $(WL_PROTOCOLS)/staging/ext-session-lock/ext-session-lock-v1.xml \
            $(WL_PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml \
            $(WL_PROTOCOLS)/stable/tablet/tablet-v2.xml

PROTO_NAMES = $(basename $(notdir $(PROTOCOLS)))
PROTO_HEADERS = $(PROTO_NAMES:%=build/%-client-protocol.h)
PROTO_OBJS = $(PROTO_NAMES:%=build/%-client-protocol.o)

SRCS = $(filter-out $(if $(filter 0,$(LAYOUTS)),src/layout.c),$(wildcard src/*.c))
COMMON_SRCS = $(PACOMMON)/src/config_file.c $(PACOMMON)/src/fontfind.c $(PACOMMON)/src/field.c $(PACOMMON)/src/length.c $(PACOMMON)/src/textlayout.c $(PACOMMON)/src/palette.c $(PACOMMON)/src/speech.c
COMMON_OBJS = $(COMMON_SRCS:$(PACOMMON)/src/%.c=build/pacommon_%.o)
OBJS = $(SRCS:src/%.c=build/%.o) $(PROTO_OBJS) $(COMMON_OBJS)
TARGET = paclock

# Translations (gettext): po/LANG.po, compiled to LOCALEDIR/LANG/LC_MESSAGES/paclock.mo.
# `make pot` rewrites po/paclock.pot from the sources and merges it into every po/*.po.
POS = $(if $(filter 0,$(I18N)),,$(wildcard po/*.po))
MOS = $(POS:po/%.po=build/locale/%/LC_MESSAGES/paclock.mo)

all: $(TARGET) $(MOS)

$(TARGET): $(OBJS)
	$(CC) $(ALL_LDFLAGS) $^ -o $@ $(ALL_LIBS)

build/pacommon_%.o: $(PACOMMON)/src/%.c | $(PROTO_HEADERS)
	$(CC) $(ALL_CFLAGS) -Isrc -DPACOMMON_I18N='"i18n.h"' -c $< -o $@

build/%.o: src/%.c | $(PROTO_HEADERS)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

build/%.o: build/%.c | $(PROTO_HEADERS)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

vpath %.xml $(sort $(dir $(PROTOCOLS)))

build/%-client-protocol.h: %.xml | build
	wayland-scanner client-header $< $@

build/%-client-protocol.c: %.xml | build
	wayland-scanner private-code $< $@

build:
	mkdir -p build

# Unit tests for the code that needs no Wayland connection or PAM.
TEST_SRCS = tests/test.c src/i18n.c src/announce.c src/hush.c src/layout.c $(PACOMMON)/src/speech.c $(PACOMMON)/src/field.c src/settings.c src/pamtext.c $(PACOMMON)/src/config_file.c $(PACOMMON)/src/length.c

build/test: $(TEST_SRCS) $(wildcard src/*.h) $(MOS) | build
	$(CC) $(ALL_CFLAGS) -Isrc $(TEST_SRCS) -o $@ $(ALL_LDFLAGS) $(ALL_LIBS)

# A comma-decimal locale for the tests, made without root; no localedef, no such test.
build/locale-test/de_DE.UTF-8:
	@mkdir -p build/locale-test
	-@localedef -i de_DE -f UTF-8 build/locale-test/de_DE.UTF-8 2>/dev/null

test: build/test build/locale-test/de_DE.UTF-8
	LOCPATH=$(CURDIR)/build/locale-test build/test

build/locale/%/LC_MESSAGES/paclock.mo: po/%.po
	@mkdir -p $(dir $@)
	msgfmt --check -o $@ $<

mo: $(MOS)

pot:
	xgettext --from-code=UTF-8 --language=C --keyword=_ --keyword=N_ --add-comments=TRANSLATORS: \
	    --package-name=paclock --package-version=$(shell sed -n 's/^\#define VERSION "\(.*\)"/\1/p' src/main.c) \
	    --copyright-holder='the paclock authors' -o po/paclock.pot src/*.c $(PACOMMON)/src/config_file.c $(PACOMMON)/src/speech.c
	for po in $(POS); do msgmerge --update --backup=none $$po po/paclock.pot; done

clean:
	rm -rf build $(TARGET)

# The stack the password is checked against: Arch and Fedora call it system-auth, Debian and Ubuntu
# common-auth. Found on this machine; set PAM_STACK=... when packaging for another one.
PAM_STACK ?= $(if $(wildcard /etc/pam.d/system-auth),system-auth,$(if $(wildcard /etc/pam.d/common-auth),common-auth,system-auth))

.PHONY: build/paclock.pam
build/paclock.pam: pam.d/paclock
	@mkdir -p build
	sed 's/system-auth/$(PAM_STACK)/' $< > $@

install: $(TARGET) $(MOS) build/paclock.pam
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	$(call pac_install_keep,build/paclock.pam,$(DESTDIR)$(SYSCONFDIR)/pam.d/paclock,644)
	install -Dm644 paclock.conf $(DESTDIR)$(DOCDIR)/paclock.conf.example
	$(call pac_install_keep,paclock.conf,$(DESTDIR)$(SYSCONFDIR)/pac/paclock.conf,644)
	$(pac_install_shared_conf)
	for mo in $(MOS); do \
	    lang=$${mo#build/locale/}; lang=$${lang%%/*}; \
	    install -Dm644 $$mo $(DESTDIR)$(LOCALEDIR)/$$lang/LC_MESSAGES/paclock.mo; \
	done

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	$(call pac_uninstall_keep,pam.d/paclock,$(DESTDIR)$(SYSCONFDIR)/pam.d/paclock)
	$(call pac_uninstall_keep,paclock.conf,$(DESTDIR)$(SYSCONFDIR)/pac/paclock.conf)
	-rmdir $(DESTDIR)$(SYSCONFDIR)/pac 2>/dev/null
	rm -rf $(DESTDIR)$(DOCDIR)
	rm -f $(DESTDIR)$(LOCALEDIR)/*/LC_MESSAGES/paclock.mo

-include $(wildcard build/*.d)


# The tests again under AddressSanitizer and UBSan.
sanitize:
	$(MAKE) clean
	$(MAKE) test CFLAGS="-g -fsanitize=address,undefined $(CFLAGS)" LDFLAGS="-fsanitize=address,undefined $(LDFLAGS)"

.PHONY: sanitize mo pot all clean install uninstall test
