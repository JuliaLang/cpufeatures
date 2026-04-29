// Host CPU detection for AArch64.
// Standalone - no LLVM dependency.
// Supports Linux (/proc/cpuinfo), macOS (sysctlbyname), Windows (stubs).

#include "target_tables_aarch64.h"
#include "target_parsing.h"

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
    size_t caps_len = 0;
    if (sysctlbyname("hw.optional.arm.caps", nullptr, &caps_len, nullptr, 0) == 0
            && caps_len > 0) {
        std::vector<uint8_t> caps(caps_len, 0);
        if (sysctlbyname("hw.optional.arm.caps", caps.data(), &caps_len, nullptr, 0) == 0) {
            for (const auto *m = cap_bit_map; m->llvm_name; m++) {
                const FeatureEntry *fe = find_feature(m->llvm_name);
                if (!fe) continue;
                if (cap_test(caps.data(), caps_len, m->cap_bit))
                    feature_set(&features, fe->bit);
                else
                    feature_clear(&features, fe->bit);
            }
        }
    }

    _expand_entailed_enable_bits(&features);
    return features;
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

FeatureBits get_host_features() {
    FeatureBits features{};

    const FeatureEntry *fe;

    if ((fe = find_feature("neon"))) feature_set(&features, fe->bit);
    if ((fe = find_feature("fp-armv8"))) feature_set(&features, fe->bit);

    #ifndef PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE
    #define PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE 31
    #endif
    if (IsProcessorFeaturePresent(PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE)) {
        if ((fe = find_feature("crc"))) feature_set(&features, fe->bit);
    }

    #ifndef PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE
    #define PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE 30
    #endif
    if (IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE)) {
        if ((fe = find_feature("aes"))) feature_set(&features, fe->bit);
        if ((fe = find_feature("sha2"))) feature_set(&features, fe->bit);
    }

    #ifndef PF_ARM_V81_ATOMIC_INSTRUCTIONS_AVAILABLE
    #define PF_ARM_V81_ATOMIC_INSTRUCTIONS_AVAILABLE 34
    #endif
    if (IsProcessorFeaturePresent(PF_ARM_V81_ATOMIC_INSTRUCTIONS_AVAILABLE)) {
        if ((fe = find_feature("lse"))) feature_set(&features, fe->bit);
    }

    #ifndef PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE
    #define PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE 43
    #endif
    if (IsProcessorFeaturePresent(PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE)) {
        if ((fe = find_feature("dotprod"))) feature_set(&features, fe->bit);
    }

    _expand_entailed_enable_bits(&features);
    return features;
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

FeatureBits get_host_features() {
    FeatureBits features{};

    // Start from the CPU table — it has the complete LLVM feature set.
    const auto &cpu = get_host_cpu_name();
    const CPUEntry *entry = find_cpu(cpu.c_str());
    if (entry)
        features = entry->features;

#if !defined(__APPLE__) && !defined(_WIN32)
    // The kernel may disable features (e.g. nosve boot param, MTE not
    // enabled). Use hwcap to detect what the kernel actually exposes,
    // and clear any table features the kernel doesn't report.
    unsigned long hwcap = _getauxval(AT_HWCAP);
    unsigned long hwcap2 = _getauxval(AT_HWCAP2);

    // Map hwcap bits → LLVM feature names.
    // If the kernel doesn't report a hwcap bit, clear the corresponding
    // LLVM feature from the CPU table.
    struct HWCapMap { unsigned long bit; bool is_hwcap2; const char *llvm_name; };
    static const HWCapMap hwcap_map[] = {
        // HWCAP
        {1UL <<  0, false, "fp-armv8"},      // HWCAP_FP
        {1UL <<  1, false, "neon"},          // HWCAP_ASIMD
        {1UL <<  3, false, "aes"},           // HWCAP_AES
        {1UL <<  6, false, "sha2"},          // HWCAP_SHA2
        {1UL <<  7, false, "crc"},           // HWCAP_CRC32
        {1UL <<  8, false, "lse"},           // HWCAP_ATOMICS
        {1UL <<  9, false, "fullfp16"},      // HWCAP_FPHP
        {1UL << 12, false, "rdm"},           // HWCAP_ASIMDRDM
        {1UL << 13, false, "jsconv"},        // HWCAP_JSCVT
        {1UL << 15, false, "rcpc"},          // HWCAP_LRCPC
        {1UL << 17, false, "sha3"},          // HWCAP_SHA3
        {1UL << 19, false, "sm4"},           // HWCAP_SM4
        {1UL << 20, false, "dotprod"},       // HWCAP_ASIMDDP
        {1UL << 22, false, "sve"},           // HWCAP_SVE
        {1UL << 23, false, "fp16fml"},       // HWCAP_ASIMDFHM
        {1UL << 24, false, "dit"},           // HWCAP_DIT
        {1UL << 25, false, "lse2"},          // HWCAP_USCAT
        {1UL << 26, false, "rcpc-immo"},     // HWCAP_ILRCPC
        {1UL << 27, false, "flagm"},         // HWCAP_FLAGM
        // {1UL << 28, false, "ssbs"},       // HWCAP_SSBS — not codegen-relevant
        {1UL << 29, false, "sb"},            // HWCAP_SB
        {1UL << 30, false, "pauth"},         // HWCAP_PACA
        // HWCAP2
        {1UL <<  1, true,  "sve2"},          // HWCAP2_SVE2
        {1UL <<  2, true,  "sve-aes"},       // HWCAP2_SVEAES
        {1UL <<  4, true,  "sve-bitperm"},   // HWCAP2_SVEBITPERM
        {1UL <<  5, true,  "sve2-sha3"},     // HWCAP2_SVESHA3
        {1UL <<  6, true,  "sve2-sm4"},      // HWCAP2_SVESM4
        {1UL <<  8, true,  "fptoint"},       // HWCAP2_FRINT
        {1UL << 13, true,  "i8mm"},          // HWCAP2_I8MM
        {1UL << 14, true,  "bf16"},          // HWCAP2_BF16
        {1UL << 16, true,  "rand"},          // HWCAP2_RNG
        {1UL << 18, true,  "mte"},           // HWCAP2_MTE
        {1UL << 23, true,  "sme"},           // HWCAP2_SME
        {1UL << 24, true,  "sme-i16i64"},    // HWCAP2_SME_I16I64
        {1UL << 25, true,  "sme-f64f64"},    // HWCAP2_SME_F64F64
        {1UL << 34, true,  "cssc"},          // HWCAP2_CSSC
        {1UL << 37, true,  "sme2"},          // HWCAP2_SME2
        {0, false, nullptr}
    };

    for (const auto *m = hwcap_map; m->llvm_name; m++) {
        unsigned long cap = m->is_hwcap2 ? hwcap2 : hwcap;
        if (!(cap & m->bit)) {
            const FeatureEntry *fe = find_feature(m->llvm_name);
            if (fe) feature_clear(&features, fe->bit);
        }
    }
#endif

    _expand_entailed_enable_bits(&features);
    return features;
}

} // namespace tp

#endif // platform
