#include "semantic_fs/ocr/text_region_detector.h"

#include <leptonica/allheaders.h>

#include <memory>
#include <stdexcept>

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

TextRegion makeRegion(
    int x,
    int y,
    int width,
    int height,
    double confidence,
    std::string label
)
{
    return {
        .x = x,
        .y = y,
        .width = width,
        .height = height,
        .confidence = confidence,
        .label = std::move(label)
    };
}

} // namespace

std::vector<TextRegion> TextRegionDetector::detect(const std::filesystem::path& imagePath) const
{
    std::unique_ptr<Pix, PixDestroyer> image(pixRead(imagePath.string().c_str()));
    if (!image) {
        throw std::runtime_error("Could not read image for text region detection: " + imagePath.string());
    }

    const int width = pixGetWidth(image.get());
    const int height = pixGetHeight(image.get());
    std::vector<TextRegion> regions;

    if (width <= 0 || height <= 0) {
        return regions;
    }

    // Region comun para captions en imagenes verticales o memes: parte superior amplia.
    regions.push_back(makeRegion(
        0,
        0,
        width,
        static_cast<int>(height * 0.60),
        0.75,
        "upper_caption_area"
    ));

    // Banda mas precisa para captions centrados en la mitad superior, comun en
    // imagenes de redes sociales y frames verticales.
    regions.push_back(makeRegion(
        0,
        static_cast<int>(height * 0.25),
        width,
        static_cast<int>(height * 0.35),
        0.85,
        "social_caption_band"
    ));

    // Region central: util para screenshots o texto superpuesto en el medio.
    regions.push_back(makeRegion(
        0,
        static_cast<int>(height * 0.20),
        width,
        static_cast<int>(height * 0.60),
        0.60,
        "center_text_area"
    ));

    // Banda inferior: util para placas informativas, noticias y capturas con
    // texto descriptivo abajo de una imagen o grafico.
    regions.push_back(makeRegion(
        0,
        static_cast<int>(height * 0.62),
        width,
        static_cast<int>(height * 0.38),
        0.72,
        "lower_caption_area"
    ));

    // Fallback: imagen completa para documentos o casos donde no sabemos donde esta el texto.
    regions.push_back(makeRegion(
        0,
        0,
        width,
        height,
        0.50,
        "full_image"
    ));

    return regions;
}

} // namespace semantic_fs::ocr
