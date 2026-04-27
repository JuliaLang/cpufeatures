// Host CPU detection for x86_64.
// Standalone - no LLVM dependency.
// Uses CPUID to detect CPU name and features.

#include "target_tables_x86_64.h"
#include "target_parsing.h"

#include <cassert>
#include <cstring>
#include <cpuid.h>

#if defined(__linux__) || defined(__FreeBSD__)
#  include <sched.h>
#  include <unistd.h>
#  define CPUFEATURES_AFFINITY_LINUX 1
#elif defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  define CPUFEATURES_AFFINITY_WIN32 1
#endif

// ============================================================================
// CPUID helpers
// ============================================================================

struct CPUIDResult {
    unsigned eax, ebx, ecx, edx;
};

static CPUIDResult cpuid(unsigned leaf, unsigned subleaf = 0) {
    CPUIDResult r = {};
    __cpuid_count(leaf, subleaf, r.eax, r.ebx, r.ecx, r.edx);
    return r;
}

static unsigned cpuid_max_leaf() {
    return cpuid(0).eax;
}

static unsigned cpuid_max_ext_leaf() {
    return cpuid(0x80000000).eax;
}

// CPUID(7, 0).EDX bit 15: package contains heterogeneous core types
// (Intel "Hybrid" — Alder Lake and later) with potentially different
// CPU features available / enabled on each core.
static bool cpu_is_hybrid_arch() {
    if (cpuid_max_leaf() < 7) return false;
    auto r = cpuid(7, 0);
    return (r.edx >> 15) & 1;
}

// ============================================================================
// CPU vendor/family/model detection
// ============================================================================

enum Vendor { VENDOR_INTEL, VENDOR_AMD, VENDOR_OTHER };

static Vendor get_vendor() {
    auto r = cpuid(0);
    if (r.ebx == 0x756e6547 && r.edx == 0x49656e69 && r.ecx == 0x6c65746e)
        return VENDOR_INTEL;
    if (r.ebx == 0x68747541 && r.edx == 0x69746e65 && r.ecx == 0x444d4163)
        return VENDOR_AMD;
    return VENDOR_OTHER;
}

struct CPUModel {
    unsigned family;
    unsigned model;
    unsigned stepping;
};

static CPUModel get_cpu_model() {
    auto r = cpuid(1);
    CPUModel m;
    m.stepping = r.eax & 0xf;
    m.model = (r.eax >> 4) & 0xf;
    m.family = (r.eax >> 8) & 0xf;

    if (m.family == 6 || m.family == 0xf) {
        m.model += ((r.eax >> 16) & 0xf) << 4;
    }
    if (m.family == 0xf) {
        m.family += (r.eax >> 20) & 0xff;
    }
    return m;
}

// ============================================================================
// Host CPU name detection
// ============================================================================

static const char *detect_intel_cpu(const CPUModel &m) {
    if (m.family != 6) return "generic";

    switch (m.model) {
    case 0x0f:
    case 0x16: return "core2";
    case 0x17:
    case 0x1d: return "core2";
    case 0x1a:
    case 0x1e:
    case 0x1f:
    case 0x2e: return "nehalem";
    case 0x25:
    case 0x2c:
    case 0x2f: return "westmere";
    case 0x2a:
    case 0x2d: return "sandybridge";
    case 0x3a:
    case 0x3e: return "ivybridge";
    case 0x3c:
    case 0x3f:
    case 0x45:
    case 0x46: return "haswell";
    case 0x3d:
    case 0x47:
    case 0x4f:
    case 0x56: return "broadwell";
    case 0x4e:
    case 0x5e:
    case 0x8e:
    case 0x9e: return "skylake";
    case 0x55: {
        if (m.stepping >= 5) return "cascadelake";
        return "skylake-avx512";
    }
    case 0x66: return "cannonlake";
    case 0x6a:
    case 0x6c: return "icelake-server";
    case 0x7d:
    case 0x7e: return "icelake-client";
    case 0x8c:
    case 0x8d: return "tigerlake";
    case 0x8f: return "sapphirerapids";
    case 0x97:
    case 0x9a: return "alderlake";
    case 0xb7:
    case 0xba:
    case 0xbf: return "raptorlake";
    case 0xaa:
    case 0xac: return "meteorlake";
    case 0xbd: return "lunarlake";
    case 0xc5: return "arrowlake";
    case 0xc6: return "arrowlake-s";
    case 0xcf: return "emeraldrapids";
    case 0xad:
    case 0xae: return "graniterapids";
    default: return "generic";
    }
}

