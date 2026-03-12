// Cross-architecture dispatch.
// Routes queries to the appropriate per-arch table wrapper.

#include "cross_arch.h"
#include <cstring>

// Per-arch table accessors (defined in tables_*.cpp)
namespace tp::x86_64 {
    bool lookup_cpu(const char *name, CrossFeatureBits &out);
    unsigned feature_words();
    unsigned nfeatures();
    unsigned ncpus();
    const char *feature_name_at(unsigned idx);
    int feature_bit_by_name(const char *name);
    bool feature_is_hw_by_name(const char *name);
    const char *cpu_name_at(unsigned idx);
    unsigned tables_version_major();
}

namespace tp::aarch64 {
    bool lookup_cpu(const char *name, CrossFeatureBits &out);
    unsigned feature_words();
    unsigned nfeatures();
    unsigned ncpus();
    const char *feature_name_at(unsigned idx);
    int feature_bit_by_name(const char *name);
    bool feature_is_hw_by_name(const char *name);
    const char *cpu_name_at(unsigned idx);
    unsigned tables_version_major();
}

namespace tp::riscv64 {
    bool lookup_cpu(const char *name, CrossFeatureBits &out);
    unsigned feature_words();
    unsigned nfeatures();
    unsigned ncpus();
    const char *feature_name_at(unsigned idx);
    int feature_bit_by_name(const char *name);
    bool feature_is_hw_by_name(const char *name);
    const char *cpu_name_at(unsigned idx);
    unsigned tables_version_major();
}

namespace tp {

// Normalize arch name variants
static const char *normalize_arch(const char *arch) {
    if (!arch) return nullptr;
    if (std::strcmp(arch, "x86_64") == 0 || std::strcmp(arch, "x86-64") == 0 ||
        std::strcmp(arch, "i686") == 0 || std::strcmp(arch, "i386") == 0)
        return "x86_64";
    if (std::strcmp(arch, "aarch64") == 0 || std::strcmp(arch, "arm64") == 0)
        return "aarch64";
    if (std::strcmp(arch, "riscv64") == 0)
        return "riscv64";
    return arch;
}

// Dispatch macro to avoid repetition
#define DISPATCH(arch_str, func, ...) do { \
    const char *a = normalize_arch(arch_str); \
    if (!a) return {}; \
    if (std::strcmp(a, "x86_64") == 0)  return x86_64::func(__VA_ARGS__); \
    if (std::strcmp(a, "aarch64") == 0) return aarch64::func(__VA_ARGS__); \
    if (std::strcmp(a, "riscv64") == 0) return riscv64::func(__VA_ARGS__); \
} while(0)

bool cross_lookup_cpu(const char *arch, const char *cpu_name,
                      CrossFeatureBits &features_out) {
    std::memset(&features_out, 0, sizeof(features_out));
    DISPATCH(arch, lookup_cpu, cpu_name, features_out);
    return false;
}

unsigned cross_feature_words(const char *arch) {
    DISPATCH(arch, feature_words);
    return 0;
}

unsigned cross_num_features(const char *arch) {
    DISPATCH(arch, nfeatures);
    return 0;
}

unsigned cross_num_cpus(const char *arch) {
    DISPATCH(arch, ncpus);
    return 0;
}

const char *cross_feature_name(const char *arch, unsigned idx) {
    DISPATCH(arch, feature_name_at, idx);
    return nullptr;
}

int cross_feature_bit(const char *arch, const char *name) {
    DISPATCH(arch, feature_bit_by_name, name);
    return -1;
}

bool cross_feature_is_hw(const char *arch, const char *name) {
    DISPATCH(arch, feature_is_hw_by_name, name);
    return false;
}

const char *cross_cpu_name(const char *arch, unsigned idx) {
    DISPATCH(arch, cpu_name_at, idx);
    return nullptr;
}

unsigned cross_tables_version_major(const char *arch) {
    DISPATCH(arch, tables_version_major);
    return 0;
}

#undef DISPATCH

} // namespace tp
