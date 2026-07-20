// host_loongarch64.cpp
// Host CPU detection for LoongArch64 (Linux), no LLVM runtime dependency
// Two data source design consistent with AArch64/RISCV:
// 1. getauxval(AT_HWCAP): Query hardware extension feature bits (LSX/LASX/LBT/LVZ/LAM...)
// 2. /proc/cpuinfo: Only read model name to match micro-arch (la464/la664)
// Detected features supply target_parsing layer to select compiler optimizations

#include "target_tables_loongarch64.h"
#include "target_parsing.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>

// ---------- Linux exclusive header & HWCAP compatibility macro ----------
#ifdef __linux__
#include <sys/auxv.h>      // getauxval(), AT_HWCAP auxv vector
#include <asm/hwcap.h>     // Upstream kernel LoongArch HWCAP bit definitions
#include <strings.h>       // strncasecmp / strcasestr for cpuinfo parsing

// Compatibility fallback for old kernel headers & cross-compile from x86
// Bit number strictly align Linux v6.0 asm/hwcap.h official definition
#ifndef HWCAP_LOONGARCH_CPUCFG
#define HWCAP_LOONGARCH_CPUCFG   (1UL << 0)
#endif
#ifndef HWCAP_LOONGARCH_LAM
#define HWCAP_LOONGARCH_LAM      (1UL << 1)
#endif
#ifndef HWCAP_LOONGARCH_UAL
#define HWCAP_LOONGARCH_UAL      (1UL << 2)
#endif
#ifndef HWCAP_LOONGARCH_FPU
#define HWCAP_LOONGARCH_FPU      (1UL << 3)
#endif
#ifndef HWCAP_LOONGARCH_LSX
#define HWCAP_LOONGARCH_LSX      (1UL << 4)
#endif
#ifndef HWCAP_LOONGARCH_LASX
#define HWCAP_LOONGARCH_LASX     (1UL << 5)
#endif
#ifndef HWCAP_LOONGARCH_CRC32
#define HWCAP_LOONGARCH_CRC32    (1UL << 6)
#endif
#ifndef HWCAP_LOONGARCH_COMPLEX
#define HWCAP_LOONGARCH_COMPLEX  (1UL << 7)
#endif
#ifndef HWCAP_LOONGARCH_CRYPTO
#define HWCAP_LOONGARCH_CRYPTO   (1UL << 8)
#endif
#ifndef HWCAP_LOONGARCH_LVZ
#define HWCAP_LOONGARCH_LVZ      (1UL << 9)
#endif
#ifndef HWCAP_LOONGARCH_LBT_X86
#define HWCAP_LOONGARCH_LBT_X86  (1UL << 10)
#endif
#ifndef HWCAP_LOONGARCH_LBT_ARM
#define HWCAP_LOONGARCH_LBT_ARM  (1UL << 11)
#endif
#ifndef HWCAP_LOONGARCH_LBT_MIPS
#define HWCAP_LOONGARCH_LBT_MIPS (1UL << 12)
#endif
#ifndef HWCAP_LOONGARCH_PTW
#define HWCAP_LOONGARCH_PTW      (1UL << 13)
#endif
#endif // End of __linux__

// ---------- String & /proc/cpuinfo helper utilities ----------
// Trim leading/trailing space and tab characters from input string
static std::string trim_str(const std::string &s) {
    size_t start = s.find_first_not_of(" \t");
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t");
    return s.substr(start, end - start + 1);
}

// Read full /proc/cpuinfo once and cache statically, avoid repeated file IO
static std::string read_cpuinfo_all() {
    std::ifstream f("/proc/cpuinfo");
    if (!f.is_open())
        return "";
    std::ostringstream buf;
    buf << f.rdbuf();
    return buf.str();
}

// ---------- Micro-arch mapping table & detection logic ----------
// Map cpuinfo model name substring to LLVM -mcpu target name
// Sort rule: Newer chip first, longer specific substring match takes priority
static const struct CpuArchMap {
    const char *model_substr;
    const char *arch;
} cpu_arch_map[] = {
    // LA664 series (3A6000/3B6000/3C6000/3D6000)
    {"3A6000", "la664"},
    {"3B6000", "la664"},
    {"3C6000", "la664"},
    {"3D6000", "la664"},
    // LA464 series (3A5000/3B5000/3C5000/3D5000)
    {"3A5000", "la464"},
    {"3B5000", "la464"},
    {"3C5000", "la464"},
    {"3D5000", "la464"},
    // Append new CPU models here in descending generation order
};

