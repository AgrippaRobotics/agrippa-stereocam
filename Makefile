CC      = gcc
CXX     ?= g++
CFLAGS  = -Wall -Wextra -O2 \
          $(shell pkg-config --cflags aravis-0.8) \
          $(shell pkg-config --cflags sdl2)
LIBS    = $(shell pkg-config --libs aravis-0.8) \
          $(shell pkg-config --libs sdl2) \
          -lz -lm

SRCDIR    = src
BINDIR    = bin
VENDORDIR = vendor

TARGET = $(BINDIR)/ag-cam-tools

SRCS = $(SRCDIR)/main.c \
       $(SRCDIR)/common.c \
       $(SRCDIR)/image.c \
       $(SRCDIR)/focus.c \
       $(SRCDIR)/focus_audio.c \
       $(SRCDIR)/cmd_connect.c \
       $(SRCDIR)/cmd_list.c \
       $(SRCDIR)/cmd_capture.c \
       $(SRCDIR)/cmd_stream.c \
       $(SRCDIR)/cmd_focus.c \
       $(SRCDIR)/cmd_calibration_capture.c \
       $(SRCDIR)/remap.c \
       $(SRCDIR)/stereo_common.c \
       $(SRCDIR)/cmd_depth_preview.c \
       $(SRCDIR)/device_file.c \
       $(SRCDIR)/calib_archive.c \
       $(SRCDIR)/cmd_calibration_stash.c

VENDOR_SRCS = $(VENDORDIR)/argtable3.c \
              $(VENDORDIR)/cJSON.c

CXX_SRCS =

OBJS        = $(patsubst $(SRCDIR)/%.c,$(BINDIR)/%.o,$(SRCS))
VENDOR_OBJS = $(patsubst $(VENDORDIR)/%.c,$(BINDIR)/%.o,$(VENDOR_SRCS))
CXX_OBJS    = $(patsubst $(SRCDIR)/%.cpp,$(BINDIR)/%.o,$(CXX_SRCS))

# --- AprilTag: prefer system install, fall back to vendor submodule ---
APRILTAG_SYSTEM_CFLAGS := $(shell pkg-config --cflags apriltag 2>/dev/null)
APRILTAG_SYSTEM_LIBS   := $(shell pkg-config --libs   apriltag 2>/dev/null)

ifneq ($(APRILTAG_SYSTEM_LIBS),)
  # System-installed apriltag found via pkg-config
  CFLAGS += $(APRILTAG_SYSTEM_CFLAGS) -DHAVE_APRILTAG=1
  LIBS   += $(APRILTAG_SYSTEM_LIBS)
