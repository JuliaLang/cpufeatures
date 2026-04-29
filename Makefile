# Target Parsing Library
# Standalone CPU/feature database from LLVM's TableGen data.
# No LLVM runtime dependency - generated tables are committed to the repo.
#
# Normal build (no LLVM needed):
#   make
#   make test
#
# Regenerate tables (requires LLVM):
#   make -f Makefile.generate
#   # then commit the updated generated/ files

CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -fno-exceptions -fno-rtti
CFLAGS ?= -O2 -Wall -Wextra

TARGET_OS := $(shell $(CXX) -dumpmachine 2>/dev/null)
ifneq (,$(findstring mingw,$(TARGET_OS))$(findstring cygwin,$(TARGET_OS)))
  EXE := .exe
  TEST_LDFLAGS := -static-libgcc -static-libstdc++
endif
ifeq ($(OS),Windows_NT)
  EXE := .exe
endif

# Directories
SRCDIR = src
INCDIR = include
GENDIR = generated
BUILDDIR = build

# Detect host architecture and select the right files.
# Allow override via ARCH= for cross-compilation.
ARCH ?= $(shell uname -m)

# Normalize architecture names
ifeq ($(ARCH),arm64)
  override ARCH := aarch64
endif
ifeq ($(ARCH),i686)
  override ARCH := x86_64
endif
ifeq ($(ARCH),i386)
  override ARCH := x86_64
endif

ifeq ($(ARCH),x86_64)
  HOST_SRC = $(SRCDIR)/host_x86.cpp
  HOST_TABLE = $(GENDIR)/target_tables_x86_64.h
else ifeq ($(ARCH),aarch64)
  HOST_SRC = $(SRCDIR)/host_aarch64.cpp
  HOST_TABLE = $(GENDIR)/target_tables_aarch64.h
else ifeq ($(ARCH),riscv64)
  HOST_SRC = $(SRCDIR)/host_riscv.cpp
  HOST_TABLE = $(GENDIR)/target_tables_riscv64.h
else
  HOST_SRC = $(SRCDIR)/host_fallback.cpp
  HOST_TABLE = $(GENDIR)/target_tables_fallback.h
endif

# All generated table headers
ALL_TABLES = $(GENDIR)/target_tables_x86_64.h \
             $(GENDIR)/target_tables_aarch64.h \
             $(GENDIR)/target_tables_riscv64.h

# Source files: host-specific + target parsing + cross-arch tables (all arches)
CROSS_SRCS = $(SRCDIR)/tables_x86_64.cpp \
             $(SRCDIR)/tables_aarch64.cpp \
             $(SRCDIR)/tables_riscv64.cpp \
             $(SRCDIR)/cross_arch.cpp

LIB_SRCS = $(SRCDIR)/target_parsing.cpp $(HOST_SRC) $(CROSS_SRCS)
LIB_OBJS = $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(LIB_SRCS))

STATIC_LIB = $(BUILDDIR)/libtarget_parsing.a

.PHONY: all clean test lib info

all: lib

lib: $(STATIC_LIB)

# ============================================================================
# Library (NO LLVM dependency)
# ============================================================================

# Host-specific files depend on the host table
$(BUILDDIR)/target_parsing.o: $(SRCDIR)/target_parsing.cpp $(HOST_TABLE) $(INCDIR)/target_parsing.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -I$(GENDIR) -c -o $@ $<

$(BUILDDIR)/host_%.o: $(SRCDIR)/host_%.cpp $(HOST_TABLE) $(INCDIR)/target_parsing.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -I$(GENDIR) -c -o $@ $<

# Per-arch table files each depend on their own generated header
$(BUILDDIR)/tables_x86_64.o: $(SRCDIR)/tables_x86_64.cpp $(GENDIR)/target_tables_x86_64.h $(INCDIR)/cross_arch.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -I$(GENDIR) -c -o $@ $<

$(BUILDDIR)/tables_aarch64.o: $(SRCDIR)/tables_aarch64.cpp $(GENDIR)/target_tables_aarch64.h $(INCDIR)/cross_arch.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -I$(GENDIR) -c -o $@ $<

$(BUILDDIR)/tables_riscv64.o: $(SRCDIR)/tables_riscv64.cpp $(GENDIR)/target_tables_riscv64.h $(INCDIR)/cross_arch.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -I$(GENDIR) -c -o $@ $<

$(BUILDDIR)/cross_arch.o: $(SRCDIR)/cross_arch.cpp $(INCDIR)/cross_arch.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -I$(GENDIR) -c -o $@ $<

$(STATIC_LIB): $(LIB_OBJS)
	ar rcs $@ $^

# ============================================================================
# Tests (NO LLVM dependency)
# ============================================================================

$(BUILDDIR)/test_standalone$(EXE): test_standalone.cpp $(STATIC_LIB) $(HOST_TABLE) $(INCDIR)/cross_arch.h
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -I$(GENDIR) -o $@ $< -L$(BUILDDIR) -ltarget_parsing $(TEST_LDFLAGS)

test: $(BUILDDIR)/test_standalone$(EXE)
	$(BUILDDIR)/test_standalone$(EXE)

# ============================================================================
# Directories & clean
# ============================================================================

$(BUILDDIR):
	mkdir -p $@

clean:
	rm -rf $(BUILDDIR)

info:
	@echo "Architecture: $(ARCH)"
	@echo "Host table:   $(HOST_TABLE)"
	@echo "Library:      $(STATIC_LIB)"
