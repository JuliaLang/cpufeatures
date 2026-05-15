// Host CPU detection for x86_64.
// Standalone - no LLVM dependency.
// Uses CPUID to detect CPU name and features.

#include "target_tables_x86_64.h"
#include "target_parsing.h"

#include <array>
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

// Test a single CPUID feature bit. Returns false if the leaf isn't supported
// or the bit is clear. `reg` is one of 'a','b','c','d'.
static bool cpuid_bit(unsigned leaf, unsigned subleaf, char reg, unsigned bit) {
    if (leaf < 0x80000000u) {
        if (cpuid_max_leaf() < leaf) return false;
    } else {
        if (cpuid_max_ext_leaf() < leaf) return false;
    }
    if (leaf == 7 && subleaf > cpuid(7, 0).eax) return false;
    auto r = cpuid(leaf, subleaf);
    unsigned val = 0;
    switch (reg) {
        case 'a': val = r.eax; break;
        case 'b': val = r.ebx; break;
        case 'c': val = r.ecx; break;
        case 'd': val = r.edx; break;
    }
    return ((val >> bit) & 1u) != 0;
}

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

// Model number -> CPU name. Mapping mirrors LLVM's
// getIntelProcessorTypeAndSubtype() (llvm/lib/TargetParser/Host.cpp).
static const char *detect_intel_cpu(const CPUModel &m) {
    // Family 15 (NetBurst): Pentium 4 / Xeon / Nocona.
    if (m.family == 15) {
#if defined(__x86_64__) || defined(_M_X64)
        return "nocona"; // any family-15 chip running x86_64 is Nocona-class
#else
        return cpuid_bit(1, 0, 'c', 0) ? "prescott" : "pentium4"; // sse3
#endif
    }

    // Family 19 (Diamond Rapids).
    if (m.family == 19) {
        if (m.model == 0x01) return "diamondrapids";
        return "generic";
    }

    if (m.family != 6) return "generic";

    switch (m.model) {
    case 0x0e: return "yonah";
    case 0x0f:
    case 0x16: return "core2";
    case 0x17: // Penryn / Wolfdale / Yorkfield
    case 0x1d: return "penryn";
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
    case 0x4e: // Skylake mobile
    case 0x5e: // Skylake desktop
    case 0x8e: // Kaby Lake mobile
    case 0x9e: // Kaby Lake desktop
    case 0xa5: // Comet Lake-H/S
    case 0xa6: return "skylake"; // Comet Lake-U
    case 0xa7: return "rocketlake";
    case 0x55: // SKX / CLX / CPX — distinguished by feature bits, not stepping
        if (cpuid_bit(7, 1, 'a', 5))  return "cooperlake";   // avx512bf16
        if (cpuid_bit(7, 0, 'c', 11)) return "cascadelake";  // avx512vnni
        return "skylake-avx512";
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
    case 0xbe: return "gracemont";
    case 0xb7:
    case 0xba:
    case 0xbf: return "raptorlake";
    case 0xaa:
    case 0xac: return "meteorlake";
    case 0xbd: return "lunarlake";
    case 0xc5:
    case 0xb5: return "arrowlake";
    case 0xc6: return "arrowlake-s";
    case 0xcc: return "pantherlake";
    case 0xcf: return "emeraldrapids";
    case 0xad: return "graniterapids";
    case 0xae: return "graniterapids-d";

    // Atom / Bonnell line
    case 0x1c: // 45 nm Atom
    case 0x26: // 45 nm Atom Lincroft
    case 0x27: // 32 nm Atom Medfield
    case 0x35: // 32 nm Atom Midview
    case 0x36: return "bonnell";

    // Silvermont / Airmont (LLVM uses the silvermont tune for both)
    case 0x37:
    case 0x4a:
    case 0x4d:
    case 0x5a:
    case 0x5d:
    case 0x4c: return "silvermont"; // Airmont
    case 0x5c: // Apollo Lake
    case 0x5f: return "goldmont"; // Denverton
    case 0x7a: return "goldmont-plus";

    // Tremont
    case 0x86: // Snow Ridge / Jacobsville
    case 0x8a: // Lakefield
    case 0x96: // Elkhart Lake
    case 0x9c: return "tremont"; // Jasper Lake

    case 0xaf: return "sierraforest";
    case 0xb6: return "grandridge";
    case 0xdd: return "clearwaterforest";

    // Xeon Phi
    case 0x57: return "knl";
    case 0x85: return "knm";

    default: return "generic";
    }
}

