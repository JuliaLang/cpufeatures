// Host CPU detection for AArch64.
// Standalone - no LLVM dependency.
// Supports Linux (/proc/cpuinfo), macOS (sysctlbyname), Windows (stubs).

#include "target_tables_aarch64.h"
#include "target_parsing.h"

#include <array>
#include <cassert>
#include <cstring>
#include <vector>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fstream>
#include <sys/auxv.h>
#endif

// ============================================================================
// macOS: CPU detection via sysctlbyname
// ============================================================================

#if defined(__APPLE__)

#define CPUFAMILY_ARM_FIRESTORM_ICESTORM 0x1b588bb3 // M1/A14
#define CPUFAMILY_ARM_BLIZZARD_AVALANCHE 0xda33d83d // M2/A15
#define CPUFAMILY_ARM_EVEREST_SAWTOOTH   0x8765edea // A16
#define CPUFAMILY_ARM_IBIZA              0xfa33415e // M3
#define CPUFAMILY_ARM_LOBOS              0x5f4dea93 // M3 Pro
#define CPUFAMILY_ARM_PALMA              0x72015832 // M3 Max
#define CPUFAMILY_ARM_COLL               0x2876f5b5 // A17 Pro
#define CPUFAMILY_ARM_DONAN              0x6f5129ac // M4
#define CPUFAMILY_ARM_BRAVA              0x17d5b93a // M4 Pro/Max
#define CPUFAMILY_ARM_TAHITI             0x75d4acb9 // A18 Pro
#define CPUFAMILY_ARM_TUPAI              0x204526d0 // A18
#define CPUFAMILY_ARM_HIDRA              0x1d5a87e8 // M5
#define CPUFAMILY_ARM_SOTRA              0xf76c5b1a // M5 Pro/Max
#define CPUFAMILY_ARM_THERA              0xab345f09 // A19 Pro
#define CPUFAMILY_ARM_TILOS              0x01d7a72b // A19

