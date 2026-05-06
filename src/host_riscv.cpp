// Host CPU detection for RISC-V (Linux).
// Standalone - no LLVM dependency.
// Uses riscv_hwprobe syscall and /proc/cpuinfo.

#include "target_tables_riscv64.h"
#include "target_parsing.h"

#include <array>
#include <cassert>
#include <cstring>
#include <cstdlib>

#ifdef __linux__
#include <unistd.h>
#include <sys/syscall.h>

struct riscv_hwprobe {
    long long key;
    unsigned long long value;
};

#ifndef __NR_riscv_hwprobe
#define __NR_riscv_hwprobe 258
#endif

static int do_hwprobe(struct riscv_hwprobe *pairs, size_t count) {
    return syscall(__NR_riscv_hwprobe, pairs, count,
                   /*cpu_count=*/0, /*cpus=*/NULL, /*flags=*/0);
}

#define RISCV_HWPROBE_KEY_MVENDORID     0
#define RISCV_HWPROBE_KEY_MARCHID       1
#define RISCV_HWPROBE_KEY_MIMPID        2
#define RISCV_HWPROBE_KEY_BASE_BEHAVIOR 3
#define RISCV_HWPROBE_KEY_IMA_EXT_0     4
#define RISCV_HWPROBE_KEY_MISALIGNED_SCALAR_PERF 9

#define RISCV_HWPROBE_BASE_BEHAVIOR_IMA (1ULL << 0)
#define RISCV_HWPROBE_MISALIGNED_SCALAR_FAST 3

struct HwprobeBitMap { unsigned bit; const char *llvm_name; };
static const HwprobeBitMap hwprobe_ext_map[] = {
    { 0, "f"},            // RISCV_HWPROBE_IMA_FD
    { 0, "d"},            // RISCV_HWPROBE_IMA_FD
    { 1, "c"},            // RISCV_HWPROBE_IMA_C
    { 2, "v"},            // RISCV_HWPROBE_IMA_V
    { 3, "zba"},          // RISCV_HWPROBE_EXT_ZBA
    { 4, "zbb"},          // RISCV_HWPROBE_EXT_ZBB
    { 5, "zbs"},          // RISCV_HWPROBE_EXT_ZBS
    { 6, "zicboz"},       // RISCV_HWPROBE_EXT_ZICBOZ
    { 7, "zbc"},          // RISCV_HWPROBE_EXT_ZBC
    { 8, "zbkb"},         // RISCV_HWPROBE_EXT_ZBKB
    { 9, "zbkc"},         // RISCV_HWPROBE_EXT_ZBKC
    {10, "zbkx"},         // RISCV_HWPROBE_EXT_ZBKX
    {11, "zknd"},         // RISCV_HWPROBE_EXT_ZKND
    {12, "zkne"},         // RISCV_HWPROBE_EXT_ZKNE
    {13, "zknh"},         // RISCV_HWPROBE_EXT_ZKNH
    {14, "zksed"},        // RISCV_HWPROBE_EXT_ZKSED
    {15, "zksh"},         // RISCV_HWPROBE_EXT_ZKSH
    {16, "zkt"},          // RISCV_HWPROBE_EXT_ZKT
    {17, "zvbb"},         // RISCV_HWPROBE_EXT_ZVBB
    {18, "zvbc"},         // RISCV_HWPROBE_EXT_ZVBC
    {19, "zvkb"},         // RISCV_HWPROBE_EXT_ZVKB
    {20, "zvkg"},         // RISCV_HWPROBE_EXT_ZVKG
    {21, "zvkned"},       // RISCV_HWPROBE_EXT_ZVKNED
    {22, "zvknha"},       // RISCV_HWPROBE_EXT_ZVKNHA
    {23, "zvknhb"},       // RISCV_HWPROBE_EXT_ZVKNHB
    {24, "zvksed"},       // RISCV_HWPROBE_EXT_ZVKSED
    {25, "zvksh"},        // RISCV_HWPROBE_EXT_ZVKSH
    {26, "zvkt"},         // RISCV_HWPROBE_EXT_ZVKT
    {27, "zfh"},          // RISCV_HWPROBE_EXT_ZFH
    {28, "zfhmin"},       // RISCV_HWPROBE_EXT_ZFHMIN
    {29, "zihintntl"},    // RISCV_HWPROBE_EXT_ZIHINTNTL
    {30, "zvfh"},         // RISCV_HWPROBE_EXT_ZVFH
    {31, "zvfhmin"},      // RISCV_HWPROBE_EXT_ZVFHMIN
    {32, "zfa"},          // RISCV_HWPROBE_EXT_ZFA
    {33, "ztso"},         // RISCV_HWPROBE_EXT_ZTSO
    {34, "zacas"},        // RISCV_HWPROBE_EXT_ZACAS
    {35, "zicond"},       // RISCV_HWPROBE_EXT_ZICOND
    {36, "zihintpause"},  // RISCV_HWPROBE_EXT_ZIHINTPAUSE
    {37, "zve32x"},       // RISCV_HWPROBE_EXT_ZVE32X
    {38, "zve32f"},       // RISCV_HWPROBE_EXT_ZVE32F
    {39, "zve64x"},       // RISCV_HWPROBE_EXT_ZVE64X
    {40, "zve64f"},       // RISCV_HWPROBE_EXT_ZVE64F
    {41, "zve64d"},       // RISCV_HWPROBE_EXT_ZVE64D
    {42, "zimop"},        // RISCV_HWPROBE_EXT_ZIMOP
    {43, "zca"},          // RISCV_HWPROBE_EXT_ZCA
    {44, "zcb"},          // RISCV_HWPROBE_EXT_ZCB
    {45, "zcd"},          // RISCV_HWPROBE_EXT_ZCD
    {46, "zcf"},          // RISCV_HWPROBE_EXT_ZCF
    {47, "zcmop"},        // RISCV_HWPROBE_EXT_ZCMOP
    {48, "zawrs"},        // RISCV_HWPROBE_EXT_ZAWRS
    {0, nullptr}          // sentinel
};

