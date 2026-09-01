#pragma once

#include <string>
#include <vector>

namespace semantic_fs::vision {

struct VisualDescriptionResult {
    std::string summary;
    std::string visualType = "unknown";
    std::vector<std::string> concepts;
    double confidence = 0.0;
};

} // namespace semantic_fs::vision
