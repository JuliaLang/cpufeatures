# cpufeautures 

cpufeatures is a library that does two things. It inspects the host to see what kind of cpu they have, and also has mappings from cpu names to lists of features.
The feature tables come directly from LLVM to ensure that the names are compatible.
This library was developed to simplify the handling of cpufeatures in Julia

## Development
`Makefile` has build and test targets for development of the library itself

`Makefile.generate` downloads a x86-linux build of LLVM from LLVMs own releases and regenerates the tables.

## Compatibility
Currently supports x86, aarch64 and RISCV on windows and unixes
