// Fallback host detection for unsupported architectures.
// Reports "generic" CPU with no features.

#include "target_tables_fallback.h"
#include "target_parsing.h"

namespace tp {

const std::string &get_host_cpu_name() {
    static std::string name = "generic";
    return name;
}

FeatureBits detect_host_features() {
    return FeatureBits{};
}

const char *const *get_host_feature_detection(HostFeatureDetectionKind) {
    static const char *empty[] = { nullptr };
    return empty;
}

} // namespace tp
