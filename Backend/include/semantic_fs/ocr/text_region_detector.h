#pragma once

#include "semantic_fs/ocr/text_region.h"

#include <filesystem>
#include <vector>

namespace semantic_fs::ocr {

// Detector liviano de regiones de texto.
// Esta primera version no usa modelos: propone zonas comunes para documentos,
// captions y screenshots, mas la imagen completa como fallback.
class TextRegionDetector {
public:
    std::vector<TextRegion> detect(const std::filesystem::path& imagePath) const;
};

} // namespace semantic_fs::ocr
