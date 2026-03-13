// Test the standalone target parsing library - NO LLVM dependency!

// Include the right table for the host architecture
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include "target_tables_x86_64.h"
#elif defined(__aarch64__) || defined(_M_ARM64)
#include "target_tables_aarch64.h"
#elif defined(__riscv) && __riscv_xlen == 64
#include "target_tables_riscv64.h"
#endif
#include "target_parsing.h"
#include "cross_arch.h"

#include <cstdio>

static void print_hw_features(const FeatureBits *bits) {
    int first = 1;
    for (unsigned i = 0; i < num_features; i++) {
        if (feature_test(bits, feature_table[i].bit)) {
            if (!first) printf(", ");
            printf("%s", feature_table[i].name);
            first = 0;
        }
    }
    printf(" (%u total)\n", feature_popcount(bits));
}

static void test_parse(const char *target_str) {
    printf("\n--- Parsing: \"%s\" ---\n", target_str);
    auto parsed = tp::parse_target_string(target_str);
    printf("  %zu target(s) parsed\n", parsed.size());

    for (size_t i = 0; i < parsed.size(); i++) {
        printf("  [%zu] cpu=%s", i, parsed[i].cpu_name.c_str());
        if (parsed[i].flags) printf(" flags=0x%x", parsed[i].flags);
        if (parsed[i].base >= 0) printf(" base=%d", parsed[i].base);
        if (!parsed[i].extra_features.empty()) {
            printf(" features={");
            for (size_t j = 0; j < parsed[i].extra_features.size(); j++) {
                if (j) printf(",");
                printf("%s", parsed[i].extra_features[j].c_str());
            }
            printf("}");
        }
        printf("\n");
    }

    // Resolve
    auto resolved = tp::resolve_targets(parsed);

    for (size_t i = 0; i < resolved.size(); i++) {
        printf("  Target %zu: cpu=%s base=%d flags=0x%x\n",
               i, resolved[i].cpu_name.c_str(), resolved[i].base, resolved[i].flags);
        printf("    features: ");
        print_hw_features(&resolved[i].features);
        if (!resolved[i].ext_features.empty())
            printf("    ext: %s\n", resolved[i].ext_features.c_str());
    }

    // Get specs
    auto specs = tp::get_target_specs(resolved);
    for (size_t i = 0; i < specs.size(); i++) {
        printf("  Spec %zu: cpu=%s features=\"%s\"\n",
               i, specs[i].cpu_name.c_str(), specs[i].cpu_features.c_str());
    }
}