namespace tp {

const std::string &get_host_cpu_name() {
    static std::string cpu_name;
    if (!cpu_name.empty()) return cpu_name;

    uint32_t family = 0;
    size_t len = sizeof(family);
    sysctlbyname("hw.cpufamily", &family, &len, NULL, 0);

    const char *name;
    switch (family) {
    case CPUFAMILY_ARM_FIRESTORM_ICESTORM: name = "apple-m1"; break;
    case CPUFAMILY_ARM_BLIZZARD_AVALANCHE: name = "apple-m2"; break;
    case CPUFAMILY_ARM_EVEREST_SAWTOOTH: name = "apple-a16"; break;
    case CPUFAMILY_ARM_IBIZA:
    case CPUFAMILY_ARM_PALMA:
    case CPUFAMILY_ARM_LOBOS:
        name = "apple-m3"; break;
    case CPUFAMILY_ARM_COLL:
        name = "apple-a17"; break;
    case CPUFAMILY_ARM_DONAN:
    case CPUFAMILY_ARM_BRAVA:
        name = "apple-m4"; break;
    // case CPUFAMILY_ARM_TAHITI:
    // case CPUFAMILY_ARM_TUPAI:
    //     name = "apple-a18"; break; // Uncomment once tables are generated from LLVM 21
    // case CPUFAMILY_ARM_HIDRA:
    // case CPUFAMILY_ARM_SOTRA:
    //     name = "apple-m5"; break; // Uncomment once tables are generated from LLVM 22
    // case CPUFAMILY_ARM_THERA:
    // case CPUFAMILY_ARM_TILOS:
    //     name = "apple-a19"; break; // Uncomment once tables are generated from LLVM 22
    default:
        name = "apple-m1"; break;
    }

    // Resolve alias and verify the CPU exists in the table.
    if (!find_cpu(name)) {
        name = "apple-m1";
    }

    cpu_name = name;
    return cpu_name;
}

// hw.optional.arm.caps bit definitions from XNU's cpu_capabilities_public.h.
// This is a bitbuffer (CAP_BIT_NB = 80 bits = 10 bytes) queried in one
// sysctlbyname call. Each entry maps a cap bit to an LLVM feature name.
struct CapBitMap { unsigned cap_bit; const char *llvm_name; };
static const CapBitMap cap_bit_map[] = {
    { 0,  "flagm"},        // CAP_BIT_FEAT_FlagM
    // { 1,  "flagm2"},    // CAP_BIT_FEAT_FlagM2 — no separate LLVM feature
    { 2,  "fp16fml"},      // CAP_BIT_FEAT_FHM
    { 3,  "dotprod"},      // CAP_BIT_FEAT_DotProd
    { 4,  "sha3"},         // CAP_BIT_FEAT_SHA3
    { 5,  "rdm"},          // CAP_BIT_FEAT_RDM
    { 6,  "lse"},          // CAP_BIT_FEAT_LSE
    { 7,  "sha2"},         // CAP_BIT_FEAT_SHA256
    // { 8, "sha512"},     // CAP_BIT_FEAT_SHA512 — no separate LLVM feature (part of sha3)
    // { 9, "sha1"},       // CAP_BIT_FEAT_SHA1 — no separate LLVM feature (part of sha2)
    {10,  "aes"},          // CAP_BIT_FEAT_AES
    // {11, "pmull"},      // CAP_BIT_FEAT_PMULL — part of AES extension
    // {12, "specres"},    // CAP_BIT_FEAT_SPECRES — not codegen-relevant
    {13,  "sb"},           // CAP_BIT_FEAT_SB
    {14,  "fptoint"},      // CAP_BIT_FEAT_FRINTTS
    {15,  "rcpc"},         // CAP_BIT_FEAT_LRCPC
    {16,  "rcpc-immo"},    // CAP_BIT_FEAT_LRCPC2
    {17,  "complxnum"},    // CAP_BIT_FEAT_FCMA
    {18,  "jsconv"},       // CAP_BIT_FEAT_JSCVT
    {19,  "pauth"},        // CAP_BIT_FEAT_PAuth
    // {20, "pauth2"},     // CAP_BIT_FEAT_PAuth2 — no separate LLVM feature
    {21,  "fpac"},         // CAP_BIT_FEAT_FPAC
    {22,  "ccpp"},         // CAP_BIT_FEAT_DPB
    {23,  "ccdp"},         // CAP_BIT_FEAT_DPB2
    {24,  "bf16"},         // CAP_BIT_FEAT_BF16
    {25,  "i8mm"},         // CAP_BIT_FEAT_I8MM
    {26,  "wfxt"},         // CAP_BIT_FEAT_WFxT
    // {27, "rpres"},      // CAP_BIT_FEAT_RPRES — not an LLVM feature
    {28,  "ecv"},          // CAP_BIT_FEAT_ECV
    // {29, "afp"},        // CAP_BIT_FEAT_AFP — not an LLVM codegen feature
    {30,  "lse2"},         // CAP_BIT_FEAT_LSE2
    // {31, "csv2"},       // CAP_BIT_FEAT_CSV2 — not codegen-relevant
    // {32, "csv3"},       // CAP_BIT_FEAT_CSV3 — not codegen-relevant
    {33,  "dit"},          // CAP_BIT_FEAT_DIT
    {34,  "fullfp16"},     // CAP_BIT_FEAT_FP16
    // {35, "ssbs"},       // CAP_BIT_FEAT_SSBS — not codegen-relevant
    {36,  "bti"},          // CAP_BIT_FEAT_BTI
    {40,  "sme"},          // CAP_BIT_FEAT_SME
    {41,  "sme2"},         // CAP_BIT_FEAT_SME2
    {42,  "sme-f64f64"},   // CAP_BIT_FEAT_SME_F64F64
    {43,  "sme-i16i64"},   // CAP_BIT_FEAT_SME_I16I64
    {49,  "neon"},         // CAP_BIT_AdvSIMD
    {51,  "crc"},          // CAP_BIT_FEAT_CRC32
    {64,  "hbc"},          // CAP_BIT_FEAT_HBC
    // {65, "ebf16"},      // CAP_BIT_FEAT_EBF16 — no separate LLVM feature
    // {66, "specres2"},   // CAP_BIT_FEAT_SPECRES2 — not codegen-relevant
    {67,  "cssc"},         // CAP_BIT_FEAT_CSSC
    {0, nullptr}           // sentinel
};

static bool cap_test(const uint8_t *caps, size_t caps_len, unsigned bit) {
    if (bit / 8 >= caps_len) return false;
    return (caps[bit / 8] >> (bit % 8)) & 1;
}

FeatureBits get_host_features() {
    FeatureBits features{};

    const auto &cpu = get_host_cpu_name();
    const CPUEntry *entry = find_cpu(cpu.c_str());
    if (entry)
        features = entry->features;

    // Read the full caps bitbuffer in one sysctlbyname call.
    // Query size first — Apple may extend the buffer in future OS versions.
    FeatureBits to_enable{};
    FeatureBits to_disable{};
    size_t caps_len = 0;
    if (sysctlbyname("hw.optional.arm.caps", nullptr, &caps_len, nullptr, 0) == 0
            && caps_len > 0) {
        std::vector<uint8_t> caps(caps_len, 0);
        if (sysctlbyname("hw.optional.arm.caps", caps.data(), &caps_len, nullptr, 0) == 0) {
            for (const auto *m = cap_bit_map; m->llvm_name; m++) {
                const FeatureEntry *fe = find_feature(m->llvm_name);
                assert(fe && "could not find feature in cap_bit_map");
                if (cap_test(caps.data(), caps_len, m->cap_bit))
                    feature_set(&to_enable, fe->bit);
                else
                    feature_set(&to_disable, fe->bit);
            }
        }
    }

    apply_feature_delta(&features, to_enable, to_disable);
    return features;
}

const char *const *get_host_feature_detection(HostFeatureDetectionKind kind) {
    static const char *empty[] = { nullptr };
    switch (kind) {
    case HOST_FEATURE_BASELINE: {
        static const char *names[] = { "fp-armv8", nullptr };
        return names;
    }
    case HOST_FEATURE_DETECTABLE: {
        constexpr size_t N = sizeof(cap_bit_map) / sizeof(cap_bit_map[0]);
        static const auto names = []() {
            std::array<const char *, N + 1> a{};
            size_t n = 0;
            for (const auto *m = cap_bit_map; m->llvm_name; m++)
                a[n++] = m->llvm_name;
            a[n] = nullptr;
            return a;
        }();
        return names.data();
    }
    case HOST_FEATURE_UNDETECTABLE: {
        static const char *names[] = {
            // Never present on Apple Silicon.
            "sve", "sve-aes", "sve-bitperm",
            "sve2", "sve2-sha3", "sve2-sm4",
            "mte", "rand", "sm4",
            // No runtime probe support available yet.
            "altnzcv", "clrbhb", "faminmax", "lut",
            "fp8", "fp8dot2", "fp8dot4", "fp8fma", "ls64",
            "lor", "ras", "predres", "specres2", "specrestrict",

            nullptr
        };
        return names;
    }
    }
    return empty;
}

} // namespace tp