#endif // __linux__

// ============================================================================
// CPU name detection
// ============================================================================

#ifdef __linux__

static const char *detect_riscv_cpu_from_hwprobe(void) {
    struct riscv_hwprobe query[] = {
        {RISCV_HWPROBE_KEY_MVENDORID, 0},
        {RISCV_HWPROBE_KEY_MARCHID, 0},
        {RISCV_HWPROBE_KEY_MIMPID, 0}
    };
    if (do_hwprobe(query, 3) != 0)
        return nullptr;

    unsigned long long vendor = query[0].value;
    unsigned long long arch = query[1].value;

    if (vendor == 0x489) {
        switch (arch) {
        case 0x8000000000000007ULL: return "sifive-u74";
        case 0x8000000000000007ULL + 1: return "sifive-s76";
        }
        if ((arch >> 56) == 0x80) {
            switch (arch & 0xFF) {
            case 0x50: return "sifive-p450";
            case 0x70: return "sifive-p670";
            }
        }
    }

    if (vendor == 0x710)
        return "spacemit-x60";

    return nullptr;
}

#endif

namespace tp {

const std::string &get_host_cpu_name() {
    static std::string cpu_name;
    if (!cpu_name.empty()) return cpu_name;

    const char *name = nullptr;

#ifdef __linux__
    name = detect_riscv_cpu_from_hwprobe();
#endif

    if (!name || !find_cpu(name))
        name = "generic-rv64";

    cpu_name = name;
    return cpu_name;
}

FeatureBits get_host_features() {
    // RISC-V has no CPU-table baseline at the LLVM-feature level — even
    // "i" comes from hwprobe — so features starts empty and any extension
    // not enumerated by hwprobe ends up in to_disable.
    FeatureBits features{};
    FeatureBits to_enable{};
    FeatureBits to_disable{};

#ifdef __linux__
    struct riscv_hwprobe query[] = {
        {RISCV_HWPROBE_KEY_BASE_BEHAVIOR, 0},
        {RISCV_HWPROBE_KEY_IMA_EXT_0, 0},
        {RISCV_HWPROBE_KEY_MISALIGNED_SCALAR_PERF, 0}
    };

    if (do_hwprobe(query, 3) != 0)
        return features;

    unsigned long long base = query[0].value;
    unsigned long long ext = query[1].value;

    bool has_ima = (base & RISCV_HWPROBE_BASE_BEHAVIOR_IMA) != 0;
    FeatureBits *delta = has_ima ? &to_enable : &to_disable;
    feature_set(delta, find_feature("i")->bit);
    feature_set(delta, find_feature("m")->bit);
    feature_set(delta, find_feature("a")->bit);

    for (const auto *m = hwprobe_ext_map; m->llvm_name; m++) {
        const FeatureEntry *fe = find_feature(m->llvm_name);
        assert(fe && "could not find feature in hwprobe_ext_map");
        if ((ext & (1ULL << m->bit)) != 0)
            feature_set(&to_enable, fe->bit);
        else
            feature_set(&to_disable, fe->bit);
    }

    if (query[2].key != -1) {
        if (query[2].value == RISCV_HWPROBE_MISALIGNED_SCALAR_FAST)
            feature_set(&to_enable, find_feature("unaligned-scalar-mem")->bit);
        else
            feature_set(&to_disable, find_feature("unaligned-scalar-mem")->bit);
    }
#endif

    apply_feature_delta(&features, to_enable, to_disable);
    return features;
}

const char *const *get_host_feature_detection(HostFeatureDetectionKind kind) {
    static const char *empty[] = { nullptr };
    switch (kind) {
    case HOST_FEATURE_BASELINE:
        return empty;
    case HOST_FEATURE_DETECTABLE: {
#ifdef __linux__
        // +4 slots for i, m, a, unaligned-scalar-mem.
        constexpr size_t N = sizeof(hwprobe_ext_map) / sizeof(hwprobe_ext_map[0]) + 4;
        static const auto names = []() {
            std::array<const char *, N> a{};
            size_t n = 0;
            a[n++] = "i";
            a[n++] = "m";
            a[n++] = "a";
            for (const auto *m = hwprobe_ext_map; m->llvm_name; m++)
                a[n++] = m->llvm_name;
            a[n++] = "unaligned-scalar-mem";
            a[n] = nullptr;
            return a;
        }();
        return names.data();
#else
        return empty;
#endif
    }
    case HOST_FEATURE_UNDETECTABLE:
        return empty;
    }
    return empty;
}

} // namespace tp
