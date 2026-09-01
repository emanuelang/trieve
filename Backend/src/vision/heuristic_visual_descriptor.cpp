#include "semantic_fs/vision/heuristic_visual_descriptor.h"

#include <leptonica/allheaders.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <sstream>

namespace semantic_fs::vision {
namespace {

struct PixDestroyer {
    void operator()(Pix* image) const
    {
        if (image != nullptr) {
            pixDestroy(&image);
        }
    }
};

struct VisualColorSignals {
    bool loaded = false;
    double warmRatio = 0.0;
    double brightWarmRatio = 0.0;
    double blueRatio = 0.0;
    double darkRatio = 0.0;
    double whiteRatio = 0.0;
    double blackRatio = 0.0;
    double lightNeutralRatio = 0.0;
    double greenRatio = 0.0;
    double topWarmRatio = 0.0;
    double middleDarkRatio = 0.0;
    double lowerDarkRatio = 0.0;
    double aspectRatio = 0.0;
};

std::string lowerCopy(std::string value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        }
    );
    return value;
}

bool contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

bool containsAny(const std::string& text, const std::vector<std::string>& needles)
{
    return std::any_of(needles.begin(), needles.end(), [&text](const std::string& needle) {
        return contains(text, needle);
    });
}

void addUnique(std::vector<std::string>& values, std::string value)
{
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(std::move(value));
    }
}

std::string joinConcepts(const std::vector<std::string>& concepts)
{
    std::ostringstream output;
    for (std::size_t index = 0; index < concepts.size(); ++index) {
        if (index > 0) {
            output << ", ";
        }
        output << concepts[index];
    }
    return output.str();
}

VisualColorSignals readColorSignals(const std::filesystem::path& imagePath)
{
    std::unique_ptr<Pix, PixDestroyer> image(pixRead(imagePath.string().c_str()));
    if (!image) {
        return {};
    }

    std::unique_ptr<Pix, PixDestroyer> rgbImage(pixConvertTo32(image.get()));
    if (!rgbImage) {
        return {};
    }

    const int width = pixGetWidth(rgbImage.get());
    const int height = pixGetHeight(rgbImage.get());
    if (width <= 0 || height <= 0) {
        return {};
    }

    int samples = 0;
    int warm = 0;
    int brightWarm = 0;
    int blue = 0;
    int dark = 0;
    int white = 0;
    int black = 0;
    int lightNeutral = 0;
    int greenCount = 0;
    int topSamples = 0;
    int topWarm = 0;
    int middleSamples = 0;
    int middleDark = 0;
    int lowerSamples = 0;
    int lowerDark = 0;

    const int stepX = std::max(1, width / 64);
    const int stepY = std::max(1, height / 64);
    for (int y = stepY / 2; y < height; y += stepY) {
        for (int x = stepX / 2; x < width; x += stepX) {
            l_int32 red = 0;
            l_int32 greenValue = 0;
            l_int32 blueValue = 0;
            pixGetRGBPixel(rgbImage.get(), x, y, &red, &greenValue, &blueValue);

            ++samples;
            if (red > greenValue * 1.08 && red > blueValue * 1.35 && red > 90) {
                ++warm;
            }
            if (red > 180 && greenValue > 70 && blueValue < 130) {
                ++brightWarm;
            }
            if (blueValue > red * 1.10 && blueValue > greenValue * 1.05) {
                ++blue;
            }
            if (red + greenValue + blueValue < 120) {
                ++dark;
            }
            if (red > 220 && greenValue > 220 && blueValue > 220) {
                ++white;
            }
            if (red < 45 && greenValue < 45 && blueValue < 65) {
                ++black;
            }
            if (red > 150 && greenValue > 150 && blueValue > 150 && std::abs(red - greenValue) < 45 && std::abs(greenValue - blueValue) < 45) {
                ++lightNeutral;
            }
            if (greenValue > red * 1.08 && greenValue > blueValue * 1.05 && greenValue > 70) {
                ++greenCount;
            }

            if (y < height * 0.35) {
                ++topSamples;
                if (red > greenValue * 1.03 && red > blueValue * 1.20 && red > 100) {
                    ++topWarm;
                }
            } else if (y < height * 0.72) {
                ++middleSamples;
                if (red + greenValue + blueValue < 190) {
                    ++middleDark;
                }
            } else {
                ++lowerSamples;
                if (red + greenValue + blueValue < 190) {
                    ++lowerDark;
                }
            }
        }
    }

    if (samples == 0) {
        return {};
    }

    return {
        .loaded = true,
        .warmRatio = static_cast<double>(warm) / static_cast<double>(samples),
        .brightWarmRatio = static_cast<double>(brightWarm) / static_cast<double>(samples),
        .blueRatio = static_cast<double>(blue) / static_cast<double>(samples),
        .darkRatio = static_cast<double>(dark) / static_cast<double>(samples),
        .whiteRatio = static_cast<double>(white) / static_cast<double>(samples),
        .blackRatio = static_cast<double>(black) / static_cast<double>(samples),
        .lightNeutralRatio = static_cast<double>(lightNeutral) / static_cast<double>(samples),
        .greenRatio = static_cast<double>(greenCount) / static_cast<double>(samples),
        .topWarmRatio = topSamples == 0 ? 0.0 : static_cast<double>(topWarm) / static_cast<double>(topSamples),
        .middleDarkRatio = middleSamples == 0 ? 0.0 : static_cast<double>(middleDark) / static_cast<double>(middleSamples),
        .lowerDarkRatio = lowerSamples == 0 ? 0.0 : static_cast<double>(lowerDark) / static_cast<double>(lowerSamples),
        .aspectRatio = static_cast<double>(width) / static_cast<double>(height)
    };
}