// ============================================================================
// Windows AArch64: CPU detection
// ============================================================================

#elif defined(_WIN32)

namespace tp {

const std::string &get_host_cpu_name() {
    static std::string cpu_name = "generic";
    return cpu_name;
}

struct PFCapMap { DWORD pf; const char *llvm_name; };
static const PFCapMap pf_cap_map[] = {
    {30, "aes"},            // PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE
    {30, "sha2"},           // PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE
    {31, "crc"},            // PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE
    {34, "lse"},            // PF_ARM_V81_ATOMIC_INSTRUCTIONS_AVAILABLE
    {43, "dotprod"},        // PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE
    {44, "jsconv"},         // PF_ARM_V83_JSCVT_INSTRUCTIONS_AVAILABLE
    {45, "rcpc"},           // PF_ARM_V83_LRCPC_INSTRUCTIONS_AVAILABLE
    {46, "sve"},            // PF_ARM_SVE_INSTRUCTIONS_AVAILABLE
    {47, "sve2"},           // PF_ARM_SVE2_INSTRUCTIONS_AVAILABLE
    {48, "sve2p1"},         // PF_ARM_SVE2_1_INSTRUCTIONS_AVAILABLE
    {49, "sve-aes"},        // PF_ARM_SVE_AES_INSTRUCTIONS_AVAILABLE
    {51, "sve-bitperm"},    // PF_ARM_SVE_BITPERM_INSTRUCTIONS_AVAILABLE
    {54, "sve-b16b16"},     // PF_ARM_SVE_B16B16_INSTRUCTIONS_AVAILABLE
    {55, "sve2-sha3"},      // PF_ARM_SVE_SHA3_INSTRUCTIONS_AVAILABLE
    {56, "sve2-sm4"},       // PF_ARM_SVE_SM4_INSTRUCTIONS_AVAILABLE
    {58, "f32mm"},          // PF_ARM_SVE_F32MM_INSTRUCTIONS_AVAILABLE
    {59, "f64mm"},          // PF_ARM_SVE_F64MM_INSTRUCTIONS_AVAILABLE
    {62, "lse2"},           // PF_ARM_LSE2_AVAILABLE
    {64, "sha3"},           // PF_ARM_SHA3_INSTRUCTIONS_AVAILABLE
    {66, "i8mm"},           // PF_ARM_V82_I8MM_INSTRUCTIONS_AVAILABLE
    {67, "fullfp16"},       // PF_ARM_V82_FP16_INSTRUCTIONS_AVAILABLE
    {68, "bf16"},           // PF_ARM_V86_BF16_INSTRUCTIONS_AVAILABLE
    {70, "sme"},            // PF_ARM_SME_INSTRUCTIONS_AVAILABLE
    {71, "sme2"},           // PF_ARM_SME2_INSTRUCTIONS_AVAILABLE
    {72, "sme2p1"},         // PF_ARM_SME2_1_INSTRUCTIONS_AVAILABLE
    {73, "sme2p2"},         // PF_ARM_SME2_2_INSTRUCTIONS_AVAILABLE
    {74, "ssve-aes"},       // PF_ARM_SME_AES_INSTRUCTIONS_AVAILABLE
    {75, "ssve-bitperm"},   // PF_ARM_SME_SBITPERM_INSTRUCTIONS_AVAILABLE
    {78, "ssve-fp8dot2"},   // PF_ARM_SME_SF8DP2_INSTRUCTIONS_AVAILABLE
    {79, "ssve-fp8dot4"},   // PF_ARM_SME_SF8DP4_INSTRUCTIONS_AVAILABLE
    {80, "ssve-fp8fma"},    // PF_ARM_SME_SF8FMA_INSTRUCTIONS_AVAILABLE
    {81, "sme-f8f32"},      // PF_ARM_SME_F8F32_INSTRUCTIONS_AVAILABLE
    {82, "sme-f8f16"},      // PF_ARM_SME_F8F16_INSTRUCTIONS_AVAILABLE
    {83, "sme-f16f16"},     // PF_ARM_SME_F16F16_INSTRUCTIONS_AVAILABLE
    {84, "sme-b16b16"},     // PF_ARM_SME_B16B16_INSTRUCTIONS_AVAILABLE
    {85, "sme-f64f64"},     // PF_ARM_SME_F64F64_INSTRUCTIONS_AVAILABLE
    {86, "sme-i16i64"},     // PF_ARM_SME_I16I64_INSTRUCTIONS_AVAILABLE
    {87, "sme-lutv2"},      // PF_ARM_SME_LUTv2_INSTRUCTIONS_AVAILABLE
    {88, "sme-fa64"},       // PF_ARM_SME_FA64_INSTRUCTIONS_AVAILABLE
    {0, nullptr}
};

FeatureBits get_host_features() {
    FeatureBits features{};
    FeatureBits to_enable{};
    FeatureBits to_disable{};

    // Windows ABI mandates neon + fp-armv8
    feature_set(&to_enable, find_feature("neon")->bit);
    feature_set(&to_enable, find_feature("fp-armv8")->bit);

    for (const auto *m = pf_cap_map; m->llvm_name; m++) {
        const FeatureEntry *fe = find_feature(m->llvm_name);
        assert(fe && "pf_cap_map names a feature missing from the table");
        if (IsProcessorFeaturePresent(m->pf))
            feature_set(&to_enable, fe->bit);
        else
            feature_set(&to_disable, fe->bit);
    }

    apply_feature_delta(&features, to_enable, to_disable);
    return features;
}

const char *const *get_host_feature_detection(HostFeatureDetectionKind kind) {
    static const char *empty[] = { nullptr };
    switch (kind) {
    case HOST_FEATURE_BASELINE: {
        static const char *names[] = { "neon", "fp-armv8", nullptr };
        return names;
    }
    case HOST_FEATURE_DETECTABLE: {
        constexpr size_t N = sizeof(pf_cap_map) / sizeof(pf_cap_map[0]);
        static const auto names = []() {
            std::array<const char *, N + 1> a{};
            size_t n = 0;
            for (const auto *m = pf_cap_map; m->llvm_name; m++)
                a[n++] = m->llvm_name;
            a[n] = nullptr;
            return a;
        }();
        return names.data();
    }
    case HOST_FEATURE_UNDETECTABLE: {
        // HW features LLVM knows about for AArch64 that have no
        // corresponding IsProcessorFeaturePresent flag.
        static const char *names[] = {
            "altnzcv", "ccdp", "ccpp", "clrbhb", "complxnum", "cssc",
            "dit", "ecv", "faminmax", "flagm", "fp16fml", "fp8dot2",
            "fp8dot4", "fp8fma", "fpac", "fptoint", "lor", "ls64",
            "lut", "mte", "pauth", "predres", "rand", "ras",
            "rcpc-immo", "rdm", "sb", "specres2", "specrestrict", "wfxt",
            nullptr
        };
        return names;
    }
    }
    return empty;
}

} // namespace tp

