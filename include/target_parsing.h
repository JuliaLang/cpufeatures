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

namespace tp {

// Flags for target cloning behavior
enum TargetFlags : uint32_t {
    TF_VEC_CALL       = 1 << 0,
    TF_CLONE_ALL      = 1 << 1,
    TF_CLONE_MATH     = 1 << 2,
    TF_CLONE_LOOP     = 1 << 3,
    TF_CLONE_SIMD     = 1 << 4,
    TF_UNKNOWN_NAME   = 1 << 5,
    TF_OPTSIZE        = 1 << 6,
    TF_MINSIZE        = 1 << 7,
    TF_CLONE_CPU      = 1 << 8,
    TF_CLONE_FLOAT16  = 1 << 9,
    TF_CLONE_BFLOAT16 = 1 << 10,
};

// A parsed target from the target string
struct ParsedTarget {
    std::string cpu_name;
    uint32_t flags = 0;
    int base = -1;
    std::vector<std::string> extra_features; // "+feat" or "-feat"
};

// A fully resolved target
struct ResolvedTarget {
    std::string cpu_name;
    FeatureBits features{};
    uint32_t flags = 0;
    int base = -1;
    std::string ext_features;
};

// Target spec for LLVM codegen
struct TargetSpec {
    std::string cpu_name;
    std::string cpu_features;
    uint32_t flags = 0;
    int base = -1;
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

// ============================================================================
// API
// ============================================================================

// Parse a target string like "haswell;skylake,+avx512f,-sse4a"
std::vector<ParsedTarget> parse_target_string(std::string_view target_str);

// Resolve parsed targets against the CPU/feature database
std::vector<ResolvedTarget> resolve_targets(
    const std::vector<ParsedTarget> &parsed,
    const FeatureBits *host_features = nullptr,
    const char *host_cpu = nullptr);

// Compute clone flags for multi-versioned targets
void compute_clone_flags(std::vector<ResolvedTarget> &targets);

// Match sysimage targets against host, return best match index
int match_sysimg_target(const std::vector<ResolvedTarget> &targets,
                        const FeatureBits &host_features,
                        std::string_view host_cpu);

// Generate LLVM feature strings from resolved targets
std::vector<TargetSpec> get_target_specs(
    const std::vector<ResolvedTarget> &resolved);

// Build an LLVM-compatible feature string from a feature bitset
std::string build_feature_string(const FeatureBits &features,
                                 const FeatureBits *baseline = nullptr);

// Host CPU detection
const std::string &get_host_cpu_name();
FeatureBits get_host_features();

} // namespace tp

#endif // TARGET_PARSING_H
