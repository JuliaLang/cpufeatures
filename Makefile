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
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
CFLAGS ?= -O2 -Wall -Wextra

# Directories
SRCDIR = src
INCDIR = include
GENDIR = generated
BUILDDIR = build

# Detect host architecture and select the right files.
# Allow override via ARCH= for cross-compilation.
ARCH ?= $(shell uname -m)

# Normalize architecture names (macOS reports arm64 instead of aarch64)
ifeq ($(ARCH),arm64)
  override ARCH := aarch64
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
  $(error Unsupported architecture: $(ARCH). Supported: x86_64, aarch64, riscv64)
endif

# Source files
LIB_SRCS = $(SRCDIR)/target_parsing.cpp $(HOST_SRC)
LIB_OBJS = $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(LIB_SRCS))

STATIC_LIB = $(BUILDDIR)/libtarget_parsing.a

.PHONY: all clean test lib info

all: lib

lib: $(STATIC_LIB)

# ============================================================================
# Library (NO LLVM dependency)
# ============================================================================

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp $(HOST_TABLE) $(INCDIR)/target_parsing.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -I$(GENDIR) -c -o $@ $<

$(STATIC_LIB): $(LIB_OBJS)
	ar rcs $@ $^

# ============================================================================
# Tests (NO LLVM dependency)
# ============================================================================

$(BUILDDIR)/test_standalone: test_standalone.cpp $(STATIC_LIB) $(HOST_TABLE)
	$(CXX) $(CXXFLAGS) -Wno-unused-function -I$(INCDIR) -I$(GENDIR) -o $@ $< -L$(BUILDDIR) -ltarget_parsing

test: $(BUILDDIR)/test_standalone
	$(BUILDDIR)/test_standalone

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