// ============================================================================
// Linux AArch64: CPU detection via /proc/cpuinfo
// FreeBSD AArch64: CPU detection via MIDR_EL1
// ============================================================================

#else // Linux or FreeBSD

struct ArmCPUInfo {
    unsigned implementer;
    unsigned part;
    const char *name;
};

static const ArmCPUInfo arm_cpus[] = {
    // ARM Ltd. (0x41)
    {0x41, 0xd03, "cortex-a53"},
    {0x41, 0xd04, "cortex-a35"},
    {0x41, 0xd05, "cortex-a55"},
    {0x41, 0xd06, "cortex-a65"},
    {0x41, 0xd07, "cortex-a57"},
    {0x41, 0xd08, "cortex-a72"},
    {0x41, 0xd09, "cortex-a73"},
    {0x41, 0xd0a, "cortex-a75"},
    {0x41, 0xd0b, "cortex-a76"},
    {0x41, 0xd0c, "neoverse-n1"},
    {0x41, 0xd0d, "cortex-a77"},
    {0x41, 0xd40, "neoverse-v1"},
    {0x41, 0xd41, "cortex-a78"},
    {0x41, 0xd44, "cortex-x1"},
    {0x41, 0xd46, "cortex-a510"},
    {0x41, 0xd47, "cortex-a710"},
    {0x41, 0xd48, "cortex-x2"},
    {0x41, 0xd49, "neoverse-n2"},
    {0x41, 0xd4d, "cortex-a715"},
    {0x41, 0xd4e, "cortex-x3"},
    {0x41, 0xd4f, "neoverse-v2"},
    {0x41, 0xd80, "cortex-a520"},
    {0x41, 0xd81, "cortex-a720"},
    {0x41, 0xd82, "cortex-x4"},
    {0x41, 0xd84, "neoverse-v3"},
    {0x41, 0xd85, "cortex-x925"},
    {0x41, 0xd87, "cortex-a725"},
    // Broadcom / Cavium (0x42/0x43)
    {0x42, 0x516, "thunderx2t99"},
    {0x42, 0x0af, "thunderx2t99"},
    {0x42, 0x0a1, "thunderxt88"},
    {0x43, 0x516, "thunderx2t99"},
    {0x43, 0x0af, "thunderx2t99"},
    {0x43, 0x0a1, "thunderxt88"},
    // Fujitsu (0x46)
    {0x46, 0x001, "a64fx"},
    {0x46, 0x003, "fujitsu-monaka"},
    // HiSilicon (0x48)
    {0x48, 0xd01, "tsv110"},
    // NVIDIA (0x4e)
    {0x4e, 0x004, "carmel"},
    // Qualcomm (0x51)
    {0x51, 0x001, "oryon-1"},
    {0x51, 0x800, "cortex-a73"},  // Kryo 2xx Gold
    {0x51, 0x801, "cortex-a73"},  // Kryo 2xx Silver
    {0x51, 0x802, "cortex-a75"},  // Kryo 3xx Gold
    {0x51, 0x803, "cortex-a75"},  // Kryo 3xx Silver
    {0x51, 0x804, "cortex-a76"},  // Kryo 4xx Gold
    {0x51, 0x805, "cortex-a76"},  // Kryo 4xx/5xx Silver
    {0x51, 0xc00, "falkor"},
    {0x51, 0xc01, "saphira"},
    // Apple (0x61, on Linux/Asahi)
    {0x61, 0x020, "apple-m1"},
    {0x61, 0x021, "apple-m1"},
    {0x61, 0x022, "apple-m1"},
    {0x61, 0x023, "apple-m1"},
    {0x61, 0x024, "apple-m1"},
    {0x61, 0x025, "apple-m1"},
    {0x61, 0x028, "apple-m1"},
    {0x61, 0x029, "apple-m1"},
    {0x61, 0x030, "apple-m2"},
    {0x61, 0x031, "apple-m2"},
    {0x61, 0x032, "apple-m2"},
    {0x61, 0x033, "apple-m2"},
    {0x61, 0x034, "apple-m2"},
    {0x61, 0x035, "apple-m2"},
    {0x61, 0x038, "apple-m2"},
    {0x61, 0x039, "apple-m2"},
    {0x61, 0x048, "apple-m3"},
    {0x61, 0x049, "apple-m3"},
    // Microsoft (0x6d)
    {0x6d, 0xd49, "neoverse-n2"},  // Azure Cobalt 100
    // Ampere (0xc0)
    {0xc0, 0xac3, "ampere1"},
    {0xc0, 0xac4, "ampere1a"},
    {0xc0, 0xac5, "ampere1b"},
    {0, 0, nullptr}
};

