// Test the standalone target parsing library - NO LLVM dependency!

// Include the right table for the host architecture
#if defined(__x86_64__) || defined(_M_X64)
#include "target_tables_x86_64.h"
#elif defined(__aarch64__) || defined(_M_ARM64)
#include "target_tables_aarch64.h"
#elif defined(__riscv) && __riscv_xlen == 64
#include "target_tables_riscv64.h"
#endif
#include "target_parsing.h"

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

    printf("\nDone.\n");
    return 0;
}
