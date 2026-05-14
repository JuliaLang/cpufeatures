// Test the standalone target parsing library - NO LLVM dependency!
//
// Organized as three sections:
//   1. Host detection — get_host_*() / build_feature_string() /
//      get_host_feature_detection() invariants.
//   2. Sysimage things — parser, resolve_targets_for_llvm, feature diff,
//      Julia CI target strings, sysimg matching, serialization, target
//      matching.
//   3. Cross-arch — focused sample of the cross_*() API surface.

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

#include <string>
#include <cstdio>
#include <cstring>

int main() {
    printf("=== Standalone Target Parsing Library Test ===\n");
    printf("No LLVM runtime dependency!\n");
    printf("Database: %u features, %u CPUs\n", num_features, num_cpus);

    int failures = 0;
    auto check = [&](bool cond, const char *msg) {
        if (!cond) { printf("  FAIL: %s\n", msg); failures++; }
    };

    // === 1. Host detection ===
    auto host_cpu = tp::get_host_cpu_name();
    auto host_feats = tp::get_host_features();
    printf("Host: %s (%u features)\n",
           host_cpu.c_str(), feature_popcount(&host_feats));

    printf("\n--- build_feature_string filtering ---\n");
    {
        // build_feature_string should not include non-hw features
        // (tuning hints like fast-variable-crosslane-shuffle).
        auto feat_str = tp::build_feature_string(host_feats);
        check(!feat_str.empty(), "host feature string should not be empty");
        for (unsigned i = 0; i < num_features; i++) {
            if (feature_table[i].is_hw) continue;
            if (feature_table[i].is_uarch) continue;
            std::string enable_name = std::string("+") + feature_table[i].name;
            std::string disable_name = std::string("-") + feature_table[i].name;
            bool found = (feat_str.find(enable_name)  != std::string::npos ||
                          feat_str.find(disable_name) != std::string::npos);
            check(!found,
                  (std::string("non-hw / non-uarch feature '") + feature_table[i].name +
                   "' should not appear in build_feature_string").c_str());
        }
        printf("  OK\n");
    }

    // Every name in the host-detection lists must resolve to a real HW
    // feature (not an unknown name, not a featureset umbrella).
    printf("\n--- host detection HW-only ---\n");
    {
        auto check_list = [&](tp::HostFeatureDetectionKind kind, const char *desc) {
            for (const char *const *p = tp::get_host_feature_detection(kind); *p; p++) {
                const FeatureEntry *fe = find_feature(*p);
                check(fe != nullptr,
                      (std::string(desc) + " feature '" + *p +
                       "' should be in the feature table").c_str());
                if (!fe) continue;
                check(fe->is_hw,
                      (std::string(desc) + " feature '" + *p +
                       "' should be is_hw=1").c_str());
                check(!fe->is_featureset,
                      (std::string(desc) + " feature '" + *p +
                       "' should not be a featureset umbrella").c_str());
            }
        };
        check_list(tp::HOST_FEATURE_DETECTABLE,   "detectable");
        check_list(tp::HOST_FEATURE_BASELINE,     "baseline");
        check_list(tp::HOST_FEATURE_UNDETECTABLE, "undetectable");
        printf("  OK\n");
    }

    printf("\n--- HW feature detection coverage ---\n");
    {
        FeatureBits detectable{}, baseline{}, undetectable{};
        for (const char *const *p =
                tp::get_host_feature_detection(tp::HOST_FEATURE_DETECTABLE); *p; p++)
            feature_set(&detectable, find_feature(*p)->bit);
        for (const char *const *p =
                tp::get_host_feature_detection(tp::HOST_FEATURE_BASELINE); *p; p++) {
            // Belt-and-suspenders: baseline names already vetted by the
            // sub-section above, but assert is_hw here too (the cheaper
            // up-front check covers the case where this section runs alone).
            const FeatureEntry *fe = find_feature(*p);
            check(fe->is_hw,
                  (std::string("baseline feature '") + *p +
                   "' must be is_hw=1").c_str());
            feature_set(&baseline, fe->bit);
        }
        for (const char *const *p =
                tp::get_host_feature_detection(tp::HOST_FEATURE_UNDETECTABLE); *p; p++)
            feature_set(&undetectable, find_feature(*p)->bit);

        // Any implied detectable HW bit should also be detectable or baseline.
        FeatureBits implied = detectable;
        _expand_entailed_enable_bits(&implied);
        for (unsigned i = 0; i < num_features; i++) {
            const FeatureEntry *fe = &feature_table[i];
            if (feature_test(&implied, fe->bit) &&
                !feature_test(&detectable, fe->bit) &&
                !feature_test(&baseline, fe->bit)) {
#if defined(_WIN32) && defined(__aarch64__)
                // Windows AArch64 PF flags probe features that require fp8/sm4
                // but no PF flag exposes those base features directly.
                if (strcmp(fe->name, "fp8") == 0 || strcmp(fe->name, "sm4") == 0)
                    continue;
#endif
                check(false, (std::string("'") + fe->name +
                              "' is implied by a detectable HW bit but is not "
                              "itself detectable or baseline").c_str());
            }
        }

        // Every non-featureset HW bit must be in exactly one of
        // baseline / detectable / undetectable.
        FeatureBits categorized{};
        check(!feature_intersects(&detectable, &baseline),
              "baseline and detectable must be disjoint");
        feature_or(&categorized, &baseline);
        feature_or(&categorized, &detectable);
        check(!feature_intersects(&categorized, &undetectable),
              "baseline/detectable and undetectable must be disjoint");
        feature_or(&categorized, &undetectable);

        bool any_missing = false;
        for (unsigned i = 0; i < num_features; i++) {
            if (!feature_table[i].is_hw) continue;
            if (feature_table[i].is_featureset) continue;
            if (feature_table[i].is_privileged) continue;
            if (!feature_test(&categorized, feature_table[i].bit)) {
#if defined(_WIN32) && defined(__aarch64__)
                if (strcmp(feature_table[i].name, "fp8") == 0 ||
                    strcmp(feature_table[i].name, "sm4") == 0)
                    continue;
#endif
                check(false, (std::string("HW feature '") +
                              feature_table[i].name +
                              "' is unhandled").c_str());
                any_missing = true;
            }
        }
        if (any_missing) {
            printf("    HW features must be categorized as:\n"
                   "      - baseline      (always present)\n"
                   "      - detectable    (has a runtime probe)\n"
                   "      - undetectable  (no runtime probe, unsafe to enable)\n"
                   "      - featureset    (only groups other features)\n");
        }
        printf("  %s\n", any_missing ? "FAILED" : "OK");
    }

    // Per-platform baseline CPU: any host on this platform/arch is expected
    // to expose at least these features at runtime detection.
#if defined(__x86_64__) || defined(_M_X64)
    const char *baseline_cpu = "haswell";
#elif defined(__aarch64__) || defined(_M_ARM64)
  #if defined(__APPLE__)
    const char *baseline_cpu = "apple-m1";
  #else
    const char *baseline_cpu = "cortex-a57";
  #endif
#elif defined(__riscv) && __riscv_xlen == 64
    const char *baseline_cpu = "sifive-u74";
#else
    const char *baseline_cpu = nullptr;
#endif
    printf("\n--- Detected features cover baseline CPU ---\n");
    {
        if (!baseline_cpu || !find_cpu(baseline_cpu)) {
            printf("  SKIP (no baseline for this platform)\n");
        } else {
            auto sysimg_specs = tp::resolve_targets_for_llvm(baseline_cpu);
            auto host_specs = tp::resolve_targets_for_llvm("native");
            check(!host_specs.empty(), "native should produce at least 1 spec");
            if (!host_specs.empty()) {
                auto match = tp::match_targets(sysimg_specs, host_specs[0]);
                check(match.best_idx == 0,
                      (std::string("detected host should match baseline CPU '") +
                       baseline_cpu + "' (got idx=" + std::to_string(match.best_idx) + ")").c_str());
                if (match.best_idx == 0) {
                    printf("  OK (baseline=%s)\n", baseline_cpu);
                } else {
                    FeatureBits missing;
                    feature_and_out(&missing,
                                    &sysimg_specs[0].en_features,
                                    &host_specs[0].dis_features);
                    printf("    features in %s but missing from host detection:\n",
                           baseline_cpu);
                    for (unsigned i = 0; i < num_features; i++) {
                        if (feature_test(&missing, feature_table[i].bit))
                            printf("      %s\n", feature_table[i].name);
                    }
                }
            }
        }
    }

    // === 2. Sysimage things ===

    // Parse-only Julia CI tests — host-arch-independent. These run the
    // parser so the aarch64 strings are validated even from an x86 host
    // (and vice-versa).
    printf("\n--- Parse-only Julia CI strings ---\n");
    {
        // x86_64
        auto parsed = tp::parse_target_string(
            "generic;sandybridge,-xsaveopt,clone_all;haswell,-rdrnd,base(1);x86-64-v4,-rdrnd,base(1)");
        check(parsed.size() == 4, "x86_64 CI parse: should parse 4 targets");
        if (parsed.size() == 4) {
            check(parsed[0].cpu_name == "generic", "x86_64 CI parse: spec[0] should be generic");
            check(parsed[1].cpu_name == "sandybridge", "x86_64 CI parse: spec[1] should be sandybridge");
            check(parsed[2].cpu_name == "haswell", "x86_64 CI parse: spec[2] should be haswell");
            check(parsed[3].cpu_name == "x86-64-v4", "x86_64 CI parse: spec[3] should be x86-64-v4");
            check(parsed[1].flags & tp::TF_CLONE_ALL, "x86_64 CI parse: sandybridge clone_all");
            check(parsed[2].base == 1, "x86_64 CI parse: haswell base should be 1");
            check(parsed[3].base == 1, "x86_64 CI parse: x86-64-v4 base should be 1");
            check(parsed[1].extra_features.size() == 1 && parsed[1].extra_features[0] == "-xsaveopt",
                  "x86_64 CI parse: sandybridge -xsaveopt");
            check(parsed[2].extra_features.size() == 1 && parsed[2].extra_features[0] == "-rdrnd",
                  "x86_64 CI parse: haswell -rdrnd");
            check(parsed[3].extra_features.size() == 1 && parsed[3].extra_features[0] == "-rdrnd",
                  "x86_64 CI parse: v4 -rdrnd");
        }
    }
    {
        // i686
        auto parsed = tp::parse_target_string("pentium4");
        check(parsed.size() == 1, "i686 CI parse: should parse 1 target");
        if (parsed.size() == 1)
            check(parsed[0].cpu_name == "pentium4", "i686 CI parse: spec[0] should be pentium4");
    }
    {
        // aarch64 macOS
        auto parsed = tp::parse_target_string("generic;apple-m1,clone_all");
        check(parsed.size() == 2, "aarch64 mac CI parse: should parse 2 targets");
        if (parsed.size() == 2) {
            check(parsed[0].cpu_name == "generic", "aarch64 mac CI parse: first should be generic");
            check(parsed[1].cpu_name == "apple-m1", "aarch64 mac CI parse: second should be apple-m1");
            check(parsed[1].flags & tp::TF_CLONE_ALL, "aarch64 mac CI parse: apple-m1 clone_all");
        }
    }
    {
        // aarch64 Linux
        auto parsed = tp::parse_target_string(
            "generic;cortex-a57;thunderx2t99;carmel,clone_all;apple-m1,base(3);neoverse-512tvb,base(3)");
        check(parsed.size() == 6, "aarch64 linux CI parse: should parse 6 targets");
        if (parsed.size() == 6) {
            check(parsed[3].cpu_name == "carmel", "aarch64 linux CI parse: target[3] should be carmel");
            check(parsed[3].flags & tp::TF_CLONE_ALL, "aarch64 linux CI parse: carmel clone_all");
            check(parsed[4].base == 3, "aarch64 linux CI parse: apple-m1 base should be 3");
            check(parsed[5].base == 3, "aarch64 linux CI parse: neoverse-512tvb base should be 3");
        }
    }
    printf("  OK\n");

    // Host-specific sysimage tests — calls find_cpu() and
    // resolve_targets_for_llvm() against the host arch's tables, so
    // they need to be #ifdef'd.
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    {

        check(find_cpu("nonexistent") == nullptr,
              "find_cpu(nonexistent) should return nullptr");

        {
            const FeatureEntry *avx512f = find_feature("avx512f");
            check(avx512f != nullptr, "avx512f should be in the feature table");
            if (avx512f) {
                check(feature_test(&avx512f->implies, find_feature("avx2")->bit),
                      "avx512f should imply avx2");
                check(feature_test(&avx512f->implies, find_feature("fma")->bit),
                      "avx512f should imply fma");
                check(feature_test(&avx512f->implies, find_feature("f16c")->bit),
                      "avx512f should imply f16c");
            }
        }

        {
            auto parsed = tp::parse_target_string(
                "haswell,clone_all;skylake,+avx512f,+avx512bw,-sse4a,opt_size");
            check(parsed.size() == 2, "parser: should parse 2 targets");
            if (parsed.size() == 2) {
                check(parsed[0].flags & tp::TF_CLONE_ALL,
                      "parser: haswell should have clone_all");
                check(parsed[1].flags & tp::TF_OPTSIZE,
                      "parser: skylake should have opt_size");
                check(parsed[1].extra_features.size() == 3,
                      "parser: skylake should have 3 extra features");
                if (parsed[1].extra_features.size() == 3) {
                    check(parsed[1].extra_features[0] == "+avx512f",
                          "parser: first extra should be +avx512f");
                    check(parsed[1].extra_features[1] == "+avx512bw",
                          "parser: second extra should be +avx512bw");
                    check(parsed[1].extra_features[2] == "-sse4a",
                          "parser: third extra should be -sse4a");
                }
            }
        }

        // resolve_targets_for_llvm — explicit CPU names, no host needed.
        {
            auto specs = tp::resolve_targets_for_llvm(
                "generic;haswell;skylake-avx512");

            check(specs.size() == 3, "should produce 3 specs");
            if (specs.size() == 3) {
                check(specs[0].cpu_name == "x86-64", "spec[0] should be x86-64 (normalized)");
                check(specs[0].base == -1, "spec[0] base should be -1");
                check(specs[1].cpu_name == "haswell", "spec[1] should be haswell");
                check(specs[1].base == 0, "spec[1] base should be 0");
                check(specs[1].diff.has_new_math, "haswell should have new math vs generic");
                check(specs[1].diff.has_new_simd, "haswell should have new simd vs generic");
                check(!specs[1].diff.has_new_float16, "haswell should NOT have new fp16 vs generic");
                check(specs[2].cpu_name == "skylake-avx512", "spec[2] should be skylake-avx512");
                check(specs[2].diff.has_new_simd, "skx should have new simd vs generic");
                check(tp::max_vector_size(specs[2].en_features) == 64,
                      "spec[2] vec_size should be 64 (avx512)");

                check(!tp::has_feature(specs[1].en_features, "rdrnd"),
                      "rdrnd should be stripped from haswell");
                check(!tp::has_feature(specs[1].en_features, "slow-3ops-lea"),
                      "tuning features should not be in en_features");

                FeatureBits combined;
                for (int w = 0; w < TARGET_FEATURE_WORDS; w++)
                    combined.bits[w] = specs[1].en_features.bits[w] | specs[1].dis_features.bits[w];
                check(feature_equal(&combined, &llvm_feature_mask),
                      "en | dis should equal llvm_feature_mask");
            }
        }

        {
            const CPUEntry *gen_cpu = find_cpu("generic");
            const CPUEntry *hsw_cpu = find_cpu("haswell");
            const CPUEntry *skx_cpu = find_cpu("skylake-avx512");
            if (gen_cpu && hsw_cpu && skx_cpu) {
                auto d1 = tp::compute_feature_diff(gen_cpu->features, hsw_cpu->features);
                check(d1.has_new_math, "generic→haswell should have new math");
                check(d1.has_new_simd, "generic→haswell should have new simd");

                auto d2 = tp::compute_feature_diff(hsw_cpu->features, skx_cpu->features);
                check(d2.has_new_simd, "haswell→skx should have new simd (avx512)");

                auto d3 = tp::compute_feature_diff(hsw_cpu->features, hsw_cpu->features);
                check(!d3.has_new_math, "same→same should have no new math");
                check(!d3.has_new_simd, "same→same should have no new simd");

                check(tp::max_vector_size(gen_cpu->features) == 16, "generic should be 16 (SSE)");
                check(tp::max_vector_size(hsw_cpu->features) == 32, "haswell should be 32 (AVX)");
                check(tp::max_vector_size(skx_cpu->features) == 64, "skx should be 64 (AVX-512)");
            }
        }

        // Popular CPU host simulation against Julia's x86_64 CI string.
        // host_features in ResolveOptions only matters for "native" entries —
        // this sysimg string has explicit CPU names, so we skip the opts and
        // use host->features directly for the hand-rolled subset match below.
        printf("\n--- Popular CPU host simulation (x86_64) ---\n");
        auto test_x86_host = [&](const char *host_name, const char *expected_best) {
            const CPUEntry *host = find_cpu(host_name);
            if (!host) { printf("  %s: NOT IN TABLE (skip)\n", host_name); return; }

            auto specs = tp::resolve_targets_for_llvm(
                "generic;sandybridge,-xsaveopt,clone_all;haswell,-rdrnd,base(1);x86-64-v4,-rdrnd,base(1)");

            int best = 0;
            for (int i = (int)specs.size() - 1; i >= 0; i--) {
                FeatureBits target_hw, host_hw, diff;
                feature_and_out(&target_hw, &specs[i].en_features, &llvm_feature_mask);
                feature_and_out(&host_hw, &host->features, &llvm_feature_mask);
                feature_andnot(&diff, &target_hw, &host_hw);
                if (!feature_any(&diff)) { best = i; break; }
            }
            printf("  %s → best match: [%d] %s (expected: %s) %s\n",
                   host_name, best, specs[best].cpu_name.c_str(), expected_best,
                   specs[best].cpu_name == expected_best ? "OK" : "MISMATCH");
            check(specs[best].cpu_name == expected_best,
                  (std::string(host_name) + " should match " + expected_best).c_str());
        };
        test_x86_host("core2", "x86-64");
        test_x86_host("sandybridge", "sandybridge");
        test_x86_host("haswell", "haswell");
        test_x86_host("znver4", "x86-64-v4");
        test_x86_host("skylake-avx512", "x86-64-v4");

        // psABI-level sysimg targets (x86-64-v2/v3/v4 instead of named CPUs)
        printf("\n--- psABI level targets (x86_64) ---\n");
        auto test_x86_psabi = [&](const char *host_name, const char *expected_best) {
            const CPUEntry *host = find_cpu(host_name);
            if (!host) { printf("  %s: NOT IN TABLE (skip)\n", host_name); return; }

            auto specs = tp::resolve_targets_for_llvm(
                "generic;x86-64-v2,clone_all;x86-64-v3,-rdrnd,base(1);x86-64-v4,-rdrnd,base(1)");

            int best = 0;
            for (int i = (int)specs.size() - 1; i >= 0; i--) {
                FeatureBits target_hw, host_hw, diff;
                feature_and_out(&target_hw, &specs[i].en_features, &llvm_feature_mask);
                feature_and_out(&host_hw, &host->features, &llvm_feature_mask);
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
        test_x86_psabi("haswell", "x86-64-v3");
        test_x86_psabi("znver1", "x86-64-v3");
        test_x86_psabi("znver4", "x86-64-v4");
        test_x86_psabi("skylake-avx512", "x86-64-v4");

        // Serialization round-trip — explicit CPU names, no host needed.
        printf("\n--- Serialization round-trip (x86_64) ---\n");
        {
            auto specs = tp::resolve_targets_for_llvm(
                "generic;x86-64-v2,clone_all;x86-64-v3,-rdrnd,base(1);x86-64-v4,-rdrnd,base(1)");

            auto blob = tp::serialize_targets(specs);
            check(blob.size() > 0, "serialized data should be non-empty");

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
            printf("  OK (%zu bytes, %zu targets)\n", blob.size(), specs.size());
        }

        // Target matching via library API ("native" — needs host_opts)
        printf("\n--- Target matching (x86_64) ---\n");
        auto test_match = [&](const char *host_name, const char *target_str,
                              const char *expected_best) {
            const CPUEntry *host = find_cpu(host_name);
            if (!host) { printf("  %s: NOT IN TABLE (skip)\n", host_name); return; }

            auto sysimg_specs = tp::resolve_targets_for_llvm(target_str);

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
        test_match("core2", "generic;x86-64-v2,clone_all;x86-64-v3,base(1);x86-64-v4,base(1)", "x86-64");
        test_match("haswell", "generic;x86-64-v2,clone_all;x86-64-v3,base(1);x86-64-v4,base(1)", "x86-64-v3");
        test_match("znver4", "generic;x86-64-v2,clone_all;x86-64-v3,base(1);x86-64-v4,base(1)", "x86-64-v4");
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    {
        const CPUEntry *a57_cpu = find_cpu("cortex-a57");
        const CPUEntry *a78_cpu = find_cpu("cortex-a78");

        check(find_cpu("nonexistent") == nullptr,
              "find_cpu(nonexistent) should return nullptr");

        {
            const FeatureEntry *sve2 = find_feature("sve2");
            check(sve2 != nullptr, "sve2 should be in the feature table");
            if (sve2) {
                check(feature_test(&sve2->implies, find_feature("sve")->bit),
                      "sve2 should imply sve");
            }
            const FeatureEntry *v82a = find_feature("v8.2a");
            check(v82a != nullptr, "v8.2a should be in the feature table");
            if (v82a) {
                check(feature_test(&v82a->implies, find_feature("v8.1a")->bit),
                      "v8.2a should imply v8.1a");
            }
        }

        printf("\n--- Feature string should include 'arch' feature bits ---\n");
        {
            const CPUEntry *host_entry = find_cpu(host_cpu.c_str());
            FeatureBits table_uarch{}, detected_uarch{};
            if (host_entry)
                feature_and_out(&table_uarch, &host_entry->features, &uarch_feature_mask);
            feature_and_out(&detected_uarch, &host_feats, &uarch_feature_mask);

            if (!host_entry || !feature_any(&table_uarch)) {
                printf("  %s has no uarch bits in its table entry (skip)\n",
                       host_cpu.c_str());
            } else {
                bool any = feature_any(&detected_uarch);
                check(any, "host CPU should have at least one detected uarch flag");
                if (!any) {
                    for (unsigned i = 0; i < num_features; i++)
                        if (feature_test(&table_uarch, feature_table[i].bit))
                            printf("    expected (from table): %s\n", feature_table[i].name);
                } else {
                    printf("  OK\n");
                }
            }
        }

        {
            auto parsed = tp::parse_target_string(
                "cortex-a57,clone_all;cortex-a78,+sve2,-aes,opt_size");
            check(parsed.size() == 2, "parser: should parse 2 targets");
            if (parsed.size() == 2) {
                check(parsed[0].flags & tp::TF_CLONE_ALL,
                      "parser: cortex-a57 should have clone_all");
                check(parsed[1].flags & tp::TF_OPTSIZE,
                      "parser: cortex-a78 should have opt_size");
                check(parsed[1].extra_features.size() == 2,
                      "parser: cortex-a78 should have 2 extra features");
                if (parsed[1].extra_features.size() == 2) {
                    check(parsed[1].extra_features[0] == "+sve2",
                          "parser: first extra should be +sve2");
                    check(parsed[1].extra_features[1] == "-aes",
                          "parser: second extra should be -aes");
                }
            }
        }

        // resolve_targets_for_llvm — explicit CPU names only.
        {
            auto specs = tp::resolve_targets_for_llvm(
                "generic;cortex-a57;cortex-a78");

            check(specs.size() == 3, "should produce 3 specs");
            if (specs.size() == 3) {
                check(specs[0].cpu_name == "generic", "spec[0] should be generic");
                check(specs[0].base == -1, "spec[0] base should be -1");
                check(specs[1].cpu_name == "cortex-a57", "spec[1] should be cortex-a57");
                check(specs[1].base == 0, "spec[1] base should be 0");
                check(specs[2].cpu_name == "cortex-a78", "spec[2] should be cortex-a78");
                check(specs[2].base == 0, "spec[2] base should be 0");
                // a78 is ARMv8.2, a57 is ARMv8.0 → must introduce new ISA bits
                check(specs[2].diff.has_new_math || specs[2].diff.has_new_simd ||
                      specs[2].diff.has_new_float16,
                      "a78 should add new ISA features vs a57");

                FeatureBits combined;
                for (int w = 0; w < TARGET_FEATURE_WORDS; w++)
                    combined.bits[w] = specs[1].en_features.bits[w] | specs[1].dis_features.bits[w];
                check(feature_equal(&combined, &llvm_feature_mask),
                      "en | dis should equal llvm_feature_mask");
            }
        }

        {
            if (a57_cpu && a78_cpu) {
                auto d_aa = tp::compute_feature_diff(a57_cpu->features, a78_cpu->features);
                check(d_aa.has_new_simd || d_aa.has_new_math || d_aa.has_new_float16,
                      "cortex-a57→cortex-a78 should add ISA bits");

                auto d_same = tp::compute_feature_diff(a78_cpu->features, a78_cpu->features);
                check(!d_same.has_new_math, "same→same should have no new math");
                check(!d_same.has_new_simd, "same→same should have no new simd");
            }
        }

        // Popular CPU host simulation against Julia's aarch64 Linux CI string
        printf("\n--- Popular CPU host simulation (aarch64) ---\n");
        const char *aarch64_ci =
            "generic;cortex-a57;thunderx2t99;carmel,clone_all;apple-m1,base(3);neoverse-512tvb,base(3)";
        auto test_a64_host = [&](const char *host_name, const char *expected_best) {
            const CPUEntry *host = find_cpu(host_name);
            if (!host) { printf("  %s: NOT IN TABLE (skip)\n", host_name); return; }

            auto specs = tp::resolve_targets_for_llvm(aarch64_ci);

            int best = 0;
            for (int i = (int)specs.size() - 1; i >= 0; i--) {
                FeatureBits target_hw, host_hw, diff;
                feature_and_out(&target_hw, &specs[i].en_features, &llvm_feature_mask);
                feature_and_out(&host_hw, &host->features, &llvm_feature_mask);
                feature_andnot(&diff, &target_hw, &host_hw);
                if (!feature_any(&diff)) { best = i; break; }
            }
            printf("  %s → best match: [%d] %s (expected: %s) %s\n",
                   host_name, best, specs[best].cpu_name.c_str(), expected_best,
                   specs[best].cpu_name == expected_best ? "OK" : "MISMATCH");
            check(specs[best].cpu_name == expected_best,
                  (std::string(host_name) + " should match " + expected_best).c_str());
        };
        test_a64_host("cortex-a57", "cortex-a57");
        test_a64_host("apple-m1", "apple-m1");

        // Serialization round-trip — explicit CPU names, no host needed.
        printf("\n--- Serialization round-trip (aarch64) ---\n");
        {
            auto specs = tp::resolve_targets_for_llvm(
                "generic;cortex-a57;cortex-a78,clone_all");

            auto blob = tp::serialize_targets(specs);
            check(blob.size() > 0, "serialized data should be non-empty");

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
            printf("  OK (%zu bytes, %zu targets)\n", blob.size(), specs.size());
        }

        // Target matching via library API ("native" — needs host_opts)
        printf("\n--- Target matching (aarch64) ---\n");
        auto test_match = [&](const char *host_name, const char *target_str,
                              const char *expected_best) {
            const CPUEntry *host = find_cpu(host_name);
            if (!host) { printf("  %s: NOT IN TABLE (skip)\n", host_name); return; }

            auto sysimg_specs = tp::resolve_targets_for_llvm(target_str);

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
        test_match("cortex-a57", aarch64_ci, "cortex-a57");
        test_match("apple-m1", aarch64_ci, "apple-m1");
    }
#elif defined(__riscv) && __riscv_xlen == 64
    {
        // sifive-u74 is the most widely-deployed RISC-V CPU in the LLVM
        // tables (RVA20-class). Used as both a fixture and a sample host.
        const CPUEntry *u74_cpu = find_cpu("sifive-u74");

        check(find_cpu("nonexistent") == nullptr,
              "find_cpu(nonexistent) should return nullptr");
        check(u74_cpu != nullptr, "find_cpu(sifive-u74) should be in the table");

        // Parser coverage: clone_all, +/-feature, opt_size
        {
            auto parsed = tp::parse_target_string(
                "sifive-u74,clone_all;sifive-u74,+m,-c,opt_size");
            check(parsed.size() == 2, "parser: should parse 2 targets");
            if (parsed.size() == 2) {
                check(parsed[0].flags & tp::TF_CLONE_ALL,
                      "parser: first should have clone_all");
                check(parsed[1].flags & tp::TF_OPTSIZE,
                      "parser: second should have opt_size");
                check(parsed[1].extra_features.size() == 2,
                      "parser: second should have 2 extra features");
            }
        }

        // resolve_targets_for_llvm — explicit CPU names, no host needed.
        {
            auto specs = tp::resolve_targets_for_llvm("sifive-u74;sifive-u74,clone_all");
            check(specs.size() == 2, "should produce 2 specs");
            if (specs.size() == 2) {
                FeatureBits combined;
                for (int w = 0; w < TARGET_FEATURE_WORDS; w++)
                    combined.bits[w] = specs[1].en_features.bits[w] | specs[1].dis_features.bits[w];
                check(feature_equal(&combined, &llvm_feature_mask),
                      "en | dis should equal llvm_feature_mask");
            }
        }

        // Feature diff: same→same has no new bits
        if (u74_cpu) {
            auto d_same = tp::compute_feature_diff(u74_cpu->features, u74_cpu->features);
            check(!d_same.has_new_math, "same→same should have no new math");
            check(!d_same.has_new_simd, "same→same should have no new simd");
        }

        // Serialization round-trip
        printf("\n--- Serialization round-trip (riscv64) ---\n");
        {
            auto specs = tp::resolve_targets_for_llvm("sifive-u74;sifive-u74,clone_all");
            auto blob = tp::serialize_targets(specs);
            check(blob.size() > 0, "serialized data should be non-empty");

            auto restored = tp::deserialize_targets(blob.data());
            check(restored.size() == specs.size(), "round-trip: same count");
            for (size_t i = 0; i < specs.size() && i < restored.size(); i++) {
                check(restored[i].cpu_name == specs[i].cpu_name,
                      ("round-trip name mismatch at " + std::to_string(i)).c_str());
                check(restored[i].flags == specs[i].flags,
                      ("round-trip flags mismatch at " + std::to_string(i)).c_str());
                check(feature_equal(&restored[i].en_features, &specs[i].en_features),
                      ("round-trip en_features mismatch at " + std::to_string(i)).c_str());
                check(feature_equal(&restored[i].dis_features, &specs[i].dis_features),
                      ("round-trip dis_features mismatch at " + std::to_string(i)).c_str());
            }
            printf("  OK (%zu bytes, %zu targets)\n", blob.size(), specs.size());
        }

        // Target matching via library API ("native" — needs host_opts).
        printf("\n--- Target matching (riscv64) ---\n");
        if (u74_cpu) {
            auto sysimg_specs = tp::resolve_targets_for_llvm("sifive-u74");

            tp::ResolveOptions host_opts;
            host_opts.host_features = &u74_cpu->features;
            host_opts.host_cpu = "sifive-u74";
            auto host_specs = tp::resolve_targets_for_llvm("native", host_opts);
            check(!host_specs.empty(), "host should produce at least 1 spec");

            if (!host_specs.empty()) {
                auto match = tp::match_targets(sysimg_specs, host_specs[0]);
                const char *matched = match.best_idx >= 0
                    ? sysimg_specs[match.best_idx].cpu_name.c_str() : "NONE";
                printf("  sifive-u74 → [%d] %s\n", match.best_idx, matched);
                check(match.best_idx >= 0,
                      "sifive-u74 host should match its own sysimg target");
            }
        }
    }
#else
    printf("\n--- Host-specific sysimage tests: SKIPPED (unknown host arch) ---\n");
#endif

    // === 3. Cross-arch queries ===
    printf("\n--- Cross-arch queries ---\n");
    {
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
        check(tp::cross_num_features("arm64") == tp::cross_num_features("aarch64"),
              "arm64 should normalize to aarch64");

        tp::CrossFeatureBits fb;
        check(tp::cross_lookup_cpu("x86_64", "haswell", fb),     "haswell should be found");
        check(tp::cross_lookup_cpu("aarch64", "cortex-a78", fb), "cortex-a78 should be found");
        check(tp::cross_lookup_cpu("riscv64", "sifive-u74", fb), "sifive-u74 should be found");
        check(!tp::cross_lookup_cpu("x86_64", "nonexistent", fb), "nonexistent should not be found");

        auto has_cross = [&](const char *arch, const char *cpu, const char *feat) {
            tp::CrossFeatureBits cfb;
            if (!tp::cross_lookup_cpu(arch, cpu, cfb)) return false;
            int bit = tp::cross_feature_bit(arch, feat);
            if (bit < 0) return false;
            return ((cfb.bits[bit / 64] >> (bit % 64)) & 1) != 0;
        };
        check(has_cross("x86_64",  "haswell",     "avx2"), "haswell should have avx2");
        check(has_cross("aarch64", "cortex-x925", "sve2"), "cortex-x925 should have sve2");
        check(has_cross("riscv64", "sifive-u74",  "m"),    "sifive-u74 should have m (multiply)");

        unsigned ver = tp::cross_tables_version_major("x86_64");
        printf("  tables version: %u\n", ver);
        check(ver >= 18, "tables version should be >= 18");
        check(tp::cross_tables_version_major("aarch64") == ver,
              "all arches should report the same tables version");
    }

    if (failures > 0) {
        printf("\nFAILED: %d test(s) failed.\n", failures);
        return 1;
    }
    printf("\nDone. All tests passed.\n");
    return 0;
}