#if defined(__FreeBSD__)
static inline unsigned long _getauxval(unsigned long type) {
    unsigned long val;
    if (elf_aux_info((int)type, &val, sizeof(val)) != 0) {
        return 0;
    }
    return val;
}

static inline unsigned int _bitslice(unsigned int reg, unsigned int msb, unsigned int lsb) {
    unsigned long bits = msb - lsb + 1ULL;
    unsigned long mask = (1ULL << bits) - 1ULL;
    return (reg >> lsb) & mask;
}

namespace tp {
// NOTE: FreeBSD reports the intersection of capabilities across CPUs, so
// checking capabilities separately by CPU is not necessary
const std::string &get_host_cpu_name() {
    static std::string cpu_name;
    if (!cpu_name.empty()) {
        return cpu_name;
    }
    unsigned long midr_el1;
    __asm__("mrs %0, MIDR_EL1" : "=r"(midr_el1));
    unsigned implementer = _bitslice(midr_el1, 31, 24);
    unsigned part = _bitslice(midr_el1, 15, 4);
    for (const ArmCPUInfo *entry = arm_cpus; entry->name; entry++) {
        if (entry->implementer == implementer && entry->part == part) {
            cpu_name = entry->name;
            break;
        }
    }
    if (cpu_name.empty()) {
        cpu_name = "generic";
    }
    return cpu_name;
}
} // namespace tp

