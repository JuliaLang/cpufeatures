// Fallback target tables for unsupported architectures.
// Provides no-op stubs so the library compiles but reports no features.
// Auto-generated headers exist for: x86_64, aarch64, riscv64.

#ifndef TARGET_TABLES_FALLBACK_H
#define TARGET_TABLES_FALLBACK_H

#include <cstdint>

#define TARGET_FEATURE_WORDS 1

typedef struct { uint64_t bits[1]; } FeatureBits;

static inline int feature_test(const FeatureBits *, unsigned) { return 0; }
static inline void feature_set(FeatureBits *, unsigned) {}
static inline void feature_clear(FeatureBits *, unsigned) {}
static inline int feature_any(const FeatureBits *) { return 0; }
static inline void feature_and_out(FeatureBits *, const FeatureBits *, const FeatureBits *) {}
static inline void feature_or(FeatureBits *, const FeatureBits *) {}
static inline void feature_andnot(FeatureBits *, const FeatureBits *, const FeatureBits *) {}
static inline int feature_popcount(const FeatureBits *) { return 0; }

static const unsigned num_features = 0;
static const unsigned num_cpus = 0;

typedef struct {
    const char *name;
    const char *desc;
    unsigned bit;
    unsigned char is_hw;
    FeatureBits implies;
} FeatureEntry;

typedef struct {
    const char *name;
    FeatureBits implies;
    FeatureBits tune_implies;
    FeatureBits features;
} CPUEntry;

static const FeatureEntry feature_table[] = {{nullptr, nullptr, 0, 0, {{0}}}};
static const CPUEntry cpu_table[] = {{nullptr, {{0}}, {{0}}, {{0}}}};

static inline const FeatureEntry *find_feature(const char *) { return nullptr; }
static inline const CPUEntry *_find_cpu_exact(const char *) { return nullptr; }
static inline void expand_implied(FeatureBits *) {}

static const FeatureBits hw_feature_mask = {{0}};

#endif // TARGET_TABLES_FALLBACK_H
