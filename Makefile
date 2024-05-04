PKG_CONFIG = pkg-config

PKGS = wayland-client wayland-cursor wlroots xkbcommon
INCS = `$(PKG_CONFIG) --cflags $(PKGS)`
LIBS = `$(PKG_CONFIG) --libs $(PKGS)`

WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`
WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`

CFLAGS  = -ggdb -O0 -pedantic -Wall $(INCS)
LDFLAGS = $(LIBS)

all: wlock

wlock: wlock.o single-pixel-buffer-v1-protocol.o ext-session-lock-v1-protocol.o viewporter-protocol.o
	$(CC) -o $@ wlock.o single-pixel-buffer-v1-protocol.o ext-session-lock-v1-protocol.o viewporter-protocol.o $(CFLAGS) $(LDFLAGS)

wlock.o: wlock.c single-pixel-buffer-v1-protocol.h ext-session-lock-v1-protocol.h viewporter-protocol.h
single-pixel-buffer-v1-protocol.o: single-pixel-buffer-v1-protocol.h
ext-session-lock-v1-protocol.o: ext-session-lock-v1-protocol.h
viewporter-protocol.o: viewporter-protocol.h

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

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $<

.PHONY: all clean