// Parse cached cpuinfo text to extract micro-arch identifier string
// Return matched arch static string; return nullptr to trigger generic fallback
static const char *detect_la_cpu_from_cpuinfo(const std::string &cpuinfo) {
    std::istringstream iss(cpuinfo);
    std::string line;
    while (std::getline(iss, line)) {
        line = trim_str(line);
        // Match model name field with case-insensitive compare
        if (strncasecmp(line.c_str(), "model name", 10) == 0) {
            size_t colon = line.find(':');
            if (colon == std::string::npos)
                continue;
            std::string cpu_model = trim_str(line.substr(colon + 1));
            // Iterate mapping table, return first matched arch name
            for (const auto &entry : cpu_arch_map) {
                if (strcasestr(cpu_model.c_str(), entry.model_substr) != nullptr) {
                    return entry.arch;
                }
            }
            // Stop scanning file once model name line processed without match
            break;
        }
    }
    return nullptr;
}

// ---------- Public detection interface inside tp namespace ----------
namespace tp {

// Get cached host micro-arch name, single static cache instance
// Fallback to "generic" if model detection failed or cpu not in table
const std::string &get_host_cpu_name() {
    static std::string cpu_name;
    if (!cpu_name.empty())
        return cpu_name;

    const char *name = nullptr;
#ifdef __linux__
    std::string cpuinfo = read_cpuinfo_all();
    name = detect_la_cpu_from_cpuinfo(cpuinfo);
#endif
    if (!name || !find_cpu(name))
        name = "generic";
    cpu_name = name;
    return cpu_name;
}

// Detect runtime hardware feature set via standard AT_HWCAP auxv vector
// Implementation aligned with aarch64/riscv host detection logic
// Step 1: Load mandatory LA64 baseline ISA features
// Step 2: Set extension bits parsed from kernel HWCAP mask
// Step 3: Apply micro-arch specific tuning hints
FeatureBits detect_host_features() {
    FeatureBits features{};
    apply_host_baseline(&features); // Mandatory base ISA features for all LA64 CPU

#ifdef __linux__
    uint64_t hwcap = static_cast<uint64_t>(getauxval(AT_HWCAP));
    // Fallback to pure baseline if kernel returns empty hwcap mask
    if (hwcap == 0) {
        apply_host_uarch(&features);
        return features;
    }

    FeatureBits to_enable{}, to_disable{};

    // Map kernel HWCAP mask bit to FEAT enum index defined in target_tables header
    struct HwcapMap {
        uint64_t mask;
        unsigned bit;
    };
    static const HwcapMap map[] = {
        { HWCAP_LOONGARCH_LSX,      FEAT_LSX },
        { HWCAP_LOONGARCH_LASX,     FEAT_LASX },
        { HWCAP_LOONGARCH_LVZ,      FEAT_LVZ },
        // LAM main bit enables both lam-bh and lamcas atomic instructions
        { HWCAP_LOONGARCH_LAM,      FEAT_LAM_BH },
        { HWCAP_LOONGARCH_LAM,      FEAT_LAMCAS },
        // Any LBT sub flag marks unified lbt feature as supported
        { HWCAP_LOONGARCH_LBT_X86,  FEAT_LBT },
        { HWCAP_LOONGARCH_LBT_ARM,  FEAT_LBT },
        { HWCAP_LOONGARCH_LBT_MIPS, FEAT_LBT },
        // Re-validate UAL hardware support via hwcap (already in baseline set)
        { HWCAP_LOONGARCH_UAL,      FEAT_UAL },
    };

    // Traverse mapping table and fill detectable hardware feature bits
    for (const auto &entry : map) {
        if (hwcap & entry.mask) {
            feature_set(&to_enable, entry.bit);
        }
    }

    apply_feature_delta(&features, to_enable, to_disable);
#endif // __linux__

    apply_host_uarch(&features); // Apply model-specific tuning configuration
    return features;
}

// Categorize all ISA features into three groups for upper layer query
// HOST_FEATURE_DETECTABLE: Features exposed via AT_HWCAP runtime mask
// HOST_FEATURE_BASELINE: Mandatory fixed LA64 base instruction set
// HOST_FEATURE_UNDETECTABLE: No independent HWCAP bit, cannot auto probe at runtime
const char *const *get_host_feature_detection(HostFeatureDetectionKind kind) {
    static const char *empty[] = { nullptr };

    switch (kind) {
        case HOST_FEATURE_DETECTABLE: {
            static const char *names[] = {
                "lsx",
                "lasx",
                "lamcas",
                "lam-bh",
                "lvz",
                "lbt",
                nullptr
            };
            return names;
        }
        case HOST_FEATURE_BASELINE: {
            static const char *baseline[] = {
                "f",
                "d",
                "64bit",
                "div32",
                "32s",
                "ual",
                "ld-seq-sa",
                nullptr
            };
            return baseline;
        }
        case HOST_FEATURE_UNDETECTABLE: {
            static const char *names[] = {
                "32bit",    // LA32 ISA irrelevant for la64 host environment
                "frecipe",  // No standalone HWCAP bit assigned for frecipe/frsqrte
                "scq",      // SC.Q detection not available on older kernels
                nullptr
            };
            return names;
        }
        default:
            return empty;
    }
}

} // namespace tp
