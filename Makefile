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
AR ?= ar
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -fno-exceptions -fno-rtti
CFLAGS ?= -O2 -Wall -Wextra
DEPFLAGS ?= -MMD -MP

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
ifeq ($(ARCH),amd64)
  override ARCH := x86_64
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

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -I$(INCDIR) -I$(GENDIR) -c -o $@ $<

$(STATIC_LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^

# ============================================================================
# Tests (NO LLVM dependency)
# ============================================================================

$(BUILDDIR)/test_standalone$(EXE): test_standalone.cpp $(STATIC_LIB) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -I$(INCDIR) -I$(GENDIR) -o $@ $< -L$(BUILDDIR) -ltarget_parsing $(TEST_LDFLAGS)

test: $(BUILDDIR)/test_standalone$(EXE)
	$(BUILDDIR)/test_standalone$(EXE)

# ============================================================================
# Coverage build & report (NO LLVM dependency)
#
# Builds with gcov-style instrumentation (--coverage works for both gcc
# and clang) into a separate build-cov/ tree, runs the test, and asks
# gcovr for a summary. Install gcovr with: pip install gcovr.
# `make coverage-lcov` additionally writes coverage.lcov for upload to
# services like Codecov.
# ============================================================================

COV_BUILDDIR := build-cov
COV_CXXFLAGS := -std=c++17 -O0 -g -Wall -Wextra -fno-exceptions -fno-rtti --coverage
# Don't count the test driver itself as covered code — we want coverage
# of the library, not of the test.
GCOVR_FILTERS := --filter '^src/' --filter '^include/' --exclude 'test_standalone\.cpp'

.PHONY: coverage coverage-lcov
coverage:
	@$(MAKE) BUILDDIR=$(COV_BUILDDIR) CXXFLAGS='$(COV_CXXFLAGS)' test
	@echo ""
	@echo "=== Coverage summary ==="
	@if command -v gcovr >/dev/null 2>&1; then \
		gcovr --root . --object-directory $(COV_BUILDDIR) $(GCOVR_FILTERS) --print-summary; \
	else \
		echo "gcovr not installed. Install: pip install gcovr"; \
		echo "Raw .gcda/.gcno files are under $(COV_BUILDDIR)/"; \
	fi

coverage-lcov: coverage
	gcovr --root . --object-directory $(COV_BUILDDIR) $(GCOVR_FILTERS) --lcov coverage.lcov
	@echo "Wrote coverage.lcov"

# ============================================================================
# Directories & clean
# ============================================================================

$(BUILDDIR):
	mkdir -p $@

clean:
	rm -rf $(BUILDDIR) $(COV_BUILDDIR) coverage.lcov coverage.xml

info:
	@echo "Architecture: $(ARCH)"
	@echo "Host table:   $(HOST_TABLE)"
	@echo "Library:      $(STATIC_LIB)"

# include build-generated header dependency files
-include $(LIB_OBJS:.o=.d) $(BUILDDIR)/test_standalone.d
