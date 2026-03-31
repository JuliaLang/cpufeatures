// Fallback host detection for unsupported architectures.
// Reports "generic" CPU with no features.

#include "target_tables_fallback.h"
#include "target_parsing.h"

namespace tp {

const std::string &get_host_cpu_name() {
    static std::string name = "generic";
    return name;
}

FeatureBits get_host_features() {
    return FeatureBits{};
}

} // namespace tp