// Model -> CPU name. Mapping mirrors LLVM's
// getAMDProcessorTypeAndSubtype() (llvm/lib/TargetParser/Host.cpp).
static const char *detect_amd_cpu(const CPUModel &m) {
    switch (m.family) {
    // Family 6 (K7) and 15 (K8) need feature probes for the SSE/SSE3 split.
    // 64-bit AMD chips start at K8 with SSE2 mandatory; family 6 only appears
    // on 32-bit hosts.
    case 6: {
        auto r = cpuid(1);
        bool has_sse = (r.edx >> 25) & 1;
        return has_sse ? "athlon-xp" : "athlon";
    }
    case 15: {
        auto r = cpuid(1);
        bool has_sse3 = r.ecx & 1;
        return has_sse3 ? "k8-sse3" : "k8";
    }

    case 0x10: // K10
    case 0x12: return "amdfam10"; // Llano (also K10-derived)
    case 0x14: return "btver1";

    case 0x15: // Bulldozer family
        if (m.model >= 0x60 && m.model <= 0x7f) return "bdver4"; // Excavator
        if (m.model >= 0x30 && m.model <= 0x3f) return "bdver3"; // Steamroller
        if ((m.model >= 0x10 && m.model <= 0x1f) || m.model == 0x02)
            return "bdver2"; // Piledriver
        return "bdver1";     // Bulldozer

    case 0x16: return "btver2";

    case 0x17: // Zen / Zen+ / Zen2
        // Zen2: 30h-3Fh (Starship), 47h (Cardinal), 60h-6Fh (Renoir),
        // 68h-6Fh (Lucienne), 70h-7Fh (Matisse), 84h-87h (ProjectX),
        // 90h-9Fh (VanGogh / Mero), A0h-AFh (Mendocino).
        if ((m.model >= 0x30 && m.model <= 0x3f) || m.model == 0x47 ||
            (m.model >= 0x60 && m.model <= 0x7f) ||
            (m.model >= 0x84 && m.model <= 0x87) ||
            (m.model >= 0x90 && m.model <= 0x9f) ||
            (m.model >= 0xa0 && m.model <= 0xaf))
            return "znver2";
        return "znver1"; // Zen / Zen+ : 10h-2Fh

    case 0x19: // Zen3 / Zen4
        // Zen3: 00h-0Fh (Genesis/Chagall), 20h-2Fh (Vermeer),
        // 30h-3Fh (Badami), 40h-4Fh (Rembrandt), 50h-5Fh (Cezanne).
        if (m.model <= 0x0f ||
            (m.model >= 0x20 && m.model <= 0x5f))
            return "znver3";
        // Zen4: 10h-1Fh (Stones/Storm Peak), 60h-6Fh (Raphael),
        // 70h-7Fh (Phoenix/Hawkpoint), A0h-AFh (Stones-Dense).
        if ((m.model >= 0x10 && m.model <= 0x1f) ||
            (m.model >= 0x60 && m.model <= 0x7f) ||
            (m.model >= 0xa0 && m.model <= 0xaf))
            return "znver4";
        return "znver3";

    case 0x1a: // Zen5 (no znver6 in the cpufeatures LLVM-21.1.8 tables yet)
        return "znver5";

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
    // {7, 0, CPUIDBitMapping::EBX, 10, "invpcid"},  // privileged instruction
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
    {7, 0, CPUIDBitMapping::ECX, 23, "kl"},
    {7, 0, CPUIDBitMapping::ECX, 25, "cldemote"},
    {7, 0, CPUIDBitMapping::ECX, 27, "movdiri"},
    {7, 0, CPUIDBitMapping::ECX, 28, "movdir64b"},
    {7, 0, CPUIDBitMapping::ECX, 29, "enqcmd"},

    // Leaf 7 sub 0, EDX
    {7, 0, CPUIDBitMapping::EDX,  5, "uintr"},
    {7, 0, CPUIDBitMapping::EDX,  8, "avx512vp2intersect"},
    {7, 0, CPUIDBitMapping::EDX, 14, "serialize"},
    {7, 0, CPUIDBitMapping::EDX, 16, "tsxldtrk"},
    // {7, 0, CPUIDBitMapping::EDX, 18, "pconfig"},  // privileged instruction
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

    // Leaf 0x14 sub 0, EBX (Intel Processor Trace)
    {0x14, 0, CPUIDBitMapping::EBX, 4, "ptwrite"},

    // Leaf 0x19 sub 0, EBX (Key Locker capabilities)
    {0x19, 0, CPUIDBitMapping::EBX, 2, "widekl"},

    // Extended 0x80000008, EBX
    {0x80000008, 0, CPUIDBitMapping::EBX, 0, "clzero"},
    {0x80000008, 0, CPUIDBitMapping::EBX, 4, "rdpru"},
    // {0x80000008, 0, CPUIDBitMapping::EBX, 9, "wbnoinvd"},  // privileged instruction
};

// Compute features visible on the currently running core, starting from
// the supplied baseline (CPU-table features + always-present features).
static FeatureBits compute_features_on_current_core(const FeatureBits &baseline) {
    FeatureBits features = baseline;
    FeatureBits to_enable{};
    FeatureBits to_disable{};

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
        const FeatureEntry *f = find_feature(entry.feature_name);
        assert(f && "cpuid_features names a feature missing from the table");

        bool is_ext = (entry.leaf >= 0x80000000);
        unsigned max = is_ext ? max_ext : max_leaf;
        if (entry.leaf > max) {
            feature_set(&to_disable, f->bit);
            continue;
        }
        if (entry.leaf == 7 && entry.subleaf > cpuid(7, 0).eax) {
            feature_set(&to_disable, f->bit);
            continue;
        }
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
        if ((reg_val & (1u << entry.bit)) != 0)
            feature_set(&to_enable, f->bit);
        else
            feature_set(&to_disable, f->bit);
    }

    // XCR0 validation: the OS must enable state save for AVX/AVX-512/AMX.
    // CPUID reports hardware capability, but XCR0 indicates OS support.
    auto r1 = cpuid(1);
    bool has_xsave = (r1.ecx >> 27) & 1;
    bool has_avx_save = false;
    bool has_avx512_save = false;
    bool has_amx_save = false;
    bool has_aeskle_save = false;

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
        // Not an XCR0 bit, but similarly communicates OS context switching support
        has_aeskle_save = (max_leaf >= 0x19) && (cpuid(0x19, 0).ebx & 1);
    }

    if (!has_avx_save) {
        feature_set(&to_disable, find_feature("avx")->bit);
        feature_set(&to_disable, find_feature("xsave")->bit);
        has_avx512_save = false;
    }
    if (!has_avx512_save) {
        feature_set(&to_disable, find_feature("avx512f")->bit);
        feature_set(&to_disable, find_feature("evex512")->bit);
    }
    if (!has_amx_save)    feature_set(&to_disable, find_feature("amx-tile")->bit);
    if (!has_aeskle_save) feature_set(&to_disable, find_feature("kl")->bit);

    // AVX-512 implies evex512
    if (feature_test(&to_enable, find_feature("avx512f")->bit))
        feature_set(&to_enable, find_feature("evex512")->bit);

    apply_feature_delta(&features, to_enable, to_disable);
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
    (void)fn;
    assert(false);
    __builtin_unreachable();