else ifneq ($(wildcard $(VENDORDIR)/apriltag/apriltag.h),)
  # Vendor submodule present — build minimal static library
  CFLAGS += -I$(VENDORDIR)/apriltag -DHAVE_APRILTAG=1
  LIBS   += -lpthread

  APRILTAG_DIR  = $(VENDORDIR)/apriltag
  APRILTAG_LIB  = $(BINDIR)/libapriltag.a
  APRILTAG_SRCS = $(APRILTAG_DIR)/apriltag.c \
                  $(APRILTAG_DIR)/apriltag_quad_thresh.c \
                  $(APRILTAG_DIR)/apriltag_pose.c \
                  $(APRILTAG_DIR)/tagStandard52h13.c \
                  $(wildcard $(APRILTAG_DIR)/common/*.c)
  APRILTAG_OBJS = $(patsubst $(APRILTAG_DIR)/%.c,$(BINDIR)/apriltag/%.o,$(APRILTAG_SRCS))
endif

# --- OpenCV: optional, provides StereoSGBM backend ---
OPENCV_CFLAGS := $(shell pkg-config --cflags opencv4 2>/dev/null)
OPENCV_LIBS   := $(shell pkg-config --libs   opencv4 2>/dev/null)

ifneq ($(OPENCV_LIBS),)
  CFLAGS   += $(OPENCV_CFLAGS) -DHAVE_OPENCV=1
  LIBS     += $(OPENCV_LIBS)
  CXX_SRCS += $(SRCDIR)/stereo_sgbm.cpp
  CXX_OBJS  = $(patsubst $(SRCDIR)/%.cpp,$(BINDIR)/%.o,$(CXX_SRCS))
  # C++ standard library required when linking a mixed C/C++ binary
  LIBS     += -lstdc++
endif

# --- ONNX Runtime: optional, provides in-process neural stereo backend ---
ONNXRT_CFLAGS := $(shell pkg-config --cflags libonnxruntime 2>/dev/null || \
                          pkg-config --cflags onnxruntime 2>/dev/null)
ONNXRT_LIBS   := $(shell pkg-config --libs   libonnxruntime 2>/dev/null || \
                          pkg-config --libs   onnxruntime 2>/dev/null)

ifdef ONNXRUNTIME_HOME
  ONNXRT_CFLAGS := -I$(ONNXRUNTIME_HOME)/include
  ONNXRT_LIBS   := -L$(ONNXRUNTIME_HOME)/lib -Wl,-rpath,$(ONNXRUNTIME_HOME)/lib -lonnxruntime
endif

ifneq ($(ONNXRT_LIBS),)
  CFLAGS += $(ONNXRT_CFLAGS) -DHAVE_ONNXRUNTIME=1
  LIBS   += $(ONNXRT_LIBS)
  SRCS   += $(SRCDIR)/stereo_onnx.c
endif

PREFIX     ?= /usr/local
BASHCOMPDIR ?= $(PREFIX)/share/bash-completion/completions
ZSHCOMPDIR  ?= $(PREFIX)/share/zsh/site-functions

.PHONY: all clean install uninstall test test-hw test-all

all: $(BINDIR) $(TARGET)

$(BINDIR):
	mkdir -p $(BINDIR)

$(BINDIR)/%.o: $(SRCDIR)/%.c | $(BINDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# C++ compilation rule (stereo_sgbm.cpp)
$(BINDIR)/%.o: $(SRCDIR)/%.cpp | $(BINDIR)
	$(CXX) $(CFLAGS) -std=c++11 -c -o $@ $<

$(BINDIR)/argtable3.o: $(VENDORDIR)/argtable3.c | $(BINDIR)
	$(CC) -Wall -O2 -c -o $@ $<

$(BINDIR)/cJSON.o: $(VENDORDIR)/cJSON.c | $(BINDIR)
	$(CC) -Wall -O2 -c -o $@ $<

# AprilTag vendor object compilation
$(BINDIR)/apriltag/%.o: $(APRILTAG_DIR)/%.c | $(BINDIR)
	@mkdir -p $(dir $@)
	$(CC) -Wall -O2 -I$(APRILTAG_DIR) -c -o $@ $<

# AprilTag vendor static library
$(APRILTAG_LIB): $(APRILTAG_OBJS)
	$(AR) rcs $@ $^

$(TARGET): $(OBJS) $(VENDOR_OBJS) $(CXX_OBJS) $(APRILTAG_LIB)
	$(CC) -o $@ $(OBJS) $(VENDOR_OBJS) $(CXX_OBJS) $(if $(APRILTAG_LIB),-L$(BINDIR) -lapriltag) $(LIBS)
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

# ---- Unit Tests (no hardware required) --------------------------------

TESTDIR = tests

# Compilation flags for tests: need aravis headers (common.h includes
# <arv.h>) but unit tests only link against glib + zlib — no aravis libs.
TEST_CFLAGS = -Wall -Wextra -O2 -g \
              $(shell pkg-config --cflags aravis-0.8) \
              -I$(SRCDIR) -I$(VENDORDIR)
TEST_LIBS   = $(shell pkg-config --libs glib-2.0) -lz -lm

# Object files needed by unit tests (no main.o, no cmd_*.o).
TEST_OBJS = $(BINDIR)/remap.o $(BINDIR)/calib_archive.o $(BINDIR)/cJSON.o

$(BINDIR)/test_calib_archive: $(TESTDIR)/test_calib_archive.c $(TEST_OBJS) | $(BINDIR)
	$(CC) $(TEST_CFLAGS) -o $@ $< $(TEST_OBJS) $(TEST_LIBS)

$(BINDIR)/test_remap: $(TESTDIR)/test_remap.c $(BINDIR)/remap.o | $(BINDIR)
	$(CC) $(TEST_CFLAGS) -o $@ $< $(BINDIR)/remap.o $(TEST_LIBS)

$(BINDIR)/gen_test_calibration: $(TESTDIR)/gen_test_calibration.c | $(BINDIR)
	$(CC) -Wall -O2 -o $@ $<

test: $(BINDIR)/test_calib_archive $(BINDIR)/test_remap
	@echo "=== Unit Tests ==="
	$(BINDIR)/test_calib_archive
	$(BINDIR)/test_remap

# ---- Hardware Integration Tests (camera required) ---------------------

test-hw: $(TARGET) $(BINDIR)/gen_test_calibration
	@echo "=== Hardware Integration Tests ==="
	$(TESTDIR)/test_stash_hw.sh

test-all: test test-hw

clean:
	rm -rf $(BINDIR)
