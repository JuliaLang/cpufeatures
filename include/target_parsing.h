// Standalone target parsing library.
// Uses CPU/feature tables generated at build time from LLVM's TableGen data.
// Zero LLVM runtime dependency.
//
// Usage: #include the generated table header first, then this header.
//   #include "target_tables_x86_64.h"
//   #include "target_parsing.h"

#ifndef TARGET_PARSING_H
#define TARGET_PARSING_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Verify that a generated table header was included first
#ifndef TARGET_FEATURE_WORDS
#error "Include the generated target table header before target_parsing.h"
#endif

// CPU name alias resolution + find_cpu wrapper.
#include "cpu_aliases.h"

inline const CPUEntry *find_cpu(const char *name) {
    return _find_cpu_exact(resolve_cpu_alias(name));
}

namespace tp {

// ============================================================================
// Target parsing flags
// ============================================================================

// Flags set during target string parsing
enum TargetFlags : uint32_t {
    TF_CLONE_ALL    = 1 << 1,
    TF_UNKNOWN_NAME = 1 << 5,
    TF_OPTSIZE      = 1 << 6,
    TF_MINSIZE      = 1 << 7,
};

// ============================================================================
// Types
// ============================================================================

// A parsed target from the target string
struct ParsedTarget {
    std::string cpu_name;
    uint32_t flags = 0;
    int base = -1;
    std::vector<std::string> extra_features; // "+feat" or "-feat"
};

// A fully resolved target (features resolved, not yet LLVM-ready)
struct ResolvedTarget {
    std::string cpu_name;
    FeatureBits features{};
    uint32_t flags = 0;
    int base = -1;
    std::string ext_features;
};

// What changed between a derived target and its base, in codegen terms
struct FeatureDiff {
    bool has_new_math    = false;  // FMA/FMA4 (x86)
    bool has_new_simd    = false;  // SSE4.1/AVX/AVX2/AVX512, SVE, RVV
    bool has_new_float16 = false;  // avx512fp16, fullfp16, zfh
    bool has_new_bfloat16 = false; // avx512bf16, bf16, zvfbfmin
};

// A fully resolved target ready for LLVM consumption
struct LLVMTargetSpec {
    std::string cpu_name;        // Normalized for LLVM (-mcpu)
    std::string cpu_features;    // "+avx2,-sse4a,..." (-mattr), hw-only with baseline
    FeatureBits en_features{};   // Enabled features (hw-masked)
    FeatureBits dis_features{};  // Disabled features (hw-masked complement)
    uint32_t flags = 0;
    int base = -1;
    std::string ext_features;    // Pass-through features unknown to the library
    FeatureDiff diff;            // What's new vs base target
};

// Options for resolve_targets_for_llvm
struct ResolveOptions {
    const FeatureBits *host_features = nullptr; // nullptr = auto-detect
    const char *host_cpu = nullptr;             // nullptr = auto-detect
    bool strip_nondeterministic = true;         // strip rdrnd/rdseed/rtm/xsaveopt (x86)
};

// ============================================================================
// String utilities
// ============================================================================

inline std::string_view trim(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t'))
        sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t'))
        sv.remove_suffix(1);
    return sv;
}

inline std::vector<std::string_view> split(std::string_view sv, char delim) {
    std::vector<std::string_view> result;
    while (!sv.empty()) {
        auto pos = sv.find(delim);
        if (pos == std::string_view::npos) {
            auto piece = trim(sv);
            if (!piece.empty()) result.push_back(piece);
            break;
        }
        auto piece = trim(sv.substr(0, pos));
        if (!piece.empty()) result.push_back(piece);
        sv.remove_prefix(pos + 1);
    }
    return result;
}

// Check if a feature bitset contains a specific feature by name
inline bool has_feature(const FeatureBits &bits, const char *name) {
    const FeatureEntry *fe = find_feature(name);
    return fe && feature_test(&bits, fe->bit);
}

// Print available CPU targets and host info to stdout
void print_cpu_targets();

// ============================================================================
// Low-level API (building blocks)
// ============================================================================

// Parse a target string like "haswell;skylake,+avx512f,-sse4a"
std::vector<ParsedTarget> parse_target_string(std::string_view target_str);

// Resolve parsed targets against the CPU/feature database
std::vector<ResolvedTarget> resolve_targets(
    const std::vector<ParsedTarget> &parsed,
    const FeatureBits *host_features = nullptr,
    const char *host_cpu = nullptr);

// Build a raw feature diff string (for debug, not filtered for LLVM)
std::string build_feature_string(const FeatureBits &features,
                                 const FeatureBits *baseline = nullptr);

// Build LLVM-ready feature string: hw-only features with baseline appended
std::string build_llvm_feature_string(const FeatureBits &enabled,
                                       const FeatureBits &disabled);

// ============================================================================
// High-level API (one-shot, LLVM-ready)
// ============================================================================

// Target string → LLVM-ready specs with diffs computed.
// This is the main entry point: parse, resolve, filter, normalize, diff.
std::vector<LLVMTargetSpec> resolve_targets_for_llvm(
    std::string_view target_str,
    const ResolveOptions &opts = {});

// Compute feature diff between a base and derived feature set
FeatureDiff compute_feature_diff(const FeatureBits &base,
                                 const FeatureBits &derived);

// Max vector register size in bytes for a feature set
// (64=AVX-512, 32=AVX, 16=SSE/NEON, 256=SVE, 128=RVV, 0=none)
int max_vector_size(const FeatureBits &features);

// Access the hw_feature_mask from generated tables
const FeatureBits &get_hw_feature_mask();

// ============================================================================
// Sysimage serialization and matching
// ============================================================================

// Serialize all targets for embedding in sysimages
std::vector<uint8_t> serialize_targets(const std::vector<LLVMTargetSpec> &targets);

// Deserialize targets from binary data (expects count header from serialize_targets)
std::vector<LLVMTargetSpec> deserialize_targets(const uint8_t *data);

// Result of target matching
struct TargetMatch {
    int best_idx = -1;
    int vreg_size = 0;
};

// Match a host target against a set of compiled targets. Returns the best
// compatible target index and its vector register size.
TargetMatch match_targets(const std::vector<LLVMTargetSpec> &targets,
                          const LLVMTargetSpec &host);

// ============================================================================
// Host detection
// ============================================================================

const std::string &get_host_cpu_name();
FeatureBits get_host_features();

enum HostFeatureDetectionKind {
    // Features the host can probe at runtime.
    HOST_FEATURE_DETECTABLE,

    // Features mandated by the ABI / platform spec.
    // Always present and never probed for at runtime.
    HOST_FEATURE_BASELINE,

    // Features the host has no runtime probe for.
    // These features are dangerous to enable at runtime since `cpufeatures`
    // will not notice when the OS or CPU does not support them.
    HOST_FEATURE_UNDETECTABLE,
};

// Pointers are owned by the library; callers must not modify or free.
const char *const *get_host_feature_detection(HostFeatureDetectionKind kind);

// Apply the requested feature delta as `(features | to_enable) & ~to_disable`.
//
// Disabled features take priority over enabled ones.
//
// Accounts for "implied" features that must be enabled / disabled together
// with the requested bits, e.g. "-avx" implies "-avx2". Functions that compute
// and apply feature deltas should use this function to keep logically entailed
// features up to date.
void apply_feature_delta(FeatureBits *features,
                         FeatureBits to_enable,
                         FeatureBits to_disable);

} // namespace tp

#endif // TARGET_PARSING_H
