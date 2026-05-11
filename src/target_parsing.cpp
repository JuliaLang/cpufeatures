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
#include "target_tables_fallback.h"
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

        // On 32-bit x86, ignore all features from the table.
        // Just use baseline features — full detection is not worth the
        // complexity on this legacy platform, and the x86_64 tables include
        // features like 64bit that don't apply.
#if defined(__i386__) || defined(_M_IX86)
        {
            FeatureBits baseline{};
            static const char *baseline_names[] = {
                "cx8", "cmov", "fxsr", "mmx", "sse", "sse2", "x87", nullptr
            };
            for (const char **f = baseline_names; *f; f++) {
                const FeatureEntry *fe = find_feature(*f);
                if (fe) feature_set(&baseline, fe->bit);
            }
            rt.features = baseline;
        }
#endif

        for (const auto &feat : parsed[i].extra_features) {
            bool enable = (feat[0] == '+');
            const char *fname = feat.c_str() + 1;

            const FeatureEntry *fe = find_feature(fname);
            if (fe) {
                FeatureBits delta{};
                feature_set(&delta, fe->bit);
                if (enable)
                    apply_feature_delta(&rt.features, delta, FeatureBits{});
                else
                    apply_feature_delta(&rt.features, FeatureBits{}, delta);
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
// llvm_feature_mask access
// ============================================================================

const FeatureBits &get_llvm_feature_mask() {
    return llvm_feature_mask;
}

// ============================================================================
// Feature string generation
// ============================================================================

std::string build_feature_string(const FeatureBits &features,
                                 const FeatureBits *baseline) {
    std::string result;
    for (unsigned i = 0; i < num_features; i++) {
        if (!feature_test(&llvm_feature_mask, feature_table[i].bit))
            continue;
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
std::string build_llvm_feature_string(const FeatureBits &enabled,
                                              const FeatureBits &disabled) {
    std::string result;
    for (unsigned i = 0; i < num_features; i++) {
        if (!feature_test(&llvm_feature_mask, feature_table[i].bit))
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
    FeatureBits to_disable{};
    for (const char **f = features_to_strip; *f; f++) {
        const FeatureEntry *fe = find_feature(*f);
        if (fe) feature_set(&to_disable, fe->bit);
    }
    apply_feature_delta(&features, FeatureBits{}, to_disable);
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
    for (size_t i = 0; i < resolved.size(); i++) {
        if (opts.strip_nondeterministic)
            strip_nondeterministic_features(resolved[i].features);
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
            spec.en_features.bits[w] = rt.features.bits[w] & llvm_feature_mask.bits[w];
            spec.dis_features.bits[w] = llvm_feature_mask.bits[w] & ~rt.features.bits[w];
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

// ============================================================================
// Sysimage serialization
// ============================================================================

std::vector<uint8_t> serialize_targets(const std::vector<LLVMTargetSpec> &targets) {
    std::vector<uint8_t> buf;

    auto emit = [&](const void *data, size_t sz) {
        auto p = static_cast<const uint8_t*>(data);
        buf.insert(buf.end(), p, p + sz);
    };
    auto emit_u32 = [&](uint32_t v) { emit(&v, 4); };
    auto emit_str = [&](const std::string &s) {
        emit_u32(static_cast<uint32_t>(s.size()));
        emit(s.data(), s.size());
    };

    emit_u32(static_cast<uint32_t>(targets.size()));
    for (const auto &t : targets) {
        emit_u32(t.flags);
        emit_u32(static_cast<uint32_t>(t.base));
        emit_u32(TARGET_FEATURE_WORDS);
        emit(t.en_features.bits, sizeof(t.en_features.bits));
        emit(t.dis_features.bits, sizeof(t.dis_features.bits));
        emit_str(t.cpu_name);
        emit_str(t.ext_features);
    }

    return buf;
}

std::vector<LLVMTargetSpec> deserialize_targets(const uint8_t *data) {
    auto load = [&](void *dest, size_t sz) {
        std::memcpy(dest, data, sz);
        data += sz;
    };
    auto load_u32 = [&]() -> uint32_t {
        uint32_t v;
        load(&v, 4);
        return v;
    };
    auto load_str = [&]() -> std::string {
        uint32_t len = load_u32();
        std::string s(reinterpret_cast<const char*>(data), len);
        data += len;
        return s;
    };

    uint32_t ntargets = load_u32();
    std::vector<LLVMTargetSpec> result(ntargets);

    for (uint32_t i = 0; i < ntargets; i++) {
        auto &t = result[i];
        t.flags = load_u32();
        t.base = static_cast<int>(load_u32());
        uint32_t nwords = load_u32();
        // Read feature bits, handle size mismatch gracefully
        std::memset(&t.en_features, 0, sizeof(t.en_features));
        std::memset(&t.dis_features, 0, sizeof(t.dis_features));
        unsigned words_to_read = nwords < TARGET_FEATURE_WORDS ? nwords : TARGET_FEATURE_WORDS;
        load(t.en_features.bits, words_to_read * sizeof(uint64_t));
        if (nwords > TARGET_FEATURE_WORDS)
            data += (nwords - TARGET_FEATURE_WORDS) * sizeof(uint64_t);
        load(t.dis_features.bits, words_to_read * sizeof(uint64_t));
        if (nwords > TARGET_FEATURE_WORDS)
            data += (nwords - TARGET_FEATURE_WORDS) * sizeof(uint64_t);
        t.cpu_name = load_str();
        t.ext_features = load_str();
    }

    return result;
}

// ============================================================================
// Sysimage target matching
// ============================================================================

TargetMatch match_targets(const std::vector<LLVMTargetSpec> &targets,
                          const LLVMTargetSpec &host) {
    TargetMatch match;
    bool match_name = false;
    int best_feat_count = 0;

    for (int i = 0; i < static_cast<int>(targets.size()); i++) {
        // Check: target must not enable features the host has disabled
        FeatureBits conflict;
        feature_and_out(&conflict, &targets[i].en_features, &host.dis_features);
        if (feature_any(&conflict))
            continue;

        bool name_match = (targets[i].cpu_name == host.cpu_name);
        int vreg = max_vector_size(targets[i].en_features);
        int feat_count = feature_popcount(&targets[i].en_features);

        if (name_match && !match_name) {
            // First name match resets the search
            match_name = true;
            match.vreg_size = 0;
            best_feat_count = 0;
        }
        if (match_name && !name_match)
            continue;

        if (vreg > match.vreg_size ||
            (vreg == match.vreg_size && feat_count > best_feat_count) ||
            (vreg == match.vreg_size && feat_count == best_feat_count && i > match.best_idx)) {
            match.best_idx = i;
            match.vreg_size = vreg;
            best_feat_count = feat_count;
        }
    }

    return match;
}

void print_cpu_targets() {
    std::printf("Available CPU targets:\n");
    for (unsigned i = 0; i < num_cpus; i++)
        std::printf("  %s\n", cpu_table[i].name);
    std::printf("\nHost CPU: %s\n", get_host_cpu_name().c_str());
}

void apply_feature_delta(FeatureBits *features,
                         FeatureBits to_enable,
                         FeatureBits to_disable) {
    _expand_entailed_enable_bits(&to_enable);
    _expand_entailed_disable_bits(&to_disable);
    feature_or(features, &to_enable);
    feature_andnot(features, features, &to_disable);
}

void apply_host_baseline(FeatureBits *features) {
    for (const char *const *p = get_host_feature_detection(HOST_FEATURE_BASELINE); *p; p++) {
        const FeatureEntry *fe = find_feature(*p);
        if (fe) feature_set(features, fe->bit);
    }
}

} // namespace tp
