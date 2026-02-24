CC      = gcc
CFLAGS  = -Wall -Wextra -O2 \
          $(shell pkg-config --cflags aravis-0.8) \
          $(shell pkg-config --cflags sdl2)
LIBS    = $(shell pkg-config --libs aravis-0.8) \
          $(shell pkg-config --libs sdl2) \
          -lm

SRCDIR    = src
BINDIR    = bin
VENDORDIR = vendor

TARGET = $(BINDIR)/ag-cam-tools

SRCS = $(SRCDIR)/main.c \
       $(SRCDIR)/common.c \
       $(SRCDIR)/image.c \
       $(SRCDIR)/cmd_connect.c \
       $(SRCDIR)/cmd_list.c \
       $(SRCDIR)/cmd_capture.c \
       $(SRCDIR)/cmd_stream.c

VENDOR_SRCS = $(VENDORDIR)/argtable3.c

OBJS        = $(patsubst $(SRCDIR)/%.c,$(BINDIR)/%.o,$(SRCS))
VENDOR_OBJS = $(patsubst $(VENDORDIR)/%.c,$(BINDIR)/%.o,$(VENDOR_SRCS))

PREFIX     ?= /usr/local
BASHCOMPDIR ?= $(PREFIX)/share/bash-completion/completions
ZSHCOMPDIR  ?= $(PREFIX)/share/zsh/site-functions

.PHONY: all clean install uninstall

all: $(BINDIR) $(TARGET)

$(BINDIR):
	mkdir -p $(BINDIR)

$(BINDIR)/%.o: $(SRCDIR)/%.c | $(BINDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BINDIR)/argtable3.o: $(VENDORDIR)/argtable3.c | $(BINDIR)
	$(CC) -Wall -O2 -c -o $@ $<

$(TARGET): $(OBJS) $(VENDOR_OBJS)
	$(CC) -o $@ $^ $(LIBS)
	codesign --force --sign - $@ 2>/dev/null || true

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/
	install -d $(DESTDIR)$(BASHCOMPDIR)
	install -m 644 completions/ag-cam-tools.bash $(DESTDIR)$(BASHCOMPDIR)/ag-cam-tools
	install -d $(DESTDIR)$(ZSHCOMPDIR)
	install -m 644 completions/ag-cam-tools.zsh $(DESTDIR)$(ZSHCOMPDIR)/_ag-cam-tools

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/ag-cam-tools
	rm -f $(DESTDIR)$(BASHCOMPDIR)/ag-cam-tools
	rm -f $(DESTDIR)$(ZSHCOMPDIR)/_ag-cam-tools

clean:
	rm -rf $(BINDIR)