#else  // Linux, probably

static inline unsigned long _getauxval(unsigned long type) {
    return getauxval(type);
}

static const std::string &load_cpuinfo() {
    static std::string content;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        std::ifstream f("/proc/cpuinfo");
        if (f) {
            content.assign(std::istreambuf_iterator<char>(f),
                           std::istreambuf_iterator<char>());
        }
    }
    return content;
}

static std::string_view cpuinfo_field(std::string_view buf, std::string_view field) {
    size_t pos = 0;
    while (pos < buf.size()) {
        auto found = buf.find(field, pos);
        if (found == std::string_view::npos) break;

        if (found > 0 && buf[found - 1] != '\n') {
            pos = found + 1;
            continue;
        }

        auto after = found + field.size();
        while (after < buf.size() && (buf[after] == ' ' || buf[after] == '\t'))
            after++;
        if (after >= buf.size() || buf[after] != ':') {
            pos = found + 1;
            continue;
        }
        after++;
        while (after < buf.size() && (buf[after] == ' ' || buf[after] == '\t'))
            after++;

        auto eol = buf.find('\n', after);
        if (eol == std::string_view::npos) eol = buf.size();
        return buf.substr(after, eol - after);
    }
    return {};
}

// Collect all distinct values of a cpuinfo field (one per core)
static std::vector<std::string_view> cpuinfo_field_all(std::string_view buf, std::string_view field) {
    std::vector<std::string_view> result;
    size_t pos = 0;
    while (pos < buf.size()) {
        auto found = buf.find(field, pos);
        if (found == std::string_view::npos) break;

        if (found > 0 && buf[found - 1] != '\n') {
            pos = found + 1;
            continue;
        }

        auto after = found + field.size();
        while (after < buf.size() && (buf[after] == ' ' || buf[after] == '\t'))
            after++;
        if (after >= buf.size() || buf[after] != ':') {
            pos = found + 1;
            continue;
        }
        after++;
        while (after < buf.size() && (buf[after] == ' ' || buf[after] == '\t'))
            after++;

        auto eol = buf.find('\n', after);
        if (eol == std::string_view::npos) eol = buf.size();
        auto val = buf.substr(after, eol - after);

        // Add if not already present
        bool dup = false;
        for (auto &v : result) if (v == val) { dup = true; break; }
        if (!dup) result.push_back(val);

        pos = eol + 1;
    }
    return result;
}

// Known big.LITTLE pairs: {big_part, little_part, result_name}
struct BigLittlePair {
    unsigned big_part;
    unsigned little_part;
    const char *name;
};

// Known big.LITTLE / DynamIQ pairings.
// First entry (from LLVM Host.cpp), rest from ARM product documentation.
// When both cores are present, report the big core.
static const BigLittlePair big_little_pairs[] = {
    // LLVM Host.cpp
    {0xd85, 0xd87, "cortex-x925"},  // X925 + A725
    // ARM DynamIQ pairings
    {0xd82, 0xd80, "cortex-x4"},    // X4 + A520
    {0xd81, 0xd80, "cortex-a720"},  // A720 + A520
    {0xd4e, 0xd46, "cortex-x3"},    // X3 + A510
    {0xd4d, 0xd46, "cortex-a715"},  // A715 + A510
    {0xd48, 0xd46, "cortex-x2"},    // X2 + A510
    {0xd47, 0xd46, "cortex-a710"},  // A710 + A510
    {0xd44, 0xd41, "cortex-x1"},    // X1 + A78
    {0xd41, 0xd05, "cortex-a78"},   // A78 + A55
    {0xd0b, 0xd05, "cortex-a76"},   // A76 + A55
    {0xd0a, 0xd05, "cortex-a75"},   // A75 + A55
    {0xd08, 0xd03, "cortex-a72"},   // A72 + A53
    {0xd07, 0xd03, "cortex-a57"},   // A57 + A53
    {0, 0, nullptr}
};

