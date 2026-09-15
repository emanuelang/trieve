#pragma once

#include "semantic_fs/vision/visual_description_result.h"

#include <filesystem>
#include <string>

namespace semantic_fs::vision {

class IVisualDescriptor {
public:
    virtual ~IVisualDescriptor() = default;

    // Describe visualmente una imagen. El texto OCR es opcional, pero ayuda a
    // crear conceptos livianos sin cargar un modelo visual pesado.
    virtual VisualDescriptionResult describe(
        const std::filesystem::path& imagePath,
        const std::string& ocrText
    ) const = 0;
};

} // namespace semantic_fs::vision
