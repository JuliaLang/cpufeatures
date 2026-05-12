# cpufeautures

cpufeatures is a library that does two things. It inspects the host to see what kind of cpu they have, and also has mappings from cpu names to lists of features.
The feature tables come directly from LLVM to ensure that the names are compatible.
This library was developed to simplify the handling of CPU features in Julia.

## Development
`Makefile` has build and test targets for development of the library itself

### Building and testing

`Makefile` has build and test targets for development of the library itself:

```
make -j
make test
```

### Code coverage

`make coverage` rebuilds the library and the test under gcov-style
instrumentation (`--coverage`) into a separate `build-cov/` tree, runs
the test, and (if [`gcovr`](https://gcovr.com/) is installed) prints a
per-file summary:

```
make coverage
```

`make coverage-lcov` additionally writes an LCOV report to
`coverage.lcov` for upload to services like
[Codecov](https://about.codecov.io/). The CI runs this on both Linux
x86_64 and Linux aarch64 and uploads the merged result.

Install `gcovr` with:

```
pip install gcovr
```

### Updating feature tables

`Makefile.generate` downloads a build of LLVM from LLVMs own releases and regenerates the tables, based on host platform (supported systems: Linux and macOS, with x86-64 and aarch64 architectures).

```
make -f Makefile.generate
```

To use an existing LLVM installation instead of downloading one:

```
make -f Makefile.generate LLVM_PREFIX=/path/to/llvm
```

The version is controlled by `LLVM_VER` at the top of `Makefile.generate`. Update it to the desired LLVM release tag before running.

The generated headers in `generated/` are committed to the repository. Normal builds do not require LLVM to be installed.

### Handling new features after an LLVM upgrade

After regenerating the tables, run the test suite:

```
make test
```

The test enforces that every hardware feature (`is_hw=1`, `is_featureset=0`, `is_privileged=0`) in the generated table is assigned to exactly one of four categories in the relevant `src/host_<arch>.cpp` file:

- **baseline** — always present, mandated by the platform ABI (e.g. `neon` on Windows AArch64)
- **detectable** — has a runtime probe implemented in the host file
- **undetectable** — exists in LLVM but has no runtime probe; unsafe to enable without explicit opt-in
- **featureset** — groups other features with no probe of its own (handled by the generator; skipped by the test)

When new features appear after an LLVM upgrade, the test will print:

```
FAIL: HW feature '<name>' is unhandled
```

For each unhandled feature, determine which category it belongs to and add it to the appropriate list in the host file for the affected architecture.

#### Deciding the category

**Featureset / privileged** — check the generated header (`generated/target_tables_<arch>.h`). If the entry has `is_featureset=1` or `is_privileged=1`, no runtime probe is required.
If a "feature" is an alias for multiple other HW bits and does not gate any HW functionality of its own (e.g. "+zk" is an alias for "+zkn,+zkr,+zkt" on RISC-V),
then it should be labeled as `is_featureset = 1`.
If a feature is not correctly set as `is_featureset` or `is_privileged`, this likely indicates the generator itself needs updating.

**Detectable** — check whether the OS exposes a runtime probe for the feature (see the reference links per architecture below).
If a probe exists, add the feature to the appropriate probe map in the host file so it is enabled or disabled at runtime.

**Undetectable** — if no runtime probe exists on a given platform, add the feature name to that platform's `HOST_FEATURE_UNDETECTABLE` list.
This documents the gap explicitly and makes the feature unavailable for runtime execution on that platform.

**Baseline** — if the feature is guaranteed present by the platform ABI on a specific OS (without needing a probe), add it to `HOST_FEATURE_BASELINE` for that platform instead.

#### Runtime probe references by architecture

**x86 / x86_64** (`src/host_x86.cpp`)
- Detection uses CPUID leaves. The Intel and AMD software developer manuals list which CPUID leaf and bit corresponds to each feature.
- [Intel® 64 and IA-32 Architectures Software Developer’s Manual](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html), volume 2A, Chapter 3 "Instruction Set Reference, A-L", and following.
- [AMD64 Architecture Programmer’s Manual Volume 3: General-Purpose and System Instructions](https://docs.amd.com/v/u/en-US/24594_3.37), Appendix D: Instruction Subsets and CPUID Feature Flags

**AArch64** (`src/host_aarch64.cpp`)

- **Linux** — `getauxval(AT_HWCAP)` / `AT_HWCAP2` / `AT_HWCAP3` bits defined in:
  [`arch/arm64/include/uapi/asm/hwcap.h`](https://github.com/torvalds/linux/blob/master/arch/arm64/include/uapi/asm/hwcap.h)
- **macOS** — `CAP_BIT_*` constants queried via `sysctlbyname("hw.optional.arm.caps", ...)`, defined in:
  [`osfmk/arm/cpu_capabilities_public.h`](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/arm/cpu_capabilities_public.h)
- **Windows** — `IsProcessorFeaturePresent(PF_ARM_*)` constants defined in `processthreadsapi.h`:
  [MSDN: IsProcessorFeaturePresent](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-isprocessorfeaturepresent)

**RISC-V** (`src/host_riscv.cpp`)
- Detection uses the `riscv_hwprobe` syscall (`RISCV_HWPROBE_KEY_IMA_EXT_0` and related keys). Extension bits are defined in:
  [`arch/riscv/include/uapi/asm/hwprobe.h`](https://github.com/torvalds/linux/blob/master/arch/riscv/include/uapi/asm/hwprobe.h)

## Compatibility

Currently supports x86, aarch64 and RISCV on Windows and Unix systems.
