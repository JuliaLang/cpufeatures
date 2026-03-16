// CPU name alias resolution.
// Maps alternate CPU names to canonical names in the generated tables.
// From LLVM's ProcessorAlias definitions.

#ifndef CPU_ALIASES_H
#define CPU_ALIASES_H

#include <string_view>

// Pure name mapping — no table lookups.
// Callers should check if the result exists in their table.
inline const char *resolve_cpu_alias(const char *name) {
    std::string_view sv(name);
    struct Alias { std::string_view from; const char *to; };
    static constexpr Alias aliases[] = {
        {"apple-m1", "apple-a14"},
        {"apple-m2", "apple-a15"},
        {"apple-m3", "apple-a16"},
        {"apple-a18", "apple-m4"},
        {"apple-a19", "apple-m5"},
    };
    for (const auto &a : aliases)
        if (sv == a.from) return a.to;
    return name;
}

#endif // CPU_ALIASES_H
