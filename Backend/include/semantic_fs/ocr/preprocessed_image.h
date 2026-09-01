#pragma once

#include "semantic_fs/ocr/text_region.h"

#include <filesystem>
#include <string>

namespace semantic_fs::ocr {

// Imagen temporal preparada para que Tesseract reciba una entrada mas limpia.
struct PreprocessedImage {
    std::filesystem::path path;
    TextRegion sourceRegion;
    std::string description;
};

} // namespace semantic_fs::ocr
