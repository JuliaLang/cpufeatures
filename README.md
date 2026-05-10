# cpufeautures 

cpufeatures is a library that does two things. It inspects the host to see what kind of cpu they have, and also has mappings from cpu names to lists of features.
The feature tables come directly from LLVM to ensure that the names are compatible.
This library was developed to simplify the handling of cpufeatures in Julia

## Development
`Makefile` has build and test targets for development of the library itself

### Building and testing

`Makefile` has build and test targets for development of the library itself:

```
make -j
make test
```

### Updating features tables

`Makefile.generate` downloads a build of LLVM from LLVMs own releases and regenerates the tables, based on host platform (supported systems: Linux and macOS, with x86-64 and aarch64 architectures).

```
make -f Makefile.generate
```

## Compatibility
Currently supports x86, aarch64 and RISCV on windows and unixes
