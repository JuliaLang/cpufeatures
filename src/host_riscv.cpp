// Host CPU detection for RISC-V (Linux).
// Standalone - no LLVM dependency.
// Uses riscv_hwprobe syscall and /proc/cpuinfo.

#include "target_tables_riscv64.h"
#include "target_parsing.h"

#include <cstring>
#include <cstdlib>

#ifdef __linux__
#include <fstream>
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

#define RISCV_HWPROBE_BASE_BEHAVIOR_IMA (1 << 0)

#define RISCV_HWPROBE_IMA_FD            (1ULL << 0)
#define RISCV_HWPROBE_IMA_C             (1ULL << 1)
#define RISCV_HWPROBE_IMA_V             (1ULL << 2)
#define RISCV_HWPROBE_EXT_ZBA           (1ULL << 3)
#define RISCV_HWPROBE_EXT_ZBB           (1ULL << 4)
#define RISCV_HWPROBE_EXT_ZBS           (1ULL << 5)
#define RISCV_HWPROBE_EXT_ZICBOZ        (1ULL << 6)
#define RISCV_HWPROBE_EXT_ZBC           (1ULL << 7)
#define RISCV_HWPROBE_EXT_ZBKB          (1ULL << 8)
#define RISCV_HWPROBE_EXT_ZBKC          (1ULL << 9)
#define RISCV_HWPROBE_EXT_ZBKX          (1ULL << 10)
#define RISCV_HWPROBE_EXT_ZKND          (1ULL << 11)
#define RISCV_HWPROBE_EXT_ZKNE          (1ULL << 12)
#define RISCV_HWPROBE_EXT_ZKNH          (1ULL << 13)
#define RISCV_HWPROBE_EXT_ZKSED         (1ULL << 14)
#define RISCV_HWPROBE_EXT_ZKSH          (1ULL << 15)
#define RISCV_HWPROBE_EXT_ZKT           (1ULL << 16)
#define RISCV_HWPROBE_EXT_ZVBB          (1ULL << 17)
#define RISCV_HWPROBE_EXT_ZVBC          (1ULL << 18)
#define RISCV_HWPROBE_EXT_ZVKB          (1ULL << 19)
#define RISCV_HWPROBE_EXT_ZVKG          (1ULL << 20)
#define RISCV_HWPROBE_EXT_ZVKNED        (1ULL << 21)
#define RISCV_HWPROBE_EXT_ZVKNHA        (1ULL << 22)
#define RISCV_HWPROBE_EXT_ZVKNHB        (1ULL << 23)
#define RISCV_HWPROBE_EXT_ZVKSED        (1ULL << 24)
#define RISCV_HWPROBE_EXT_ZVKSH         (1ULL << 25)
#define RISCV_HWPROBE_EXT_ZVKT          (1ULL << 26)
#define RISCV_HWPROBE_EXT_ZFH           (1ULL << 27)
#define RISCV_HWPROBE_EXT_ZFHMIN        (1ULL << 28)
#define RISCV_HWPROBE_EXT_ZIHINTNTL     (1ULL << 29)
#define RISCV_HWPROBE_EXT_ZVFH          (1ULL << 30)
#define RISCV_HWPROBE_EXT_ZVFHMIN       (1ULL << 31)
#define RISCV_HWPROBE_EXT_ZFA           (1ULL << 32)
#define RISCV_HWPROBE_EXT_ZTSO          (1ULL << 33)
#define RISCV_HWPROBE_EXT_ZACAS         (1ULL << 34)
#define RISCV_HWPROBE_EXT_ZICOND        (1ULL << 35)
#define RISCV_HWPROBE_EXT_ZIHINTPAUSE   (1ULL << 36)
#define RISCV_HWPROBE_EXT_ZVE32X        (1ULL << 37)
#define RISCV_HWPROBE_EXT_ZVE32F        (1ULL << 38)
#define RISCV_HWPROBE_EXT_ZVE64X        (1ULL << 39)
#define RISCV_HWPROBE_EXT_ZVE64F        (1ULL << 40)
#define RISCV_HWPROBE_EXT_ZVE64D        (1ULL << 41)
#define RISCV_HWPROBE_EXT_ZIMOP         (1ULL << 42)
#define RISCV_HWPROBE_EXT_ZCA           (1ULL << 43)
#define RISCV_HWPROBE_EXT_ZCB           (1ULL << 44)
#define RISCV_HWPROBE_EXT_ZCD           (1ULL << 45)
#define RISCV_HWPROBE_EXT_ZCF           (1ULL << 46)
#define RISCV_HWPROBE_EXT_ZCMOP         (1ULL << 47)
#define RISCV_HWPROBE_EXT_ZAWRS         (1ULL << 48)