static const char *detect_amd_cpu(const CPUModel &m) {
    switch (m.family) {
    case 0x10: return "amdfam10";
    case 0x14: return "btver1";
    case 0x15:
        if (m.model >= 0x60) return "bdver4";
        if (m.model >= 0x30) return "bdver3";
        if (m.model >= 0x02) return "bdver2";
        return "bdver1";
    case 0x16: return "btver2";
    case 0x17:
        if (m.model >= 0x30) return "znver2";
        return "znver1";
    case 0x19:
        if (m.model >= 0x10) return "znver4";
        return "znver3";
    case 0x1a: return "znver5";
    default: return "generic";
    }
}

namespace tp {

const std::string &get_host_cpu_name() {
    static std::string cpu_name;
    if (!cpu_name.empty()) return cpu_name;

#if defined(__i386__) || defined(_M_IX86)
    cpu_name = "pentium4";
#else
    Vendor v = get_vendor();
    CPUModel m = get_cpu_model();

    const char *name;
    if (v == VENDOR_INTEL)
        name = detect_intel_cpu(m);
    else if (v == VENDOR_AMD)
        name = detect_amd_cpu(m);
    else
        name = "generic";

    if (!find_cpu(name))
        name = "generic";

    cpu_name = name;
#endif
    return cpu_name;
}

// ============================================================================
// Table-driven host feature detection via CPUID
// ============================================================================

struct CPUIDBitMapping {
    unsigned leaf;
    unsigned subleaf;
    enum Reg { EAX, EBX, ECX, EDX } reg;
    unsigned bit;
    const char *feature_name;
};

static constexpr CPUIDBitMapping cpuid_features[] = {
    // Leaf 1, ECX
    {1, 0, CPUIDBitMapping::ECX,  0, "sse3"},
    {1, 0, CPUIDBitMapping::ECX,  1, "pclmul"},
    {1, 0, CPUIDBitMapping::ECX,  9, "ssse3"},
    {1, 0, CPUIDBitMapping::ECX, 12, "fma"},
    {1, 0, CPUIDBitMapping::ECX, 13, "cx16"},
    {1, 0, CPUIDBitMapping::ECX, 19, "sse4.1"},
    {1, 0, CPUIDBitMapping::ECX, 20, "sse4.2"},
    {1, 0, CPUIDBitMapping::ECX, 20, "crc32"},
    {1, 0, CPUIDBitMapping::ECX, 22, "movbe"},
    {1, 0, CPUIDBitMapping::ECX, 23, "popcnt"},
    {1, 0, CPUIDBitMapping::ECX, 25, "aes"},
    {1, 0, CPUIDBitMapping::ECX, 26, "xsave"},
    {1, 0, CPUIDBitMapping::ECX, 28, "avx"},
    {1, 0, CPUIDBitMapping::ECX, 29, "f16c"},
    {1, 0, CPUIDBitMapping::ECX, 30, "rdrnd"},

    // Leaf 7 sub 0, EBX
    {7, 0, CPUIDBitMapping::EBX,  0, "fsgsbase"},
    {7, 0, CPUIDBitMapping::EBX,  3, "bmi"},
    {7, 0, CPUIDBitMapping::EBX,  5, "avx2"},
    {7, 0, CPUIDBitMapping::EBX,  8, "bmi2"},
    {7, 0, CPUIDBitMapping::EBX, 10, "invpcid"},
    {7, 0, CPUIDBitMapping::EBX, 16, "avx512f"},
    {7, 0, CPUIDBitMapping::EBX, 17, "avx512dq"},
    {7, 0, CPUIDBitMapping::EBX, 18, "rdseed"},
    {7, 0, CPUIDBitMapping::EBX, 19, "adx"},
    {7, 0, CPUIDBitMapping::EBX, 21, "avx512ifma"},
    {7, 0, CPUIDBitMapping::EBX, 23, "clflushopt"},
    {7, 0, CPUIDBitMapping::EBX, 24, "clwb"},
    {7, 0, CPUIDBitMapping::EBX, 28, "avx512cd"},
    {7, 0, CPUIDBitMapping::EBX, 29, "sha"},
    {7, 0, CPUIDBitMapping::EBX, 30, "avx512bw"},
    {7, 0, CPUIDBitMapping::EBX, 31, "avx512vl"},

    // Leaf 7 sub 0, ECX
    {7, 0, CPUIDBitMapping::ECX,  1, "avx512vbmi"},
    {7, 0, CPUIDBitMapping::ECX,  4, "pku"},
    {7, 0, CPUIDBitMapping::ECX,  5, "waitpkg"},
    {7, 0, CPUIDBitMapping::ECX,  6, "avx512vbmi2"},
    {7, 0, CPUIDBitMapping::ECX,  7, "shstk"},
    {7, 0, CPUIDBitMapping::ECX,  8, "gfni"},
    {7, 0, CPUIDBitMapping::ECX,  9, "vaes"},
    {7, 0, CPUIDBitMapping::ECX, 10, "vpclmulqdq"},
    {7, 0, CPUIDBitMapping::ECX, 11, "avx512vnni"},
    {7, 0, CPUIDBitMapping::ECX, 12, "avx512bitalg"},
    {7, 0, CPUIDBitMapping::ECX, 14, "avx512vpopcntdq"},
    {7, 0, CPUIDBitMapping::ECX, 22, "rdpid"},
    {7, 0, CPUIDBitMapping::ECX, 25, "cldemote"},
    {7, 0, CPUIDBitMapping::ECX, 27, "movdiri"},
    {7, 0, CPUIDBitMapping::ECX, 28, "movdir64b"},
    {7, 0, CPUIDBitMapping::ECX, 29, "enqcmd"},

    // Leaf 7 sub 0, EDX
    {7, 0, CPUIDBitMapping::EDX,  5, "uintr"},
    {7, 0, CPUIDBitMapping::EDX,  8, "avx512vp2intersect"},
    {7, 0, CPUIDBitMapping::EDX, 14, "serialize"},
    {7, 0, CPUIDBitMapping::EDX, 16, "tsxldtrk"},
    {7, 0, CPUIDBitMapping::EDX, 18, "pconfig"},
    {7, 0, CPUIDBitMapping::EDX, 22, "amx-bf16"},
    {7, 0, CPUIDBitMapping::EDX, 23, "avx512fp16"},
    {7, 0, CPUIDBitMapping::EDX, 24, "amx-tile"},
    {7, 0, CPUIDBitMapping::EDX, 25, "amx-int8"},

    // Leaf 7 sub 1, EAX
    {7, 1, CPUIDBitMapping::EAX,  0, "sha512"},
    {7, 1, CPUIDBitMapping::EAX,  1, "sm3"},
    {7, 1, CPUIDBitMapping::EAX,  2, "sm4"},
    {7, 1, CPUIDBitMapping::EAX,  4, "avxvnni"},
    {7, 1, CPUIDBitMapping::EAX,  5, "avx512bf16"},
    {7, 1, CPUIDBitMapping::EAX,  7, "cmpccxadd"},
    {7, 1, CPUIDBitMapping::EAX, 21, "amx-fp16"},
    {7, 1, CPUIDBitMapping::EAX, 23, "avxifma"},

    // Leaf 7 sub 1, EBX
    {7, 1, CPUIDBitMapping::EBX,  4, "avxvnniint8"},
    {7, 1, CPUIDBitMapping::EBX,  5, "avxneconvert"},
    {7, 1, CPUIDBitMapping::EBX,  8, "amx-complex"},
    {7, 1, CPUIDBitMapping::EBX, 10, "avxvnniint16"},
    {7, 1, CPUIDBitMapping::EBX, 14, "prefetchi"},

    // Extended 0x80000001, ECX
    {0x80000001, 0, CPUIDBitMapping::ECX,  0, "sahf"},
    {0x80000001, 0, CPUIDBitMapping::ECX,  5, "lzcnt"},
    {0x80000001, 0, CPUIDBitMapping::ECX,  6, "sse4a"},
    {0x80000001, 0, CPUIDBitMapping::ECX,  8, "prfchw"},
    {0x80000001, 0, CPUIDBitMapping::ECX, 11, "xop"},
    {0x80000001, 0, CPUIDBitMapping::ECX, 16, "fma4"},
    {0x80000001, 0, CPUIDBitMapping::ECX, 21, "tbm"},
    {0x80000001, 0, CPUIDBitMapping::ECX, 29, "mwaitx"},

    // Leaf 0xD sub 1, EAX (XSAVE)
    {0xd, 1, CPUIDBitMapping::EAX, 0, "xsaveopt"},
    {0xd, 1, CPUIDBitMapping::EAX, 1, "xsavec"},
    {0xd, 1, CPUIDBitMapping::EAX, 3, "xsaves"},

    // Extended 0x80000008, EBX
    {0x80000008, 0, CPUIDBitMapping::EBX, 0, "clzero"},
    {0x80000008, 0, CPUIDBitMapping::EBX, 4, "rdpru"},
    {0x80000008, 0, CPUIDBitMapping::EBX, 9, "wbnoinvd"},
};

// Compute features visible on the currently running core, starting from
// the supplied baseline (CPU-table features + always-present features).
static FeatureBits compute_features_on_current_core(const FeatureBits &baseline) {
    FeatureBits features = baseline;

    unsigned max_leaf = cpuid_max_leaf();
    unsigned max_ext = cpuid_max_ext_leaf();

    // Walk the table, cache CPUID results to avoid redundant calls
    struct {
        unsigned leaf, subleaf;
        CPUIDResult result;
        bool valid;
    } cache[8] = {};
    unsigned cache_count = 0;

    for (const auto &entry : cpuid_features) {
        bool is_ext = (entry.leaf >= 0x80000000);
        unsigned max = is_ext ? max_ext : max_leaf;
        if (entry.leaf > max) continue;

        CPUIDResult r = {};
        bool found = false;
        for (unsigned c = 0; c < cache_count; c++) {
            if (cache[c].leaf == entry.leaf && cache[c].subleaf == entry.subleaf) {
                r = cache[c].result;
                found = true;
                break;
            }
        }
        if (!found) {
            r = cpuid(entry.leaf, entry.subleaf);
            if (cache_count < 8) {
                cache[cache_count] = {entry.leaf, entry.subleaf, r, true};
                cache_count++;
            }
        }

        unsigned reg_val = 0;
        switch (entry.reg) {
        case CPUIDBitMapping::EAX: reg_val = r.eax; break;
        case CPUIDBitMapping::EBX: reg_val = r.ebx; break;
        case CPUIDBitMapping::ECX: reg_val = r.ecx; break;
        case CPUIDBitMapping::EDX: reg_val = r.edx; break;
        }

        if (reg_val & (1u << entry.bit)) {
            const FeatureEntry *f = find_feature(entry.feature_name);
            if (f) feature_set(&features, f->bit);
        }
    }

    // XCR0 validation: the OS must enable state save for AVX/AVX-512/AMX.
    // CPUID reports hardware capability, but XCR0 indicates OS support.
    auto r1 = cpuid(1);
    bool has_xsave = (r1.ecx >> 27) & 1;
    bool has_avx_save = false;
    bool has_avx512_save = false;
    bool has_amx_save = false;

    if (has_xsave) {
        // Read XCR0 via XGETBV(0)
        unsigned xcr0_lo, xcr0_hi;
        __asm__ volatile(".byte 0x0f, 0x01, 0xd0"
                         : "=a"(xcr0_lo), "=d"(xcr0_hi) : "c"(0));

        has_avx_save = (xcr0_lo & 0x6) == 0x6;  // bits 1,2: SSE + AVX state
#if defined(__APPLE__)
        // Darwin lazily saves AVX-512 context on first use
        has_avx512_save = has_avx_save;
#else
        has_avx512_save = has_avx_save && (xcr0_lo & 0xe0) == 0xe0;  // bits 5,6,7
#endif
        has_amx_save = has_xsave && (xcr0_lo & ((1 << 17) | (1 << 18))) == ((1 << 17) | (1 << 18));
    }

    // Disable features that require OS state save support
    auto disable_feature = [&](const char *name) {
        const FeatureEntry *fe = find_feature(name);
        if (fe) feature_clear(&features, fe->bit);
    };

    if (!has_avx_save) {
        static const char *avx_features[] = {
            "avx", "avx2", "fma", "f16c", "fma4", "xop",
            "vaes", "vpclmulqdq", "xsave", "xsaveopt", "xsavec", "xsaves",
            nullptr
        };
        for (const char **f = avx_features; *f; f++) disable_feature(*f);
        has_avx512_save = false;
    }

    if (!has_avx512_save) {
        static const char *avx512_features[] = {
            "avx512f", "avx512dq", "avx512ifma", "avx512cd",
            "avx512bw", "avx512vl", "avx512vbmi", "avx512vpopcntdq",
            "avx512vbmi2", "avx512vnni", "avx512bitalg",
            "avx512vp2intersect", "avx512bf16", "avx512fp16",
            "evex512", nullptr
        };
        for (const char **f = avx512_features; *f; f++) disable_feature(*f);
    }

    if (!has_amx_save) {
        static const char *amx_features[] = {
            "amx-tile", "amx-int8", "amx-bf16", "amx-fp16",
            "amx-complex", "amx-fp8", "amx-transpose", "amx-avx512",
            "amx-tf32", "amx-movrs", nullptr
        };
        for (const char **f = amx_features; *f; f++) disable_feature(*f);
    }

    // AVX-512 implies evex512 (only if not already disabled above)
    const FeatureEntry *avx512f = find_feature("avx512f");
    if (avx512f && feature_test(&features, avx512f->bit)) {
        const FeatureEntry *evex512 = find_feature("evex512");
        if (evex512) feature_set(&features, evex512->bit);
    }

    return features;
}

// Iterate all available cores that this process may be scheduled onto,
// even those outside the current CPU affinity, and invoke the provided
// callback to measure the features on each core.
template <typename Fn>
static void for_each_schedulable_cpu(Fn &&fn) {
#if defined(CPUFEATURES_AFFINITY_LINUX)
    cpu_set_t old_mask;
    CPU_ZERO(&old_mask);
    if (sched_getaffinity(0, sizeof(old_mask), &old_mask) != 0) {
        fn();
        return;
    }
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu <= 0) ncpu = CPU_SETSIZE;
    if (ncpu > CPU_SETSIZE) ncpu = CPU_SETSIZE;
    if (ncpu <= 1) {
        fn();
        return;
    }
    bool ran = false;
    for (long cpu = 0; cpu < ncpu; cpu++) {
        cpu_set_t one;
        CPU_ZERO(&one);
        CPU_SET(cpu, &one);
        if (sched_setaffinity(0, sizeof(one), &one) != 0) continue;
        fn();
        ran = true;
    }
    sched_setaffinity(0, sizeof(old_mask), &old_mask);
    if (!ran) fn();
#elif defined(CPUFEATURES_AFFINITY_WIN32)
    // On Windows a thread can't widen past its process mask, so the
    // process mask is the upper bound of schedulable CPUs.
    DWORD_PTR proc_mask = 0, sys_mask = 0;
    HANDLE thread = GetCurrentThread();
    if (!GetProcessAffinityMask(GetCurrentProcess(), &proc_mask, &sys_mask) || proc_mask == 0) {
        fn();
        return;
    }
    int n = 0;
    for (DWORD_PTR m = proc_mask; m; m &= m - 1) n++;
    if (n <= 1) {
        fn();
        return;
    }
    // Windows has no GetThreadAffinityMask; SetThreadAffinityMask returns
    // the previous mask, so we read-modify-write to capture the original.
    DWORD_PTR saved = SetThreadAffinityMask(thread, proc_mask);
    if (saved == 0) {
        fn();
        return;
    }
    bool ran = false;
    for (unsigned cpu = 0; cpu < sizeof(DWORD_PTR) * 8; cpu++) {
        DWORD_PTR bit = (DWORD_PTR)1 << cpu;
        if (!(proc_mask & bit)) continue;
        if (SetThreadAffinityMask(thread, bit) == 0) continue;
        // Windows applies the new affinity at the next dispatch; yield
        // to force a migration before CPUID runs.
        SwitchToThread();
        fn();
        ran = true;
    }
    SetThreadAffinityMask(thread, saved);
    if (!ran) fn();
#else
    assert(false);
    __builtin_unreachable();
#endif
}

