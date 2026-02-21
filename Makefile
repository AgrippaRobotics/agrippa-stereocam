CC      = gcc
CFLAGS  = -Wall -Wextra -O2 $(shell pkg-config --cflags aravis-0.8)
LIBS    = $(shell pkg-config --libs aravis-0.8)
SRCDIR  = src
BINDIR  = bin

TARGETS = $(BINDIR)/connect $(BINDIR)/capture $(BINDIR)/capture_debug

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

clean:
	rm -rf $(BINDIR)