namespace tp {

const std::string &get_host_cpu_name() {
    static std::string cpu_name;
    if (!cpu_name.empty()) return cpu_name;

    const auto &info = load_cpuinfo();

    // Collect all distinct (implementer, part) pairs from all cores.
    // On big.LITTLE systems, different cores report different parts.
    auto impl_all = cpuinfo_field_all(info, "CPU implementer");
    auto part_all = cpuinfo_field_all(info, "CPU part");

    struct CoreInfo { unsigned impl; unsigned part; };
    std::vector<CoreInfo> cores;
    // Pair up: typically each core block has one implementer + one part,
    // but we collect all distinct parts we see.
    unsigned default_impl = 0x41; // ARM Ltd.
    if (!impl_all.empty())
        default_impl = static_cast<unsigned>(std::strtoul(
            std::string(impl_all[0]).c_str(), nullptr, 0));

    for (auto &p : part_all) {
        unsigned part = static_cast<unsigned>(std::strtoul(
            std::string(p).c_str(), nullptr, 0));
        cores.push_back({default_impl, part});
    }

    const char *name = "generic";

    // Check for known big.LITTLE pairs first
    if (cores.size() >= 2) {
        for (const auto &bl : big_little_pairs) {
            if (!bl.name) break;
            bool has_big = false, has_little = false;
            for (const auto &c : cores) {
                if (c.part == bl.big_part) has_big = true;
                if (c.part == bl.little_part) has_little = true;
            }
            if (has_big && has_little) {
                name = bl.name;
                break;
            }
        }
    }

    // If no big.LITTLE match, look up all cores and pick the one with the
    // most features (i.e. the "big" core on an unknown big.LITTLE system).
    if (std::strcmp(name, "generic") == 0 && !cores.empty()) {
        unsigned best_popcount = 0;
        for (const auto &c : cores) {
            for (const ArmCPUInfo *entry = arm_cpus; entry->name; entry++) {
                if (entry->implementer == c.impl && entry->part == c.part) {
                    const CPUEntry *cpu = find_cpu(entry->name);
                    if (!cpu) continue;
                    unsigned pc = feature_popcount(&cpu->features);
                    if (pc > best_popcount) {
                        best_popcount = pc;
                        name = entry->name;
                    }
                    break;
                }
            }
        }
    }

    if (!find_cpu(name))
        name = "generic";

    cpu_name = name;
    return cpu_name;
}
} // namespace tp
#endif // Linux

