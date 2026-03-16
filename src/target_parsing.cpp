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
// Clone flag computation
// ============================================================================

void compute_clone_flags(std::vector<ResolvedTarget> &targets) {
    if (targets.size() <= 1) return;

    FeatureBits base_features = targets[0].features;

    for (size_t i = 1; i < targets.size(); i++) {
        auto &t = targets[i];

        FeatureBits diff;
        feature_andnot(&diff, &t.features, &base_features);

        t.flags |= TF_CLONE_CPU | TF_CLONE_LOOP;

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        if (has_feature(diff, "fma") || has_feature(diff, "fma4"))
            t.flags |= TF_CLONE_MATH;

        if (has_feature(diff, "avx") || has_feature(diff, "avx2") ||
            has_feature(diff, "avx512f") || has_feature(diff, "sse4.1"))
            t.flags |= TF_CLONE_SIMD;

        if (has_feature(diff, "avx512fp16"))
            t.flags |= TF_CLONE_FLOAT16;
        if (has_feature(diff, "avx512bf16"))
            t.flags |= TF_CLONE_BFLOAT16;
#elif defined(__aarch64__) || defined(_M_ARM64)
        if (has_feature(diff, "sve") || has_feature(diff, "sve2"))
            t.flags |= TF_CLONE_SIMD;

        if (has_feature(diff, "fullfp16"))
            t.flags |= TF_CLONE_FLOAT16;
        if (has_feature(diff, "bf16"))
            t.flags |= TF_CLONE_BFLOAT16;
#elif defined(__riscv)
        if (has_feature(diff, "v") || has_feature(diff, "zve32x") ||
            has_feature(diff, "zve64d"))
            t.flags |= TF_CLONE_SIMD;

        if (has_feature(diff, "zfh"))
            t.flags |= TF_CLONE_FLOAT16;
        if (has_feature(diff, "zvfbfmin"))
            t.flags |= TF_CLONE_BFLOAT16;
#endif
    }
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

std::vector<TargetSpec> get_target_specs(
        const std::vector<ResolvedTarget> &resolved) {
    std::vector<TargetSpec> result;
    result.reserve(resolved.size());

    for (const auto &rt : resolved) {
        TargetSpec spec;
        spec.cpu_name = rt.cpu_name;
        spec.flags = rt.flags;
        spec.base = rt.base;

        const CPUEntry *cpu = find_cpu(rt.cpu_name.c_str());
        const FeatureBits *baseline = cpu ? &cpu->features : nullptr;

        spec.cpu_features = build_feature_string(rt.features, baseline);

        if (!rt.ext_features.empty()) {
            if (!spec.cpu_features.empty())
                spec.cpu_features += ',';
            spec.cpu_features += rt.ext_features;
        }

        result.push_back(std::move(spec));
    }

    return result;
}

} // namespace tp