#endif
}

static const char *const baseline_features[] = {
#if defined(__x86_64__) || defined(_M_X64)
    "64bit",
#endif
    "cx8", "cmov", "fxsr", "mmx", "sse", "sse2", "x87",
    nullptr
};

FeatureBits detect_host_features() {
    FeatureBits features{};
    apply_host_baseline(&features);

#if defined(__i386__) || defined(_M_IX86)
    // On 32-bit, just return baseline features.
    // Full detection is not worth the complexity on this legacy platform.
    return features;
#endif

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

    return features;
}

const char *const *get_host_feature_detection(HostFeatureDetectionKind kind) {
    static const char *empty[] = { nullptr };
    switch (kind) {
    case HOST_FEATURE_BASELINE:
        return baseline_features;
    case HOST_FEATURE_DETECTABLE: {
        constexpr size_t N = sizeof(cpuid_features) / sizeof(cpuid_features[0]);
        static const auto names = []() {
            std::array<const char *, N + 1> a{};
            for (size_t i = 0; i < N; i++) a[i] = cpuid_features[i].feature_name;
            a[N] = nullptr;
            return a;
        }();
        return names.data();
    }
    case HOST_FEATURE_DETECTABLE_BY_IMPLICATION_ONLY: {
        static const char *names[] = { nullptr };
        return names;
    }
    case HOST_FEATURE_UNDETECTABLE: {
        static const char *names[] = {
            // AMX extensions detectable at CPUID(0x1E,1).EAX[4..8].
            "amx-transpose", // removed from architecture
            "evex512", // pseudo-feature used by LLVM

            // FIXME: Unimplemented detection
            "amx-fp8", "amx-tf32", "amx-avx512", "amx-movrs",
            "avx10.1-256", "avx10.1-512", "avx10.2-256", "avx10.2-512",
            "ccmp", "cf", "egpr", "ndd", "nf", "ppx", "push2pop2", "zu",
            "lwp", "movrs", "usermsr",

            nullptr
        };
        return names;
    }
    }
    return empty;
}

} // namespace tp
