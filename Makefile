.POSIX:

VERSION = 0

PREFIX = /usr/local

PKG_CONFIG = pkg-config

PKGS       = wayland-client xkbcommon
WLCPPFLAGS = -DVERSION=\"$(VERSION)\"
WLCFLAGS   = `$(PKG_CONFIG) --cflags $(PKGS)` $(WLCPPFLAGS) $(CFLAGS)
LDLIBS     = `$(PKG_CONFIG) --libs $(PKGS)` -lcrypt

SRC = wlock.c single-pixel-buffer-v1-protocol.c ext-session-lock-v1-protocol.c viewporter-protocol.c
OBJ = $(SRC:.c=.o)

all: wlock

wlock: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDLIBS) $(LDFLAGS) $(WLCFLAGS)

wlock.o: wlock.c single-pixel-buffer-v1-protocol.h ext-session-lock-v1-protocol.h viewporter-protocol.h
single-pixel-buffer-v1-protocol.o: single-pixel-buffer-v1-protocol.h
ext-session-lock-v1-protocol.o: ext-session-lock-v1-protocol.h
viewporter-protocol.o: viewporter-protocol.h

WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`
WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`

single-pixel-buffer-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/staging/single-pixel-buffer/single-pixel-buffer-v1.xml $@
single-pixel-buffer-v1-protocol.h:
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/staging/single-pixel-buffer/single-pixel-buffer-v1.xml $@
ext-session-lock-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/staging/ext-session-lock/ext-session-lock-v1.xml $@
ext-session-lock-v1-protocol.h:
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/staging/ext-session-lock/ext-session-lock-v1.xml $@
viewporter-protocol.c:
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/stable/viewporter/viewporter.xml $@
viewporter-protocol.h:
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/stable/viewporter/viewporter.xml $@

clean:
	rm -f wlock *.o *-protocol.*

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f wlock $(DESTDIR)$(PREFIX)/bin
	chmod 4755 $(DESTDIR)$(PREFIX)/bin/wlock

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/wlock

.SUFFIXES: .c .o
.c.o:
	$(CC) -c $< $(CPPFLAGS) $(WLCFLAGS)

.PHONY: all clean install uninstall
