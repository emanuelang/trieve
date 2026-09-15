#pragma once

#include "semantic_fs/ocr/preprocessed_image.h"
#include "semantic_fs/ocr/text_region.h"

#include <filesystem>
#include <string>
#include <vector>

namespace semantic_fs::ocr {

class ImagePreprocessor {
public:
    // Recorta una region, la agranda y la guarda como PNG temporal.
    PreprocessedImage preprocess(
        const std::filesystem::path& imagePath,
        const TextRegion& region
    ) const;

    // Genera varias versiones de la misma region para mejorar OCR en casos
    // dificiles: captions chicos, texto blanco con borde, fondos complejos, etc.
    std::vector<PreprocessedImage> preprocessVariants(
        const std::filesystem::path& imagePath,
        const TextRegion& region
    ) const;

    // Genera solamente las variantes pedidas. Esto permite que el OCR rapido
    // no pague el costo de crear imagenes que no va a usar.
    std::vector<PreprocessedImage> preprocessSelectedVariants(
        const std::filesystem::path& imagePath,
        const TextRegion& region,
        const std::vector<std::string>& variantNames
    ) const;
};

} // namespace semantic_fs::ocr