int main() {
    printf("=== Standalone Target Parsing Library Test ===\n");
    printf("No LLVM runtime dependency!\n");
    printf("Database: %u features, %u CPUs\n\n", num_features, num_cpus);

    // Host detection
    printf("Host CPU: %s\n", tp::get_host_cpu_name().c_str());
    auto host_features = tp::get_host_features();
    printf("Host features: ");
    print_hw_features(&host_features);

    // Check a few specific features
    printf("\nHost has AVX2: %s\n", tp::has_feature(host_features, "avx2") ? "yes" : "no");
    printf("Host has AVX512F: %s\n", tp::has_feature(host_features, "avx512f") ? "yes" : "no");
    printf("Host has SSE4.2: %s\n", tp::has_feature(host_features, "sse4.2") ? "yes" : "no");

    // CPU lookups
    printf("\n--- CPU lookup test ---\n");
    const char *test_cpus[] = {"generic", "haswell", "skylake", "znver4", "znver3", "nonexistent"};
    for (const char *cpu : test_cpus) {
        const CPUEntry *entry = find_cpu(cpu);
        if (entry) {
            printf("  %s: %u features\n", cpu, feature_popcount(&entry->features));
        } else {
            printf("  %s: NOT FOUND\n", cpu);
        }
    }

    // Feature implications
    printf("\n--- Feature implications ---\n");
    const FeatureEntry *avx512f = find_feature("avx512f");
    if (avx512f) {
        printf("avx512f implies: ");
        int first = 1;
        for (unsigned i = 0; i < num_features; i++) {
            if (feature_test(&avx512f->implies, feature_table[i].bit)) {
                if (!first) printf(", ");
                printf("%s", feature_table[i].name);
                first = 0;
            }
        }
        printf("\n");
    }

    // Target string parsing
    test_parse("native");
    test_parse("haswell");
    test_parse("generic;haswell;skylake-avx512");
    test_parse("haswell,clone_all;skylake,+avx512f,+avx512bw,-sse4a,opt_size");
    test_parse("znver3;znver4,base(0)");

    // Sysimage matching
    printf("\n--- Sysimage matching ---\n");
    {
        auto parsed = tp::parse_target_string("generic;haswell;skylake-avx512");
        auto resolved = tp::resolve_targets(parsed);

        const auto &host_cpu = tp::get_host_cpu_name();
        int match = tp::match_sysimg_target(resolved, host_features, host_cpu);
        printf("  Host: %s\n", host_cpu.c_str());
        printf("  Best match: target %d (%s)\n", match,
               match >= 0 ? resolved[match].cpu_name.c_str() : "none");
    }

    // Clone flags
    printf("\n--- Clone flags ---\n");
    {
        auto parsed = tp::parse_target_string("generic;haswell;skylake-avx512");
        auto resolved = tp::resolve_targets(parsed);
        tp::compute_clone_flags(resolved);

        for (size_t i = 0; i < resolved.size(); i++) {
            printf("  Target %zu (%s): flags=", i, resolved[i].cpu_name.c_str());
            uint32_t f = resolved[i].flags;
            if (f & tp::TF_CLONE_ALL)  printf("CLONE_ALL ");
            if (f & tp::TF_CLONE_MATH) printf("CLONE_MATH ");
            if (f & tp::TF_CLONE_LOOP) printf("CLONE_LOOP ");
            if (f & tp::TF_CLONE_SIMD) printf("CLONE_SIMD ");
            if (f & tp::TF_CLONE_CPU)  printf("CLONE_CPU ");
            if (f & tp::TF_CLONE_FLOAT16)  printf("CLONE_FLOAT16 ");
            if (f & tp::TF_CLONE_BFLOAT16) printf("CLONE_BFLOAT16 ");
            if (f == 0) printf("(none)");
            printf("\n");
        }
    }

    // Cross-arch queries
    printf("\n--- Cross-arch queries ---\n");
    int failures = 0;
    {
        auto check = [&](bool cond, const char *msg) {
            if (!cond) { printf("  FAIL: %s\n", msg); failures++; }
        };

        // All arches have tables
        const char *arches[] = {"x86_64", "aarch64", "riscv64"};
        for (const char *arch : arches) {
            unsigned nf = tp::cross_num_features(arch);
            unsigned nc = tp::cross_num_cpus(arch);
            unsigned nw = tp::cross_feature_words(arch);
            printf("  %s: %u features, %u CPUs, %u words\n", arch, nf, nc, nw);
            check(nf > 50, "should have >50 features");
            check(nc > 5, "should have >5 CPUs");
            check(nw >= 4 && nw <= 5, "should have 4 or 5 words");
        }

        // Unknown arch returns zeros
        check(tp::cross_num_features("powerpc") == 0, "unknown arch returns 0 features");
        check(tp::cross_feature_words("powerpc") == 0, "unknown arch returns 0 words");

        // Arch name normalization
        check(tp::cross_num_features("arm64") == tp::cross_num_features("aarch64"),
              "arm64 should normalize to aarch64");
        check(tp::cross_num_features("i686") == tp::cross_num_features("x86_64"),
              "i686 should normalize to x86_64");

        // Cross-arch CPU lookups
        tp::CrossFeatureBits fb;
        check(tp::cross_lookup_cpu("x86_64", "haswell", fb), "haswell should be found");
        check(fb.num_words == 4, "x86_64 should have 4 words");

        // Count bits in haswell - should have many hw features
        int haswell_bits = 0;
        for (unsigned w = 0; w < fb.num_words; w++)
            haswell_bits += __builtin_popcountll(fb.bits[w]);
        printf("  x86_64/haswell: %d hw features\n", haswell_bits);
        check(haswell_bits > 20, "haswell should have >20 hw features");

        check(tp::cross_lookup_cpu("aarch64", "cortex-a78", fb), "cortex-a78 should be found");
        check(fb.num_words == 5, "aarch64 should have 5 words");
        int a78_bits = 0;
        for (unsigned w = 0; w < fb.num_words; w++)
            a78_bits += __builtin_popcountll(fb.bits[w]);
        printf("  aarch64/cortex-a78: %d hw features\n", a78_bits);
        check(a78_bits > 15, "cortex-a78 should have >15 hw features");

        check(tp::cross_lookup_cpu("riscv64", "sifive-u74", fb), "sifive-u74 should be found");
        check(!tp::cross_lookup_cpu("x86_64", "nonexistent", fb), "nonexistent should not be found");
        check(!tp::cross_lookup_cpu("badarch", "haswell", fb), "bad arch should not be found");

        // Apple M-series aliases
        tp::CrossFeatureBits m1_fb, a14_fb;
        check(tp::cross_lookup_cpu("aarch64", "apple-m1", m1_fb), "apple-m1 alias should resolve");
        check(tp::cross_lookup_cpu("aarch64", "apple-a14", a14_fb), "apple-a14 should be found");
        bool m1_eq_a14 = (m1_fb.num_words == a14_fb.num_words);
        for (unsigned w = 0; m1_eq_a14 && w < m1_fb.num_words; w++)
            m1_eq_a14 = (m1_fb.bits[w] == a14_fb.bits[w]);
        check(m1_eq_a14, "apple-m1 should equal apple-a14");

        // Verify aarch64 CPUs include architecture version features
        auto has_cross_feat = [&](const char *arch, const char *cpu, const char *feat) {
            tp::CrossFeatureBits cfb;
            if (!tp::cross_lookup_cpu(arch, cpu, cfb)) return false;
            int bit = tp::cross_feature_bit(arch, feat);
            if (bit < 0) return false;
            return ((cfb.bits[bit / 64] >> (bit % 64)) & 1) != 0;
        };

        // cortex-x925 is ARMv9.2 — must have v8.1a through v9.2a
        check(has_cross_feat("aarch64", "cortex-x925", "v8a"),   "x925 should have v8a");
        check(has_cross_feat("aarch64", "cortex-x925", "v8.1a"), "x925 should have v8.1a");
        check(has_cross_feat("aarch64", "cortex-x925", "v8.2a"), "x925 should have v8.2a");
        check(has_cross_feat("aarch64", "cortex-x925", "v9a"),   "x925 should have v9a");
        check(has_cross_feat("aarch64", "cortex-x925", "v9.2a"), "x925 should have v9.2a");
        check(has_cross_feat("aarch64", "cortex-x925", "sve2"),  "x925 should have sve2");
        check(has_cross_feat("aarch64", "cortex-x925", "dotprod"), "x925 should have dotprod");
        check(has_cross_feat("aarch64", "cortex-x925", "fullfp16"), "x925 should have fullfp16");
        check(has_cross_feat("aarch64", "cortex-x925", "bf16"),  "x925 should have bf16");

        // cortex-a78 is ARMv8.2 — must have v8.1a, v8.2a but not v9a
        check(has_cross_feat("aarch64", "cortex-a78", "v8.1a"), "a78 should have v8.1a");
        check(has_cross_feat("aarch64", "cortex-a78", "v8.2a"), "a78 should have v8.2a");
        check(!has_cross_feat("aarch64", "cortex-a78", "v9a"),  "a78 should NOT have v9a");
        check(has_cross_feat("aarch64", "cortex-a78", "lse"),   "a78 should have lse");
        check(has_cross_feat("aarch64", "cortex-a78", "rdm"),   "a78 should have rdm");

        // apple-m1 (a14) — Apple CPUs cherry-pick features rather than
        // claiming full ARMv8.x compliance, so check individual features
        check(has_cross_feat("aarch64", "apple-m1", "dotprod"), "m1 should have dotprod");
        check(has_cross_feat("aarch64", "apple-m1", "sha3"),   "m1 should have sha3");
        check(has_cross_feat("aarch64", "apple-m1", "fullfp16"), "m1 should have fullfp16");

        // x86 psABI levels
        check(has_cross_feat("x86_64", "x86-64-v3", "avx2"), "x86-64-v3 should have avx2");
        check(has_cross_feat("x86_64", "x86-64-v3", "fma"),  "x86-64-v3 should have fma");
        check(has_cross_feat("x86_64", "x86-64-v3", "bmi2"), "x86-64-v3 should have bmi2");
        check(!has_cross_feat("x86_64", "x86-64-v3", "avx512f"), "x86-64-v3 should NOT have avx512f");
        check(has_cross_feat("x86_64", "x86-64-v4", "avx512f"),  "x86-64-v4 should have avx512f");
        check(has_cross_feat("x86_64", "x86-64-v4", "avx512vl"), "x86-64-v4 should have avx512vl");

        // riscv64 — sifive-u74 has basic extensions
        check(has_cross_feat("riscv64", "sifive-u74", "m"), "u74 should have m (multiply)");
        check(has_cross_feat("riscv64", "sifive-u74", "a"), "u74 should have a (atomic)");

        // Feature name/bit lookups
        int avx2_bit = tp::cross_feature_bit("x86_64", "avx2");
        check(avx2_bit >= 0, "avx2 should have a valid bit");
        int sve_bit = tp::cross_feature_bit("aarch64", "sve");
        check(sve_bit >= 0, "sve should have a valid bit");
        check(tp::cross_feature_bit("x86_64", "nonexistent_feat") == -1,
              "unknown feature should return -1");

        // Feature bit_at and name iteration
        const char *name0 = tp::cross_feature_name("x86_64", 0);
        int bit0 = tp::cross_feature_bit_at("x86_64", 0);
        check(name0 != nullptr, "feature 0 should have a name");
        check(bit0 >= 0, "feature 0 should have a valid bit");
        check(tp::cross_feature_name("x86_64", 99999) == nullptr,
              "out of range index should return nullptr");
        check(tp::cross_feature_bit_at("x86_64", 99999) == -1,
              "out of range index should return -1");

        // is_hw queries
        check(tp::cross_feature_is_hw("x86_64", "avx2"), "avx2 should be hw");
        check(!tp::cross_feature_is_hw("x86_64", "nonexistent_feat"),
              "unknown feature should not be hw");

        // CPU name iteration
        const char *cpu0 = tp::cross_cpu_name("x86_64", 0);
        check(cpu0 != nullptr, "cpu 0 should have a name");
        check(tp::cross_cpu_name("x86_64", 99999) == nullptr,
              "out of range cpu index should return nullptr");

        // Version
        unsigned ver = tp::cross_tables_version_major("x86_64");
        printf("  tables version: %u\n", ver);
        check(ver >= 18, "tables version should be >= 18");
        check(tp::cross_tables_version_major("aarch64") == ver,
              "all arches should have same version");
    }

    if (failures > 0) {
        printf("\nFAILED: %d test(s) failed.\n", failures);
        return 1;
    }
    printf("\nDone. All tests passed.\n");
    return 0;
}