#define RISCV_HWPROBE_MISALIGNED_SCALAR_FAST 3

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

static const char *detect_riscv_cpu_from_cpuinfo(void) {
    std::ifstream f("/proc/cpuinfo");
    if (!f) return nullptr;

    std::string line;
    while (std::getline(f, line)) {
        if (line.compare(0, 5, "uarch") != 0) continue;

        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        auto val = line.substr(colon + 1);
        auto start = val.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        val = val.substr(start);

        if (val.find("sifive,u74") != std::string::npos)
            return "sifive-u74";
        if (val.find("sifive,bullet") != std::string::npos)
            return "sifive-u74";
    }
    return nullptr;
}
#endif

static void set_feature(FeatureBits *features, const char *name) {
    const FeatureEntry *fe = find_feature(name);
    if (fe) feature_set(features, fe->bit);
}

namespace tp {

const std::string &get_host_cpu_name() {
    static std::string cpu_name;
    if (!cpu_name.empty()) return cpu_name;

    const char *name = nullptr;

#ifdef __linux__
    name = detect_riscv_cpu_from_hwprobe();
    if (!name)
        name = detect_riscv_cpu_from_cpuinfo();
#endif

    if (!name || !find_cpu(name))
        name = "generic-rv64";

    cpu_name = name;
    return cpu_name;
}

FeatureBits get_host_features() {
    FeatureBits features{};

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

    if (base & RISCV_HWPROBE_BASE_BEHAVIOR_IMA) {
        set_feature(&features, "i");
        set_feature(&features, "m");
        set_feature(&features, "a");
    }

    if (ext & RISCV_HWPROBE_IMA_FD) {
        set_feature(&features, "f");
        set_feature(&features, "d");
    }
    if (ext & RISCV_HWPROBE_IMA_C)    set_feature(&features, "c");
    if (ext & RISCV_HWPROBE_IMA_V)    set_feature(&features, "v");

    if (ext & RISCV_HWPROBE_EXT_ZBA)   set_feature(&features, "zba");
    if (ext & RISCV_HWPROBE_EXT_ZBB)   set_feature(&features, "zbb");
    if (ext & RISCV_HWPROBE_EXT_ZBS)   set_feature(&features, "zbs");
    if (ext & RISCV_HWPROBE_EXT_ZBC)   set_feature(&features, "zbc");
    if (ext & RISCV_HWPROBE_EXT_ZBKB)  set_feature(&features, "zbkb");
    if (ext & RISCV_HWPROBE_EXT_ZBKC)  set_feature(&features, "zbkc");
    if (ext & RISCV_HWPROBE_EXT_ZBKX)  set_feature(&features, "zbkx");

    if (ext & RISCV_HWPROBE_EXT_ZKND)  set_feature(&features, "zknd");
    if (ext & RISCV_HWPROBE_EXT_ZKNE)  set_feature(&features, "zkne");
    if (ext & RISCV_HWPROBE_EXT_ZKNH)  set_feature(&features, "zknh");
    if (ext & RISCV_HWPROBE_EXT_ZKSED) set_feature(&features, "zksed");
    if (ext & RISCV_HWPROBE_EXT_ZKSH)  set_feature(&features, "zksh");
    if (ext & RISCV_HWPROBE_EXT_ZKT)   set_feature(&features, "zkt");

    if (ext & RISCV_HWPROBE_EXT_ZVBB)   set_feature(&features, "zvbb");
    if (ext & RISCV_HWPROBE_EXT_ZVBC)   set_feature(&features, "zvbc");
    if (ext & RISCV_HWPROBE_EXT_ZVKB)   set_feature(&features, "zvkb");
    if (ext & RISCV_HWPROBE_EXT_ZVKG)   set_feature(&features, "zvkg");
    if (ext & RISCV_HWPROBE_EXT_ZVKNED) set_feature(&features, "zvkned");
    if (ext & RISCV_HWPROBE_EXT_ZVKNHA) set_feature(&features, "zvknha");
    if (ext & RISCV_HWPROBE_EXT_ZVKNHB) set_feature(&features, "zvknhb");
    if (ext & RISCV_HWPROBE_EXT_ZVKSED) set_feature(&features, "zvksed");
    if (ext & RISCV_HWPROBE_EXT_ZVKSH)  set_feature(&features, "zvksh");
    if (ext & RISCV_HWPROBE_EXT_ZVKT)   set_feature(&features, "zvkt");

    if (ext & RISCV_HWPROBE_EXT_ZFH)    set_feature(&features, "zfh");
    if (ext & RISCV_HWPROBE_EXT_ZFHMIN) set_feature(&features, "zfhmin");
    if (ext & RISCV_HWPROBE_EXT_ZVFH)   set_feature(&features, "zvfh");
    if (ext & RISCV_HWPROBE_EXT_ZVFHMIN) set_feature(&features, "zvfhmin");
    if (ext & RISCV_HWPROBE_EXT_ZFA)    set_feature(&features, "zfa");

    if (ext & RISCV_HWPROBE_EXT_ZICBOZ)     set_feature(&features, "zicboz");
    if (ext & RISCV_HWPROBE_EXT_ZIHINTNTL)   set_feature(&features, "zihintntl");
    if (ext & RISCV_HWPROBE_EXT_ZIHINTPAUSE) set_feature(&features, "zihintpause");
    if (ext & RISCV_HWPROBE_EXT_ZTSO)        set_feature(&features, "ztso");
    if (ext & RISCV_HWPROBE_EXT_ZACAS)       set_feature(&features, "zacas");
    if (ext & RISCV_HWPROBE_EXT_ZICOND)      set_feature(&features, "zicond");
    if (ext & RISCV_HWPROBE_EXT_ZIMOP)       set_feature(&features, "zimop");
    if (ext & RISCV_HWPROBE_EXT_ZAWRS)       set_feature(&features, "zawrs");

    if (ext & RISCV_HWPROBE_EXT_ZVE32X) set_feature(&features, "zve32x");
    if (ext & RISCV_HWPROBE_EXT_ZVE32F) set_feature(&features, "zve32f");
    if (ext & RISCV_HWPROBE_EXT_ZVE64X) set_feature(&features, "zve64x");
    if (ext & RISCV_HWPROBE_EXT_ZVE64F) set_feature(&features, "zve64f");
    if (ext & RISCV_HWPROBE_EXT_ZVE64D) set_feature(&features, "zve64d");

    if (ext & RISCV_HWPROBE_EXT_ZCA)  set_feature(&features, "zca");
    if (ext & RISCV_HWPROBE_EXT_ZCB)  set_feature(&features, "zcb");
    if (ext & RISCV_HWPROBE_EXT_ZCD)  set_feature(&features, "zcd");
    if (ext & RISCV_HWPROBE_EXT_ZCF)  set_feature(&features, "zcf");
    if (ext & RISCV_HWPROBE_EXT_ZCMOP) set_feature(&features, "zcmop");

    if (query[2].key != -1 &&
        query[2].value == RISCV_HWPROBE_MISALIGNED_SCALAR_FAST) {
        set_feature(&features, "unaligned-scalar-mem");
    }
#endif

    expand_implied(&features);
    return features;
}

} // namespace tp