bool looksLikeTextGraphic(const VisualColorSignals& signals)
{
    return signals.loaded
        && signals.darkRatio > 0.30
        && signals.whiteRatio > 0.06
        && signals.blueRatio > 0.25;
}

bool looksLikeSunset(const std::string& combined, const VisualColorSignals& signals)
{
    if (containsAny(combined, { "sunset", "atardecer", "ocaso", "amanecer", "sunrise" })) {
        return true;
    }

    return signals.loaded
        && signals.warmRatio > 0.35
        && signals.brightWarmRatio > 0.08
        && signals.blueRatio < 0.25;
}

bool looksLikeMountainLandscape(const std::string& combined, const VisualColorSignals& signals)
{
    if (containsAny(combined, { "mountain", "mountains", "montana", "montaña", "cerro", "pico", "peak", "landscape", "paisaje" })) {
        return true;
    }

    return signals.loaded
        && signals.aspectRatio < 0.90
        && (signals.topWarmRatio > 0.10 || signals.warmRatio > 0.14)
        && (signals.middleDarkRatio > 0.10 || signals.lowerDarkRatio > 0.16)
        && signals.lightNeutralRatio > 0.04;
}

bool looksLikeNaturalLandscape(const std::string& combined, const VisualColorSignals& signals)
{
    if (containsAny(combined, { "landscape", "paisaje", "nature", "naturaleza", "forest", "bosque", "beach", "playa", "sea", "mar" })) {
        return true;
    }

    return signals.loaded
        && (signals.greenRatio > 0.18 || signals.blueRatio > 0.20 || signals.warmRatio > 0.25)
        && signals.lightNeutralRatio > 0.04;
}

} // namespace

VisualDescriptionResult HeuristicVisualDescriptor::describe(
    const std::filesystem::path& imagePath,
    const std::string& ocrText
) const
{
    const auto visualType = inferVisualType(imagePath, ocrText);
    const auto concepts = inferConcepts(imagePath, ocrText, visualType);

    std::ostringstream summary;
    if (visualType == "possible_text_graphic") {
        summary << "Imagen grafica con posible texto visible";
    } else if (visualType == "mountain_landscape") {
        summary << "Paisaje de montana con cielo calido y nubes";
    } else if (visualType == "sunset_landscape") {
        summary << "Paisaje de atardecer con colores calidos";
    } else if (visualType == "natural_landscape") {
        summary << "Paisaje natural";
    } else if (visualType == "economic_news_graphic") {
        summary << "Placa informativa de economia";
    } else if (visualType == "chart_or_graph") {
        summary << "Imagen con grafico o metrica visual";
    } else if (visualType == "document_or_text_heavy_image") {
        summary << "Imagen con mucho texto visible";
    } else if (visualType == "screenshot") {
        summary << "Captura de pantalla con contenido visual y texto";
    } else if (visualType == "captioned_image") {
        summary << "Imagen con texto superpuesto";
    } else {
        summary << "Imagen general";
    }

    if (!concepts.empty()) {
        summary << ". Conceptos detectados: " << joinConcepts(concepts);
    }
    summary << ".";

    return {
        .summary = summary.str(),
        .visualType = visualType,
        .concepts = concepts,
        .confidence = ocrText.empty() ? 0.45 : 0.65
    };
}

