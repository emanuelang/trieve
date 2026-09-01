#pragma once

#include "semantic_fs/vision/i_visual_descriptor.h"

namespace semantic_fs::vision {

// Descriptor visual liviano para V1.
// No entiende la imagen como CLIP/SigLIP, pero genera contexto util usando
// dimensiones, metadata y texto OCR.
class HeuristicVisualDescriptor final : public IVisualDescriptor {
public:
    VisualDescriptionResult describe(
        const std::filesystem::path& imagePath,
        const std::string& ocrText
    ) const override;

private:
    std::string inferVisualType(
        const std::filesystem::path& imagePath,
        const std::string& ocrText
    ) const;

    std::vector<std::string> inferConcepts(
        const std::filesystem::path& imagePath,
        const std::string& ocrText,
        const std::string& visualType
    ) const;
};

} // namespace semantic_fs::vision