FeatureBits get_host_features() {
    static bool cached_valid = false;
    static FeatureBits cached;
    if (cached_valid) return cached;

    FeatureBits features{};

    // Baseline features always present
    static const char *baseline_features[] = {
#if defined(__x86_64__) || defined(_M_X64)
        "64bit",
#endif
        "cx8", "cmov", "fxsr", "mmx", "sse", "sse2", "x87"
    };
    auto apply_baseline = [&](FeatureBits &fb) {
        for (const char *name : baseline_features) {
            const FeatureEntry *f = find_feature(name);
            if (f) feature_set(&fb, f->bit);
        }
    };
    apply_baseline(features);

#if defined(__i386__) || defined(_M_IX86)
    // On 32-bit, just return baseline features.
    // Full detection is not worth the complexity on this legacy platform.
    cached = features;
    cached_valid = true;
    return features;
#endif

    // Start with the detected CPU's features from the table.
    // This gives us non-CPUID features like nopl, ermsb, etc.
    const auto &cpu = get_host_cpu_name();
    const CPUEntry *entry = _find_cpu_exact(cpu.c_str());
    if (entry)
        features = entry->features;

    // Re-apply baseline (table may not include all of them)
    apply_baseline(features);

    // Detect CPUID/XCR0-derived features (on all cores, if necessary)
    FeatureBits all_cpu_features = compute_features_on_current_core(features);
    if (cpu_is_hybrid_arch()) {
        // Intersect CPUID/XCR0-derived features across every CPU we are
        // allowed to run on. On hybrid CPUs the per-core results differ, and
        // callers need a feature set that holds on any core they may be
        // scheduled to later.
        for_each_schedulable_cpu([&]() {
            FeatureBits per_core = compute_features_on_current_core(features);
            for (unsigned i = 0; i < TARGET_FEATURE_WORDS; i++)
                all_cpu_features.bits[i] &= per_core.bits[i];
        });
    }
    features = all_cpu_features;

    expand_implied(&features);
    cached = features;
    cached_valid = true;
    return features;
}

} // namespace tp
