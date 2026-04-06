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

    // High-level API: resolve_targets_for_llvm
    printf("\n--- resolve_targets_for_llvm ---\n");
    {
        // Use host features for the test
        auto host_feats = tp::get_host_features();
        auto host_cpu = tp::get_host_cpu_name();
        tp::ResolveOptions opts;
        opts.host_features = &host_feats;
        opts.host_cpu = host_cpu.c_str();

        auto specs = tp::resolve_targets_for_llvm(
            "generic;haswell;skylake-avx512", opts);
        printf("  %zu LLVM specs:\n", specs.size());
        for (size_t i = 0; i < specs.size(); i++) {
            printf("  [%zu] cpu=%s base=%d\n", i,
                   specs[i].cpu_name.c_str(), specs[i].base);
            printf("       features=%s\n",
                   specs[i].cpu_features.substr(0, 80).c_str());
            if (i > 0) {
                printf("       diff: math=%d simd=%d fp16=%d bf16=%d\n",
                       specs[i].diff.has_new_math, specs[i].diff.has_new_simd,
                       specs[i].diff.has_new_float16, specs[i].diff.has_new_bfloat16);
            }
        }

        // Test with specific host to get deterministic results
        // Use haswell features as "host" to test matching
        const CPUEntry *hsw = find_cpu("haswell");
        if (hsw) {
            tp::ResolveOptions hsw_opts;
            hsw_opts.host_features = &hsw->features;
            hsw_opts.host_cpu = "haswell";

            auto hsw_specs = tp::resolve_targets_for_llvm(
                "generic;haswell;skylake-avx512", hsw_opts);

            printf("\n  With haswell as host:\n");
            for (size_t i = 0; i < hsw_specs.size(); i++) {
                printf("  [%zu] cpu=%s vec_size=%d\n", i,
                       hsw_specs[i].cpu_name.c_str(),
                       tp::max_vector_size(hsw_specs[i].en_features));
                if (i > 0) {
                    printf("       diff: math=%d simd=%d\n",
                           hsw_specs[i].diff.has_new_math,
                           hsw_specs[i].diff.has_new_simd);
                }
            }

            // haswell target on haswell host: features should match
            // (first target is masked to host)
            printf("  [0] cpu=%s\n", hsw_specs[0].cpu_name.c_str());
        }
    }

    // Feature diff tests
    printf("\n--- Feature diff ---\n");
    {
        const CPUEntry *generic = find_cpu("generic");
        const CPUEntry *hsw = find_cpu("haswell");
        const CPUEntry *skx = find_cpu("skylake-avx512");

        if (generic && hsw && skx) {
            auto diff_gh = tp::compute_feature_diff(generic->features, hsw->features);
            printf("  generic→haswell: math=%d simd=%d fp16=%d bf16=%d\n",
                   diff_gh.has_new_math, diff_gh.has_new_simd,
                   diff_gh.has_new_float16, diff_gh.has_new_bfloat16);

            auto diff_hs = tp::compute_feature_diff(hsw->features, skx->features);
            printf("  haswell→skylake-avx512: math=%d simd=%d fp16=%d bf16=%d\n",
                   diff_hs.has_new_math, diff_hs.has_new_simd,
                   diff_hs.has_new_float16, diff_hs.has_new_bfloat16);

            auto diff_same = tp::compute_feature_diff(hsw->features, hsw->features);
            printf("  haswell→haswell: math=%d simd=%d (should be 0,0)\n",
                   diff_same.has_new_math, diff_same.has_new_simd);
        }

        // max_vector_size
        if (generic && hsw && skx) {
            printf("  vec_size: generic=%d haswell=%d skylake-avx512=%d\n",
                   tp::max_vector_size(generic->features),
                   tp::max_vector_size(hsw->features),
                   tp::max_vector_size(skx->features));
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

        // apple-m1 (a14) — M1 is arm 8.5 without BTI, so must report v8.4
        check(has_cross_feat("aarch64", "apple-m1", "v8.4a"), "m1 should report v8.4a");
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

        // ============================================================
        // resolve_targets_for_llvm with deterministic host
        // ============================================================

        const CPUEntry *hsw_cpu = find_cpu("haswell");
        if (hsw_cpu) {
            tp::ResolveOptions hsw_opts;
            hsw_opts.host_features = &hsw_cpu->features;
            hsw_opts.host_cpu = "haswell";

            // Test: generic;haswell;skylake-avx512 on haswell host
            auto specs = tp::resolve_targets_for_llvm(
                "generic;haswell;skylake-avx512", hsw_opts);

            check(specs.size() == 3, "should produce 3 specs");

            // First target: generic
            check(specs[0].cpu_name == "x86-64", "spec[0] should be x86-64 (normalized)");
            check(specs[0].base == -1, "spec[0] base should be -1");

            // Second target: haswell
            check(specs[1].cpu_name == "haswell", "spec[1] should be haswell");
            check(specs[1].base == 0, "spec[1] base should be 0");
            check(specs[1].diff.has_new_math, "haswell should have new math vs generic");
            check(specs[1].diff.has_new_simd, "haswell should have new simd vs generic");
            check(!specs[1].diff.has_new_float16, "haswell should NOT have new fp16 vs generic");

            // Third target: skylake-avx512
            check(specs[2].cpu_name == "skylake-avx512", "spec[2] should be skylake-avx512");
            check(specs[2].diff.has_new_simd, "skx should have new simd vs generic");
            check(tp::max_vector_size(specs[2].en_features) == 64,
                  "spec[2] vec_size should be 64 (avx512)");

            // Test: rdrnd/rdseed should be stripped
            check(!tp::has_feature(specs[1].en_features, "rdrnd"),
                  "rdrnd should be stripped from haswell");

            // Test: hw_feature_mask filtering
            // Tuning features should NOT be in en_features
            check(!tp::has_feature(specs[1].en_features, "slow-3ops-lea"),
                  "tuning features should not be in en_features");

            // Test: dis_features is complement of en within hw_mask
            FeatureBits combined;
            for (int w = 0; w < TARGET_FEATURE_WORDS; w++)
                combined.bits[w] = specs[1].en_features.bits[w] | specs[1].dis_features.bits[w];
            check(feature_equal(&combined, &hw_feature_mask),
                  "en | dis should equal hw_feature_mask");
        }

        // Feature diff standalone tests
        {
            const CPUEntry *gen = find_cpu("generic");
            const CPUEntry *skx = find_cpu("skylake-avx512");
            if (gen && hsw_cpu && skx) {
                auto d1 = tp::compute_feature_diff(gen->features, hsw_cpu->features);
                check(d1.has_new_math, "generic→haswell should have new math");
                check(d1.has_new_simd, "generic→haswell should have new simd");

                auto d2 = tp::compute_feature_diff(hsw_cpu->features, skx->features);
                check(d2.has_new_simd, "haswell→skx should have new simd (avx512)");

                auto d3 = tp::compute_feature_diff(hsw_cpu->features, hsw_cpu->features);
                check(!d3.has_new_math, "same→same should have no new math");
                check(!d3.has_new_simd, "same→same should have no new simd");
            }
        }

        // max_vector_size tests
        {
            const CPUEntry *gen = find_cpu("generic");
            const CPUEntry *skx = find_cpu("skylake-avx512");
            if (gen && hsw_cpu && skx) {
                check(tp::max_vector_size(gen->features) == 16, "generic should be 16 (SSE)");
                check(tp::max_vector_size(hsw_cpu->features) == 32, "haswell should be 32 (AVX)");
                check(tp::max_vector_size(skx->features) == 64, "skx should be 64 (AVX-512)");
            }
        }

        // ============================================================
        // Test actual Julia CI target strings
        // ============================================================
        printf("\n  --- Julia CI target strings ---\n");

        // x86_64: generic;sandybridge,-xsaveopt,clone_all;haswell,-rdrnd,base(1);x86-64-v4,-rdrnd,base(1)
        if (hsw_cpu) {
            tp::ResolveOptions x86_opts;
            x86_opts.host_features = &hsw_cpu->features;
            x86_opts.host_cpu = "haswell";

            auto x86_specs = tp::resolve_targets_for_llvm(
                "generic;sandybridge,-xsaveopt,clone_all;haswell,-rdrnd,base(1);x86-64-v4,-rdrnd,base(1)",
                x86_opts);

            check(x86_specs.size() == 4, "x86 CI: should produce 4 specs");
            check(x86_specs[0].cpu_name == "x86-64", "x86 CI: spec[0] should be x86-64");
            check(x86_specs[1].cpu_name == "sandybridge", "x86 CI: spec[1] should be sandybridge");
            check(x86_specs[2].cpu_name == "haswell", "x86 CI: spec[2] should be haswell");
            check(x86_specs[3].cpu_name == "x86-64-v4", "x86 CI: spec[3] should be x86-64-v4");

            // sandybridge has clone_all flag
            check(x86_specs[1].flags & tp::TF_CLONE_ALL, "x86 CI: sandybridge should have clone_all");
            // haswell and x86-64-v4 have base(1) → base=1
            check(x86_specs[2].base == 1, "x86 CI: haswell base should be 1");
            check(x86_specs[3].base == 1, "x86 CI: x86-64-v4 base should be 1");

            // rdrnd should be stripped (both from -rdrnd in target AND from strip_nondeterministic)
            check(!tp::has_feature(x86_specs[2].en_features, "rdrnd"), "x86 CI: haswell should not have rdrnd");
            check(!tp::has_feature(x86_specs[3].en_features, "rdrnd"), "x86 CI: v4 should not have rdrnd");

            // xsaveopt should be stripped from sandybridge (explicit -xsaveopt)
            check(!tp::has_feature(x86_specs[1].en_features, "xsaveopt"), "x86 CI: sandybridge should not have xsaveopt");

            // Feature diffs
            check(x86_specs[1].diff.has_new_simd, "x86 CI: sandybridge should have new simd vs generic");
            check(x86_specs[2].diff.has_new_simd, "x86 CI: haswell should have new simd vs sandybridge");
            check(x86_specs[3].diff.has_new_simd, "x86 CI: v4 should have new simd vs sandybridge");

            printf("  x86_64 CI targets: OK (%zu specs)\n", x86_specs.size());
        }

        // i686: pentium4 (only testable on x86 host)
        {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
            auto i686_specs = tp::resolve_targets_for_llvm("pentium4");
            check(i686_specs.size() == 1, "i686 CI: should produce 1 spec");
            check(!(i686_specs[0].flags & tp::TF_UNKNOWN_NAME), "i686 CI: pentium4 should be known");
            printf("  i686 CI targets: OK\n");
#else
            printf("  i686 CI targets: SKIPPED (not x86 host)\n");
#endif
        }

        // aarch64 macOS: generic;apple-m1,clone_all
        {
            tp::CrossFeatureBits m1_cross;
            if (tp::cross_lookup_cpu("aarch64", "apple-m1", m1_cross)) {
                // Just verify the target string parses correctly
                auto parsed = tp::parse_target_string("generic;apple-m1,clone_all");
                check(parsed.size() == 2, "aarch64 mac CI: should parse 2 targets");
                check(parsed[0].cpu_name == "generic", "aarch64 mac CI: first should be generic");
                check(parsed[1].cpu_name == "apple-m1", "aarch64 mac CI: second should be apple-m1");
                check(parsed[1].flags & tp::TF_CLONE_ALL, "aarch64 mac CI: apple-m1 should have clone_all");
                printf("  aarch64 macOS CI targets: OK\n");
            }
        }

        // aarch64 Linux: generic;cortex-a57;thunderx2t99;carmel,clone_all;apple-m1,base(3);neoverse-512tvb,base(3)
        {
            auto parsed = tp::parse_target_string(
                "generic;cortex-a57;thunderx2t99;carmel,clone_all;apple-m1,base(3);neoverse-512tvb,base(3)");
            check(parsed.size() == 6, "aarch64 linux CI: should parse 6 targets");
            check(parsed[3].cpu_name == "carmel", "aarch64 linux CI: target[3] should be carmel");
            check(parsed[3].flags & tp::TF_CLONE_ALL, "aarch64 linux CI: carmel should have clone_all");
            check(parsed[4].base == 3, "aarch64 linux CI: apple-m1 base should be 3");
            check(parsed[5].base == 3, "aarch64 linux CI: neoverse-512tvb base should be 3");

            // Verify all CPU names are known via cross-arch lookup
            tp::CrossFeatureBits tmpfb;
            check(tp::cross_lookup_cpu("aarch64", "cortex-a57", tmpfb), "aarch64 CI: cortex-a57 should be known");
            check(tp::cross_lookup_cpu("aarch64", "thunderx2t99", tmpfb), "aarch64 CI: thunderx2t99 should be known");
            check(tp::cross_lookup_cpu("aarch64", "carmel", tmpfb), "aarch64 CI: carmel should be known");
            check(tp::cross_lookup_cpu("aarch64", "apple-m1", tmpfb), "aarch64 CI: apple-m1 should be known");
            // neoverse-512tvb might not be in LLVM 20 tables
            bool has_512tvb = tp::cross_lookup_cpu("aarch64", "neoverse-512tvb", tmpfb);
            printf("  aarch64 Linux CI targets: OK (neoverse-512tvb %s)\n",
                   has_512tvb ? "found" : "NOT found - may need LLVM update");
        }

        // ============================================================
        // Test resolution on popular CPUs as simulated hosts
        // ============================================================
        printf("\n  --- Popular CPU host simulation ---\n");

        auto test_x86_host = [&](const char *host_name, const char *expected_best) {
            const CPUEntry *host = find_cpu(host_name);
            if (!host) { printf("  %s: NOT IN TABLE (skip)\n", host_name); return; }

            tp::ResolveOptions opts;
            opts.host_features = &host->features;
            opts.host_cpu = host_name;

            auto specs = tp::resolve_targets_for_llvm(
                "generic;sandybridge,-xsaveopt,clone_all;haswell,-rdrnd,base(1);x86-64-v4,-rdrnd,base(1)",
                opts);

            // Find the "best" target: the last one whose en_features are
            // a subset of the host (simulating sysimg matching)
            int best = 0;
            for (int i = (int)specs.size() - 1; i >= 0; i--) {
                FeatureBits missing;
                feature_andnot(&missing, &specs[i].en_features, &hw_feature_mask);
                // Check: does the host have all the enabled hw features of this target?
                FeatureBits target_hw, host_hw, diff;
                feature_and_out(&target_hw, &specs[i].en_features, &hw_feature_mask);
                feature_and_out(&host_hw, &host->features, &hw_feature_mask);
                feature_andnot(&diff, &target_hw, &host_hw);
                if (!feature_any(&diff)) {
                    best = i;
                    break;
                }
            }

            printf("  %s → best match: [%d] %s (expected: %s) %s\n",
                   host_name, best, specs[best].cpu_name.c_str(), expected_best,
                   specs[best].cpu_name == expected_best ? "OK" : "MISMATCH");
            check(specs[best].cpu_name == expected_best,
                  (std::string(host_name) + " should match " + expected_best).c_str());
        };

        // x86_64 hosts against Julia's CI target string
        test_x86_host("core2", "x86-64");           // too old for sandybridge
        test_x86_host("sandybridge", "sandybridge"); // exact match
        test_x86_host("haswell", "haswell");          // exact match
        test_x86_host("skylake", "haswell");          // skylake > haswell but < v4
        test_x86_host("skylake-avx512", "x86-64-v4"); // has avx512
        test_x86_host("znver1", "haswell");           // zen1 has all haswell codegen features
        test_x86_host("znver3", "haswell");           // zen3 has all haswell ISA features
        test_x86_host("znver4", "x86-64-v4");        // AMD Zen 4 has avx512
        test_x86_host("broadwell", "haswell");        // broadwell ⊃ haswell

        // Same test with psABI levels — fixes AMD matching
        printf("\n  --- psABI level targets (recommended) ---\n");
        auto test_x86_psabi = [&](const char *host_name, const char *expected_best) {
            const CPUEntry *host = find_cpu(host_name);
            if (!host) { printf("  %s: NOT IN TABLE (skip)\n", host_name); return; }

            tp::ResolveOptions opts;
            opts.host_features = &host->features;
            opts.host_cpu = host_name;

            auto specs = tp::resolve_targets_for_llvm(
                "generic;x86-64-v2,clone_all;x86-64-v3,-rdrnd,base(1);x86-64-v4,-rdrnd,base(1)",
                opts);

            int best = 0;
            for (int i = (int)specs.size() - 1; i >= 0; i--) {
                FeatureBits target_hw, host_hw, diff;
                feature_and_out(&target_hw, &specs[i].en_features, &hw_feature_mask);
                feature_and_out(&host_hw, &host->features, &hw_feature_mask);
                feature_andnot(&diff, &target_hw, &host_hw);
                if (!feature_any(&diff)) { best = i; break; }
            }

            printf("  %s → [%d] %s (expected: %s) %s\n",
                   host_name, best, specs[best].cpu_name.c_str(), expected_best,
                   specs[best].cpu_name == expected_best ? "OK" : "MISMATCH");
            check(specs[best].cpu_name == expected_best,
                  (std::string(host_name) + " psABI should match " + expected_best).c_str());
        };

        test_x86_psabi("core2", "x86-64");
        test_x86_psabi("sandybridge", "x86-64-v2");
        test_x86_psabi("haswell", "x86-64-v3");
        test_x86_psabi("skylake", "x86-64-v3");
        test_x86_psabi("skylake-avx512", "x86-64-v4");
        test_x86_psabi("znver1", "x86-64-v3");       // AMD Zen 1: now correctly matches v3!
        test_x86_psabi("znver3", "x86-64-v3");       // AMD Zen 3: same
        test_x86_psabi("znver4", "x86-64-v4");       // AMD Zen 4: avx512
        test_x86_psabi("broadwell", "x86-64-v3");

        // ============================================================
        // Serialization round-trip
        // ============================================================
        printf("\n  --- Serialization round-trip ---\n");
        if (hsw_cpu) {
            tp::ResolveOptions opts;
            opts.host_features = &hsw_cpu->features;
            opts.host_cpu = "haswell";

            auto specs = tp::resolve_targets_for_llvm(
                "generic;x86-64-v2,clone_all;x86-64-v3,-rdrnd,base(1);x86-64-v4,-rdrnd,base(1)",
                opts);

            // Serialize
            auto blob = tp::serialize_targets(specs);
            check(blob.size() > 0, "serialized data should be non-empty");

            // Deserialize
            auto restored = tp::deserialize_targets(blob.data());
            check(restored.size() == specs.size(), "round-trip: same count");
            for (size_t i = 0; i < specs.size() && i < restored.size(); i++) {
                check(restored[i].cpu_name == specs[i].cpu_name,
                      ("round-trip name mismatch at " + std::to_string(i)).c_str());
                check(restored[i].flags == specs[i].flags,
                      ("round-trip flags mismatch at " + std::to_string(i)).c_str());
                check(restored[i].base == specs[i].base,
                      ("round-trip base mismatch at " + std::to_string(i)).c_str());
                check(feature_equal(&restored[i].en_features, &specs[i].en_features),
                      ("round-trip en_features mismatch at " + std::to_string(i)).c_str());
                check(feature_equal(&restored[i].dis_features, &specs[i].dis_features),
                      ("round-trip dis_features mismatch at " + std::to_string(i)).c_str());
            }
            printf("  serialization round-trip: OK (%zu bytes, %zu targets)\n",
                   blob.size(), specs.size());
        }

        // ============================================================
        // Target matching via library API
        // ============================================================
        printf("\n  --- Target matching ---\n");
        {
            auto test_match = [&](const char *host_name, const char *target_str,
                                  const char *expected_best) {
                const CPUEntry *host = find_cpu(host_name);
                if (!host) { printf("  %s: NOT IN TABLE (skip)\n", host_name); return; }

                auto sysimg_specs = tp::resolve_targets_for_llvm(target_str);

                // Build host target
                tp::ResolveOptions host_opts;
                host_opts.host_features = &host->features;
                host_opts.host_cpu = host_name;
                auto host_specs = tp::resolve_targets_for_llvm("native", host_opts);
                check(!host_specs.empty(), "host should produce at least 1 spec");

                auto match = tp::match_targets(sysimg_specs, host_specs[0]);

                const char *matched = match.best_idx >= 0
                    ? sysimg_specs[match.best_idx].cpu_name.c_str() : "NONE";
                printf("  %s → [%d] %s (expected: %s) %s\n",
                       host_name, match.best_idx, matched, expected_best,
                       std::string(matched) == expected_best ? "OK" : "MISMATCH");
                check(std::string(matched) == expected_best,
                      (std::string(host_name) + " match should be " + expected_best).c_str());
            };

            // psABI targets
            test_match("core2", "generic;x86-64-v2,clone_all;x86-64-v3,base(1);x86-64-v4,base(1)", "x86-64");
            test_match("haswell", "generic;x86-64-v2,clone_all;x86-64-v3,base(1);x86-64-v4,base(1)", "x86-64-v3");
            test_match("znver1", "generic;x86-64-v2,clone_all;x86-64-v3,base(1);x86-64-v4,base(1)", "x86-64-v3");
            test_match("znver4", "generic;x86-64-v2,clone_all;x86-64-v3,base(1);x86-64-v4,base(1)", "x86-64-v4");
            test_match("skylake-avx512", "generic;x86-64-v2,clone_all;x86-64-v3,base(1);x86-64-v4,base(1)", "x86-64-v4");
        }
        // ============================================================
        // build_feature_string should only include hw features
        // ============================================================
        printf("\n  --- build_feature_string filtering ---\n");
        {
            // build_feature_string should not include non-hw features
            // (tuning hints like fast-variable-crosslane-shuffle)
            auto host_feats = tp::get_host_features();
            auto feat_str = tp::build_feature_string(host_feats);

            // Check that no non-hw feature appears in the string
            for (unsigned i = 0; i < num_features; i++) {
                if (feature_table[i].is_hw) continue;
                // Non-hw feature should not appear in the output
                std::string name = std::string("+") + feature_table[i].name;
                bool found = feat_str.find(name) != std::string::npos;
                if (found) {
                    printf("  FAIL: non-hw feature '%s' found in build_feature_string output\n",
                           feature_table[i].name);
                }
                check(!found,
                      (std::string("non-hw feature '") + feature_table[i].name +
                       "' should not appear in build_feature_string").c_str());
            }
            printf("  build_feature_string: %s\n",
                   failures == 0 ? "OK (no non-hw features)" : "FAILED");
        }

    } // end cross-arch tests

    if (failures > 0) {
        printf("\nFAILED: %d test(s) failed.\n", failures);
        return 1;
    }
    printf("\nDone. All tests passed.\n");
    return 0;
}
