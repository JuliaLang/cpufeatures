// Cross-architecture CPU/feature table queries.
// Allows looking up any architecture's data regardless of host.

#ifndef CROSS_ARCH_H
#define CROSS_ARCH_H

#include <cstdint>
#include <cstddef>

namespace tp {

// Maximum feature words across all architectures (aarch64/riscv = 5, x86 = 4)
constexpr size_t MAX_FEATURE_WORDS = 5;

// Cross-arch query results
struct CrossFeatureBits {
    uint64_t bits[MAX_FEATURE_WORDS];
    unsigned num_words;  // actual words used (4 for x86, 5 for aarch64/riscv)
};

// Look up a CPU's hardware features by architecture and name.
// Returns true if found, false otherwise.
// features_out is zeroed and filled with hw-masked features.
bool cross_lookup_cpu(const char *arch, const char *cpu_name,
                      CrossFeatureBits &features_out);

// Get the number of feature words for an architecture.
// Returns 0 if architecture is unknown.
unsigned cross_feature_words(const char *arch);

// Get the number of features for an architecture.
unsigned cross_num_features(const char *arch);

// Get the number of CPUs for an architecture.
unsigned cross_num_cpus(const char *arch);

// Get a feature name by table index for an architecture.
// Returns nullptr if out of range or unknown arch.
const char *cross_feature_name(const char *arch, unsigned idx);

// Get a feature's bit index by table index for an architecture.
// Returns -1 if out of range or unknown arch.
int cross_feature_bit_at(const char *arch, unsigned idx);

// Get a feature's bit index by name for an architecture.
// Returns -1 if not found.
int cross_feature_bit(const char *arch, const char *name);

// Check if a feature is a hardware feature (vs tuning hint).
bool cross_feature_is_hw(const char *arch, const char *name);

// Get a CPU name by index for an architecture.
const char *cross_cpu_name(const char *arch, unsigned idx);

// Get the major version of the compiler toolchain the tables were generated from.
// Returns 0 if unknown. All arches should have the same version.
unsigned cross_tables_version_major(const char *arch);

} // namespace tp

#endif // CROSS_ARCH_H
