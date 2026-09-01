#pragma once

#include <string>

namespace semantic_fs::ocr {

// Region rectangular de una imagen donde probablemente haya texto.
struct TextRegion {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    double confidence = 0.0;
    std::string label;
};

} // namespace semantic_fs::ocr
