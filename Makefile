CC      = gcc
CFLAGS  = -Wall -Wextra -O2 $(shell pkg-config --cflags aravis-0.8)
LIBS    = $(shell pkg-config --libs aravis-0.8) -lm
SDL2_CFLAGS = $(shell pkg-config --cflags sdl2)
SDL2_LIBS   = $(shell pkg-config --libs sdl2)
SRCDIR  = src
BINDIR  = bin

TARGETS = $(BINDIR)/connect $(BINDIR)/capture $(BINDIR)/capture_debug $(BINDIR)/stream $(BINDIR)/list_cameras

.PHONY: all clean

all: $(BINDIR) $(TARGETS)

$(BINDIR):
	mkdir -p $(BINDIR)

$(BINDIR)/connect: $(SRCDIR)/connect.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

$(BINDIR)/capture: $(SRCDIR)/capture.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)
	codesign --force --sign - $@

$(BINDIR)/capture_debug: $(SRCDIR)/capture_debug.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

$(BINDIR)/stream: $(SRCDIR)/stream.c
	$(CC) $(CFLAGS) $(SDL2_CFLAGS) -o $@ $< $(LIBS) $(SDL2_LIBS)
	codesign --force --sign - $@

$(BINDIR)/list_cameras: $(SRCDIR)/list_cameras.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

clean:
	rm -rf $(BINDIR)
