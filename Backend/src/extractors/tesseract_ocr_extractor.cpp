#include "semantic_fs/extractors/tesseract_ocr_extractor.h"

#include <leptonica/allheaders.h>
#include <tesseract/baseapi.h>

#include <memory>
#include <stdexcept>

namespace semantic_fs::extractors {
namespace {

struct PixDestroyer {
    void operator()(Pix* image) const
    {
        if (image != nullptr) {
            pixDestroy(&image);
        }
    }
};

struct TextDestroyer {
    void operator()(char* text) const
    {
        delete[] text;
    }
};

} // namespace

TesseractOcrExtractor::TesseractOcrExtractor(
    std::string language,
    std::string tessdataPath
)
    : language_(std::move(language)),
      tessdataPath_(std::move(tessdataPath))
{
}

std::string TesseractOcrExtractor::extractText(const std::string& imagePath) const
{
    std::unique_ptr<Pix, PixDestroyer> image(pixRead(imagePath.c_str()));
    if (!image) {
        throw std::runtime_error("Could not read image: " + imagePath);
    }

    tesseract::TessBaseAPI api;
    const char* dataPath = tessdataPath_.empty() ? nullptr : tessdataPath_.c_str();

    if (api.Init(dataPath, language_.c_str()) != 0) {
        throw std::runtime_error(
            "Could not initialize Tesseract. Check tessdata path and language: " + language_
        );
    }

    api.SetImage(image.get());

    std::unique_ptr<char, TextDestroyer> text(api.GetUTF8Text());
    if (!text) {
        return "";
    }

    return std::string(text.get());
}

} // namespace semantic_fs::extractors
