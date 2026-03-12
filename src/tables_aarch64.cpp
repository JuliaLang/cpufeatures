// Cross-arch table access for aarch64.
// Always compiled regardless of host architecture.

#include "target_tables_aarch64.h"
#include "cross_arch.h"
#include <cstring>

namespace tp::aarch64 {

bool lookup_cpu(const char *name, CrossFeatureBits &out) {
    const CPUEntry *c = find_cpu(name);
    if (!c) return false;
    std::memset(&out, 0, sizeof(out));
    out.num_words = TARGET_FEATURE_WORDS;
    for (int i = 0; i < TARGET_FEATURE_WORDS; i++)
        out.bits[i] = c->features.bits[i] & hw_feature_mask.bits[i];
    return true;
}

unsigned feature_words() { return TARGET_FEATURE_WORDS; }
unsigned nfeatures() { return num_features; }
unsigned ncpus() { return num_cpus; }

const char *feature_name_at(unsigned idx) {
    return idx < num_features ? feature_table[idx].name : nullptr;
}

int feature_bit_by_name(const char *name) {
    const FeatureEntry *fe = find_feature(name);
    return fe ? static_cast<int>(fe->bit) : -1;
}

bool feature_is_hw_by_name(const char *name) {
    const FeatureEntry *fe = find_feature(name);
    return fe && fe->is_hw;
}

const char *cpu_name_at(unsigned idx) {
    return idx < num_cpus ? cpu_table[idx].name : nullptr;
}

unsigned tables_version_major() {
#ifdef TARGET_TABLES_LLVM_VERSION_MAJOR
    return TARGET_TABLES_LLVM_VERSION_MAJOR;
#else
    return 0;
#endif
}

} // namespace tp::aarch64
