// Standalone target parsing library implementation.
// No LLVM runtime dependency - uses pre-generated tables.

// Include generated tables FIRST (defines FeatureBits etc.)
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include "target_tables_x86_64.h"
#elif defined(__aarch64__) || defined(_M_ARM64)
#include "target_tables_aarch64.h"
#elif defined(__riscv) && __riscv_xlen == 64
#include "target_tables_riscv64.h"
#else
#error "Unsupported architecture - generate tables with gen_target_tables"
#endif

#include "target_parsing.h"

#include <cstring>
#include <cstdio>

namespace tp {

// ============================================================================
// Target string parsing
// ============================================================================

std::vector<ParsedTarget> parse_target_string(std::string_view target_str) {
    std::vector<ParsedTarget> result;

    if (target_str.empty()) {
        result.push_back({"native", 0, -1, {}});
        return result;
    }

    for (auto target_sv : split(target_str, ';')) {
        ParsedTarget t;
        auto tokens = split(target_sv, ',');
        if (tokens.empty()) continue;

        t.cpu_name = std::string(tokens[0]);

        for (size_t i = 1; i < tokens.size(); i++) {
            auto tok = tokens[i];
            if (tok == "clone_all")       t.flags |= TF_CLONE_ALL;
            else if (tok == "-clone_all") t.flags &= ~TF_CLONE_ALL;
            else if (tok == "opt_size")   t.flags |= TF_OPTSIZE;
            else if (tok == "min_size")   t.flags |= TF_MINSIZE;
            else if (tok.size() > 5 && tok.substr(0, 5) == "base(" && tok.back() == ')') {
                auto num_str = tok.substr(5, tok.size() - 6);
                t.base = std::atoi(std::string(num_str).c_str());
            } else if (!tok.empty() && (tok[0] == '+' || tok[0] == '-')) {
                t.extra_features.emplace_back(tok);
            }
        }

        result.push_back(std::move(t));
    }

    return result;
}

// ============================================================================
// Target resolution
// ============================================================================

std::vector<ResolvedTarget> resolve_targets(
        const std::vector<ParsedTarget> &parsed,
        const FeatureBits *host_features,
        const char *host_cpu) {

    std::vector<ResolvedTarget> result;
    result.reserve(parsed.size());

    for (size_t i = 0; i < parsed.size(); i++) {
        ResolvedTarget rt;
        rt.flags = parsed[i].flags;
        rt.base = parsed[i].base >= 0 ? parsed[i].base : (i > 0 ? 0 : -1);

        const auto &name = parsed[i].cpu_name;

        if (name == "native" || name.empty()) {
            if (host_cpu && *host_cpu)
                rt.cpu_name = host_cpu;
            else
                rt.cpu_name = get_host_cpu_name();

            if (host_features)
                rt.features = *host_features;
            else
                rt.features = get_host_features();
        } else {
            rt.cpu_name = name;
            const CPUEntry *cpu = find_cpu(name.c_str());
            if (cpu) {
                rt.features = cpu->features;
            } else {
                std::fprintf(stderr, "target_parsing: unknown CPU '%s'\n", name.c_str());
                rt.flags |= TF_UNKNOWN_NAME;
                const CPUEntry *gen = find_cpu("generic");
                if (gen) rt.features = gen->features;
            }
        }

        for (const auto &feat : parsed[i].extra_features) {
            bool enable = (feat[0] == '+');
            const char *fname = feat.c_str() + 1;

            const FeatureEntry *fe = find_feature(fname);
            if (fe) {
                if (enable) {
                    feature_set(&rt.features, fe->bit);
                    feature_or(&rt.features, &fe->implies);
                    expand_implied(&rt.features);
                } else {
                    feature_clear(&rt.features, fe->bit);
                    for (unsigned k = 0; k < num_features; k++) {
                        if (feature_test(&feature_table[k].implies, fe->bit))
                            feature_clear(&rt.features, feature_table[k].bit);
                    }
                }
            } else {
                if (!rt.ext_features.empty())
                    rt.ext_features += ',';
                rt.ext_features += feat;
            }
        }

        result.push_back(std::move(rt));
    }

    return result;
}

// ============================================================================
// Feature diff computation
// ============================================================================

FeatureDiff compute_feature_diff(const FeatureBits &base,
                                  const FeatureBits &derived) {
    FeatureBits diff;
    feature_andnot(&diff, &derived, &base);

    FeatureDiff result;

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    result.has_new_math = has_feature(diff, "fma") || has_feature(diff, "fma4");
    result.has_new_simd = has_feature(diff, "avx") || has_feature(diff, "avx2") ||
                          has_feature(diff, "avx512f") || has_feature(diff, "sse4.1");
    result.has_new_float16 = has_feature(diff, "avx512fp16");
    result.has_new_bfloat16 = has_feature(diff, "avx512bf16");
#elif defined(__aarch64__) || defined(_M_ARM64)
    result.has_new_simd = has_feature(diff, "sve") || has_feature(diff, "sve2");
    result.has_new_float16 = has_feature(diff, "fullfp16");
    result.has_new_bfloat16 = has_feature(diff, "bf16");
#elif defined(__riscv)
    result.has_new_simd = has_feature(diff, "v") || has_feature(diff, "zve32x") ||
                          has_feature(diff, "zve64d");
    result.has_new_float16 = has_feature(diff, "zfh");
    result.has_new_bfloat16 = has_feature(diff, "zvfbfmin");
#endif

    return result;
}

// ============================================================================
// Vector register size
// ============================================================================

int max_vector_size(const FeatureBits &bits) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    if (has_feature(bits, "avx512f")) {
        if (!find_feature("evex512") || has_feature(bits, "evex512"))
            return 64;
        return 32;
    }
    if (has_feature(bits, "avx"))
        return 32;
    return 16;
#elif defined(__aarch64__) || defined(_M_ARM64)
    if (has_feature(bits, "sve"))
        return 256; // SVE is scalable, use large number
    return 16;
#elif defined(__riscv)
    if (has_feature(bits, "v") || has_feature(bits, "zve64d"))
        return 128; // RVV scalable
    if (has_feature(bits, "zve32x"))
        return 32;
    return 0;
#else
    (void)bits;
    return 16;
#endif
}