namespace tp {

// Map hwcap bits → LLVM feature names. If the kernel doesn't report a
// hwcap bit at runtime, the corresponding LLVM feature is cleared from
// the CPU-table baseline. Hoisted to file scope so the test-introspection
// accessor below can derive its name list from the same table.
struct HWCapMap { unsigned long bit; unsigned char which; const char *llvm_name; };
static const HWCapMap hwcap_map[] = {
    // AT_HWCAP
    {1UL <<  0, 0, "fp-armv8"},      // HWCAP_FP
    {1UL <<  1, 0, "neon"},          // HWCAP_ASIMD
    {1UL <<  3, 0, "aes"},           // HWCAP_AES
    {1UL <<  6, 0, "sha2"},          // HWCAP_SHA2
    {1UL <<  7, 0, "crc"},           // HWCAP_CRC32
    {1UL <<  8, 0, "lse"},           // HWCAP_ATOMICS
    {1UL <<  9, 0, "fullfp16"},      // HWCAP_FPHP
    {1UL << 12, 0, "rdm"},           // HWCAP_ASIMDRDM
    {1UL << 13, 0, "jsconv"},        // HWCAP_JSCVT
    {1UL << 14, 0, "complxnum"},     // HWCAP_FCMA
    {1UL << 15, 0, "rcpc"},          // HWCAP_LRCPC
    {1UL << 16, 0, "ccpp"},          // HWCAP_DCPOP (FEAT_DPB)
    {1UL << 17, 0, "sha3"},          // HWCAP_SHA3
    {1UL << 19, 0, "sm4"},           // HWCAP_SM4
    {1UL << 20, 0, "dotprod"},       // HWCAP_ASIMDDP
    {1UL << 22, 0, "sve"},           // HWCAP_SVE
    {1UL << 23, 0, "fp16fml"},       // HWCAP_ASIMDFHM
    {1UL << 24, 0, "dit"},           // HWCAP_DIT
    {1UL << 25, 0, "lse2"},          // HWCAP_USCAT
    {1UL << 26, 0, "rcpc-immo"},     // HWCAP_ILRCPC
    {1UL << 27, 0, "flagm"},         // HWCAP_FLAGM
    // {1UL << 28, 0, "ssbs"},       // HWCAP_SSBS — not codegen-relevant
    {1UL << 29, 0, "sb"},            // HWCAP_SB
    {1UL << 30, 0, "pauth"},         // HWCAP_PACA
    // AT_HWCAP2
    {1UL <<  0, 1, "ccdp"},          // HWCAP2_DCPODP (FEAT_DPB2)
    {1UL <<  1, 1, "sve2"},          // HWCAP2_SVE2
    {1UL <<  2, 1, "sve-aes"},       // HWCAP2_SVEAES
    {1UL <<  4, 1, "sve-bitperm"},   // HWCAP2_SVEBITPERM
    {1UL <<  5, 1, "sve2-sha3"},     // HWCAP2_SVESHA3
    {1UL <<  6, 1, "sve2-sm4"},      // HWCAP2_SVESM4
    {1UL <<  7, 1, "altnzcv"},       // HWCAP2_FLAGM2
    {1UL <<  8, 1, "fptoint"},       // HWCAP2_FRINT
    {1UL << 13, 1, "i8mm"},          // HWCAP2_I8MM
    {1UL << 14, 1, "bf16"},          // HWCAP2_BF16
    {1UL << 16, 1, "rand"},          // HWCAP2_RNG
    {1UL << 18, 1, "mte"},           // HWCAP2_MTE
    {1UL << 19, 1, "ecv"},           // HWCAP2_ECV
    {1UL << 23, 1, "sme"},           // HWCAP2_SME
    {1UL << 24, 1, "sme-i16i64"},    // HWCAP2_SME_I16I64
    {1UL << 25, 1, "sme-f64f64"},    // HWCAP2_SME_F64F64
    {1UL << 31, 1, "wfxt"},          // HWCAP2_WFXT
    {1UL << 34, 1, "cssc"},          // HWCAP2_CSSC
    {1UL << 37, 1, "sme2"},          // HWCAP2_SME2
    {1UL << 49, 1, "lut"},           // HWCAP2_LUT
    {1UL << 50, 1, "faminmax"},      // HWCAP2_FAMINMAX
    {1UL << 51, 1, "fp8"},           // HWCAP2_F8CVT
    {1UL << 52, 1, "fp8fma"},        // HWCAP2_F8FMA
    {1UL << 53, 1, "fp8dot4"},       // HWCAP2_F8DP4
    {1UL << 54, 1, "fp8dot2"},       // HWCAP2_F8DP2
    // AT_HWCAP3
    {1UL <<  3, 2, "ls64"},          // HWCAP3_LS64 (FEAT_LS64)
    {0, 0, nullptr}
};

FeatureBits get_host_features() {
    FeatureBits features{};

    // Start from the CPU table — it has the complete LLVM feature set.
    const auto &cpu = get_host_cpu_name();
    const CPUEntry *entry = find_cpu(cpu.c_str());
    if (entry)
        features = entry->features;

    // The kernel may disable features (e.g. nosve boot param, MTE not
    // enabled). Use hwcap to detect what the kernel actually exposes,
    // and clear any table features the kernel doesn't report.
    unsigned long hwcaps[3] = {
        _getauxval(AT_HWCAP),
        _getauxval(AT_HWCAP2),
#ifdef AT_HWCAP3
        _getauxval(AT_HWCAP3),
#endif
    };

    FeatureBits to_enable{};
    FeatureBits to_disable{};
    for (const auto *m = hwcap_map; m->llvm_name; m++) {
        const FeatureEntry *fe = find_feature(m->llvm_name);
        assert(fe && "hwcap_map names a feature missing from the table");
        if ((hwcaps[m->which] & m->bit) != 0)
            feature_set(&to_enable, fe->bit);
        else
            feature_set(&to_disable, fe->bit);
    }

    apply_feature_delta(&features, to_enable, to_disable);
    return features;
}

const char *const *get_host_feature_detection(HostFeatureDetectionKind kind) {
    static const char *empty[] = { nullptr };
    switch (kind) {
    case HOST_FEATURE_BASELINE:
        return empty;
    case HOST_FEATURE_DETECTABLE: {
        constexpr size_t N = sizeof(hwcap_map) / sizeof(hwcap_map[0]);
        static const auto names = []() {
            std::array<const char *, N + 1> a{};
            size_t n = 0;
            for (const auto *m = hwcap_map; m->llvm_name; m++)
                a[n++] = m->llvm_name;
            a[n] = nullptr;
            return a;
        }();
        return names.data();
    }
    case HOST_FEATURE_UNDETECTABLE: {
        static const char *names[] = {
            "clrbhb", "fpac",
            "lor", "ras", "predres", "specres2", "specrestrict",
            nullptr
        };
        return names;
    }
    }
    return empty;
}

} // namespace tp

#endif // platform
