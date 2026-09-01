#include "semantic_fs/ocr/image_preprocessor.h"

#include <leptonica/allheaders.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace semantic_fs::ocr {
namespace {

struct PixDestroyer {
    void operator()(Pix* image) const
    {
        if (image != nullptr) {
            pixDestroy(&image);
        }
    }
};

struct BoxDestroyer {
    void operator()(BOX* box) const
    {
        if (box != nullptr) {
            boxDestroy(&box);
        }
    }
};

std::filesystem::path outputPathFor(
    const std::filesystem::path& imagePath,
    const TextRegion& region,
    const std::string& variantName
)
{
    const auto outputDir = std::filesystem::temp_directory_path() / "semantic_fs_ocr";
    std::filesystem::create_directories(outputDir);

    std::ostringstream name;
    name << imagePath.stem().string()
         << "_"
         << region.label
         << "_"
         << region.x
         << "_"
         << region.y
         << "_"
         << region.width
         << "x"
         << region.height
         << "_"
         << variantName
         << ".png";

    return outputDir / name.str();
}

PreprocessedImage writeVariant(
    const std::filesystem::path& imagePath,
    const TextRegion& region,
    const std::string& variantName,
    Pix* image
)
{
    const auto outputPath = outputPathFor(imagePath, region, variantName);
    if (pixWrite(outputPath.string().c_str(), image, IFF_PNG) != 0) {
        throw std::runtime_error("Could not write preprocessed OCR image: " + outputPath.string());
    }

    return {
        .path = outputPath,
        .sourceRegion = region,
        .description = variantName
    };
}

float scaleFactorFor(const TextRegion& region)
{
    if (region.width >= 900 || region.height >= 500) {
        return 1.25f;
    }
    if (region.width >= 600 || region.height >= 350) {
        return 2.0f;
    }
    return 3.0f;
}

} // namespace

PreprocessedImage ImagePreprocessor::preprocess(
    const std::filesystem::path& imagePath,
    const TextRegion& region
) const
{
    const auto variants = preprocessVariants(imagePath, region);
    if (variants.empty()) {
        throw std::runtime_error("No OCR preprocessing variants generated: " + imagePath.string());
    }

    return variants.front();
}

std::vector<PreprocessedImage> ImagePreprocessor::preprocessVariants(
    const std::filesystem::path& imagePath,
    const TextRegion& region
) const
{
    return preprocessSelectedVariants(
        imagePath,
        region,
        {
            "scaled_3x",
            "grayscale_scaled_3x",
            "binary_threshold_190",
            "binary_threshold_190_inverted",
            "binary_threshold_140",
            "binary_threshold_140_inverted",
            "sharpened_grayscale"
        }
    );
}

std::vector<PreprocessedImage> ImagePreprocessor::preprocessSelectedVariants(
    const std::filesystem::path& imagePath,
    const TextRegion& region,
    const std::vector<std::string>& variantNames
) const
{
    std::unique_ptr<Pix, PixDestroyer> original(pixRead(imagePath.string().c_str()));
    if (!original) {
        throw std::runtime_error("Could not read image for preprocessing: " + imagePath.string());
    }

    std::unique_ptr<BOX, BoxDestroyer> box(boxCreate(region.x, region.y, region.width, region.height));
    std::unique_ptr<Pix, PixDestroyer> cropped(pixClipRectangle(original.get(), box.get(), nullptr));
    if (!cropped) {
        throw std::runtime_error("Could not crop OCR text region: " + imagePath.string());
    }

    // Escalar ayuda con captions pequenos, pero en screenshots grandes un 3x
    // vuelve carisimo a Tesseract. Por eso el factor baja segun el tamano.
    const auto scaleFactor = scaleFactorFor(region);
    std::unique_ptr<Pix, PixDestroyer> scaled(pixScale(cropped.get(), scaleFactor, scaleFactor));
    if (!scaled) {
        throw std::runtime_error("Could not scale OCR text region: " + imagePath.string());
    }

    const auto wants = [&variantNames](const std::string& name) {
        return std::find(variantNames.begin(), variantNames.end(), name) != variantNames.end();
    };

    std::vector<PreprocessedImage> variants;
    if (wants("scaled_3x")) {
        variants.push_back(writeVariant(imagePath, region, "scaled_3x", scaled.get()));
    }

    std::unique_ptr<Pix, PixDestroyer> grayscale(pixConvertTo8(scaled.get(), 0));
    if (grayscale) {
        if (wants("grayscale_scaled_3x")) {
            variants.push_back(writeVariant(imagePath, region, "grayscale_scaled_3x", grayscale.get()));
        }

        // Umbral fijo alto: suele ayudar con subtitulos blancos sobre fondo oscuro/mixto.
        std::unique_ptr<Pix, PixDestroyer> binaryHigh(pixThresholdToBinary(grayscale.get(), 190));
        if (binaryHigh) {
            if (wants("binary_threshold_190")) {
                variants.push_back(writeVariant(imagePath, region, "binary_threshold_190", binaryHigh.get()));
            }

            std::unique_ptr<Pix, PixDestroyer> invertedHigh(pixInvert(nullptr, binaryHigh.get()));
            if (invertedHigh && wants("binary_threshold_190_inverted")) {
                variants.push_back(writeVariant(imagePath, region, "binary_threshold_190_inverted", invertedHigh.get()));
            }
        }

        // Umbral medio: fallback para texto oscuro o fondos mas claros.
        std::unique_ptr<Pix, PixDestroyer> binaryMid(pixThresholdToBinary(grayscale.get(), 140));
        if (binaryMid) {
            if (wants("binary_threshold_140")) {
                variants.push_back(writeVariant(imagePath, region, "binary_threshold_140", binaryMid.get()));
            }

            std::unique_ptr<Pix, PixDestroyer> invertedMid(pixInvert(nullptr, binaryMid.get()));
            if (invertedMid && wants("binary_threshold_140_inverted")) {
                variants.push_back(writeVariant(imagePath, region, "binary_threshold_140_inverted", invertedMid.get()));
            }
        }

        // Enfoque liviano para reforzar bordes de letras en imagenes comprimidas.
        std::unique_ptr<Pix, PixDestroyer> sharpened(pixUnsharpMasking(grayscale.get(), 2, 0.5f));
        if (sharpened && wants("sharpened_grayscale")) {
            variants.push_back(writeVariant(imagePath, region, "sharpened_grayscale", sharpened.get()));
        }
    }

    return variants;
}

} // namespace semantic_fs::ocr