// ============================================================================
// hw_feature_mask access
// ============================================================================

const FeatureBits &get_hw_feature_mask() {
    return hw_feature_mask;
}

// ============================================================================
// Feature string generation
// ============================================================================

std::string build_feature_string(const FeatureBits &features,
                                 const FeatureBits *baseline) {
    std::string result;
    for (unsigned i = 0; i < num_features; i++) {
        int in_feat = feature_test(&features, feature_table[i].bit);
        int in_base = baseline ? feature_test(baseline, feature_table[i].bit) : 0;

        if (in_feat && !in_base) {
            if (!result.empty()) result += ',';
            result += '+';
            result += feature_table[i].name;
        } else if (!in_feat && in_base) {
            if (!result.empty()) result += ',';
            result += '-';
            result += feature_table[i].name;
        }
    }
    return result;
}

// Build LLVM feature string: hw-only, with baseline features appended
static std::string build_llvm_feature_string(const FeatureBits &enabled,
                                              const FeatureBits &disabled) {
    std::string result;
    for (unsigned i = 0; i < num_features; i++) {
        if (!feature_test(&hw_feature_mask, feature_table[i].bit))
            continue;
        bool in_en = feature_test(&enabled, feature_table[i].bit);
        bool in_dis = feature_test(&disabled, feature_table[i].bit);
        if (in_en) {
            if (!result.empty()) result += ',';
            result += '+';
            result += feature_table[i].name;
        } else if (in_dis) {
            if (!result.empty()) result += ',';
            result += '-';
            result += feature_table[i].name;
        }
    }

    // Arch-specific baseline features always required
#if defined(__x86_64__) || defined(_M_X64)
    result += ",+sse2,+mmx,+fxsr,+64bit,+cx8";
#elif defined(__i386__) || defined(_M_IX86)
    result += ",+sse2,+mmx,+fxsr,+cx8";
#endif

    return result;
}