std::string HeuristicVisualDescriptor::inferVisualType(
    const std::filesystem::path& imagePath,
    const std::string& ocrText
) const
{
    const auto fileText = lowerCopy(imagePath.filename().string());
    const auto text = lowerCopy(ocrText);
    const auto combined = fileText + " " + text;
    const auto colorSignals = readColorSignals(imagePath);

    if (text.empty() && looksLikeTextGraphic(colorSignals)) {
        return "possible_text_graphic";
    }

    if (containsAny(
            combined,
            {
                "economia",
                "ventas",
                "minoristas",
                "comercios",
                "sector",
                "caida",
                "retroceso",
                "tendencia",
                "%"
            }
        )) {
        return "economic_news_graphic";
    }

    if (looksLikeSunset(combined, colorSignals)) {
        return "sunset_landscape";
    }

    if (!looksLikeTextGraphic(colorSignals) && looksLikeMountainLandscape(combined, colorSignals)) {
        return "mountain_landscape";
    }

    if (!looksLikeTextGraphic(colorSignals) && looksLikeNaturalLandscape(combined, colorSignals)) {
        return "natural_landscape";
    }

    if (!text.empty() && text.size() > 120) {
        return "document_or_text_heavy_image";
    }

    if (!text.empty()) {
        return "captioned_image";
    }

    if (contains(fileText, "screenshot") || contains(fileText, "screen")) {
        return "screenshot";
    }

    if (contains(fileText, "chart") || contains(fileText, "graph") || contains(combined, "grafico")) {
        return "chart_or_graph";
    }

    return "image";
}

std::vector<std::string> HeuristicVisualDescriptor::inferConcepts(
    const std::filesystem::path& imagePath,
    const std::string& ocrText,
    const std::string& visualType
) const
{
    std::vector<std::string> concepts;
    const auto combined = lowerCopy(imagePath.filename().string() + " " + ocrText);
    const auto colorSignals = readColorSignals(imagePath);

    addUnique(concepts, visualType);

    if (!ocrText.empty()) {
        addUnique(concepts, "contains text");
    }
    if (looksLikeTextGraphic(colorSignals)) {
        addUnique(concepts, "posible texto visible");
        addUnique(concepts, "placa informativa");
        addUnique(concepts, "grafica con texto");
    }

    if (looksLikeSunset(combined, colorSignals)) {
        addUnique(concepts, "atardecer");
        addUnique(concepts, "sunset");
        addUnique(concepts, "paisaje");
        addUnique(concepts, "cielo naranja");
        addUnique(concepts, "sol");
        addUnique(concepts, "naturaleza");
    }

    if (!looksLikeTextGraphic(colorSignals) && looksLikeMountainLandscape(combined, colorSignals)) {
        addUnique(concepts, "paisaje");
        addUnique(concepts, "montana");
        addUnique(concepts, "montaña");
        addUnique(concepts, "mountain");
        addUnique(concepts, "pico de montana");
        addUnique(concepts, "nubes");
        addUnique(concepts, "niebla");
        addUnique(concepts, "cielo calido");
        addUnique(concepts, "naturaleza");
    }

    if (!looksLikeTextGraphic(colorSignals) && looksLikeNaturalLandscape(combined, colorSignals)) {
        addUnique(concepts, "paisaje");
        addUnique(concepts, "naturaleza");
        addUnique(concepts, "outdoor");
        addUnique(concepts, "escena natural");
    }

    if (contains(combined, "music") || contains(combined, "lyric") || contains(combined, "techno")) {
        addUnique(concepts, "music");
    }
    if (contains(combined, "invoice") || contains(combined, "receipt") || contains(combined, "factura")) {
        addUnique(concepts, "financial document");
    }
    if (contains(combined, "contract") || contains(combined, "contrato")) {
        addUnique(concepts, "legal document");
    }
    if (contains(combined, "chart") || contains(combined, "graph") || contains(combined, "revenue")) {
        addUnique(concepts, "chart or business metric");
    }
    if (containsAny(combined, { "economia", "ventas", "minoristas", "comercios", "sector" })) {
        addUnique(concepts, "noticia economica");
    }
    if (containsAny(combined, { "ventas", "minoristas", "comercios" })) {
        addUnique(concepts, "ventas minoristas");
        addUnique(concepts, "comercio");
    }
    if (containsAny(combined, { "caida", "retroceso", "baja", "desfavorable" })) {
        addUnique(concepts, "tendencia negativa");
    }
    if (containsAny(combined, { "grafico", "tendencia", "linea", "%" })) {
        addUnique(concepts, "grafico de linea");
    }
    if (contains(combined, "%") || containsAny(combined, { "2023", "2024", "2025" })) {
        addUnique(concepts, "estadisticas por anio");
    }
    if (contains(combined, "code") || contains(combined, "class") || contains(combined, "function")) {
        addUnique(concepts, "programming");
    }
    if (contains(combined, "login") || contains(combined, "dashboard")) {
        addUnique(concepts, "software interface");
    }

    return concepts;
}

} // namespace semantic_fs::vision
