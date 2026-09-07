// Compile-only check of the fallback target tables.
//
// The fallback header is selected for every architecture that has no generated
// table, i.e. everything but x86_64/aarch64/riscv64 — none of which CI has a
// runner for. Consumers reach the tables through `tp::`, so this exercises the
// qualified spelling of the names they use.

#include "target_tables_fallback.h"
#include "target_parsing.h"

void check_fallback_tables()
{
    tp::FeatureBits bits = {};
    for (unsigned i = 0; i < TARGET_FEATURE_WORDS; i++)
        bits.bits[i] &= tp::llvm_feature_mask.bits[i];

    const tp::FeatureEntry *fe = tp::find_feature("");
    const tp::CPUEntry *ce = tp::find_cpu("");
    (void)fe;
    (void)ce;

    for (unsigned i = 0; i < tp::num_features; i++)
        (void)tp::feature_table[i].name;
    for (unsigned i = 0; i < tp::num_cpus; i++)
        (void)tp::cpu_table[i].name;

    (void)tp::feature_test(&bits, 0);
    (void)tp::has_feature(bits, "");
}