// Normalize CPU name for LLVM (-mcpu value)
static std::string normalize_cpu_for_llvm(const std::string &name) {
#if defined(__x86_64__) || defined(_M_X64)
    if (name == "generic" || name == "x86-64" || name == "x86_64")
        return "x86-64";
#elif defined(__i386__) || defined(_M_IX86)
    if (name == "generic" || name == "i686" || name == "pentium4")
        return "pentium4";
#endif
    return name;
}

// Strip features that LLVM doesn't use for codegen and rr disables
static void strip_nondeterministic_features(FeatureBits &features) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    static const char *features_to_strip[] = {
        "rdrnd", "rdseed", "rtm", "xsaveopt", nullptr
    };
    for (const char **f = features_to_strip; *f; f++) {
        const FeatureEntry *fe = find_feature(*f);
        if (fe) feature_clear(&features, fe->bit);
    }
#else
    (void)features;
#endif
}

// ============================================================================
// High-level: resolve_targets_for_llvm
// ============================================================================

std::vector<LLVMTargetSpec> resolve_targets_for_llvm(
        std::string_view target_str,
        const ResolveOptions &opts) {

    // 1. Parse
    auto parsed = parse_target_string(target_str);

    // 2. Resolve against CPU database
    auto resolved = resolve_targets(parsed, opts.host_features, opts.host_cpu);

    // 3. Post-process each target
    FeatureBits host_feats;
    if (opts.host_features)
        host_feats = *opts.host_features;
    else
        host_feats = get_host_features();

    for (size_t i = 0; i < resolved.size(); i++) {
        auto &rt = resolved[i];

        // Strip nondeterministic features (rdrnd etc.)
        if (opts.strip_nondeterministic)
            strip_nondeterministic_features(rt.features);

        // Mask first target to host features
        if (i == 0 && opts.mask_first_to_host) {
            for (int w = 0; w < TARGET_FEATURE_WORDS; w++)
                rt.features.bits[w] &= host_feats.bits[w];
        }

        // Expand implied after masking
        expand_implied(&rt.features);
    }

    // 4. Build LLVM specs with diffs
    std::vector<LLVMTargetSpec> result;
    result.reserve(resolved.size());

    for (size_t i = 0; i < resolved.size(); i++) {
        const auto &rt = resolved[i];
        LLVMTargetSpec spec;

        spec.cpu_name = normalize_cpu_for_llvm(rt.cpu_name);
        spec.flags = rt.flags;
        spec.base = rt.base;
        spec.ext_features = rt.ext_features;

        // Compute hw-masked enabled and disabled features
        for (int w = 0; w < TARGET_FEATURE_WORDS; w++) {
            spec.en_features.bits[w] = rt.features.bits[w] & hw_feature_mask.bits[w];
            spec.dis_features.bits[w] = hw_feature_mask.bits[w] & ~rt.features.bits[w];
        }

        // Build LLVM feature string
        spec.cpu_features = build_llvm_feature_string(spec.en_features, spec.dis_features);

        // Append ext_features
        if (!rt.ext_features.empty()) {
            spec.cpu_features += ',';
            spec.cpu_features += rt.ext_features;
        }

        // Compute diff from base target
        if (i > 0) {
            int base_idx = rt.base >= 0 ? rt.base : 0;
            spec.diff = compute_feature_diff(resolved[base_idx].features, rt.features);
        }

        result.push_back(std::move(spec));
    }

    return result;
}

} // namespace tp
