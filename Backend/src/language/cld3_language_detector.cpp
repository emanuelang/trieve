#include "semantic_fs/language/cld3_language_detector.h"

#include <cld3/nnet_language_identifier.h>

#include <algorithm>
#include <cctype>

namespace semantic_fs::language {
namespace {

std::string trimCopy(const std::string& value)
{
    auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });

    auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();

    if (begin >= end) {
        return {};
    }

    return { begin, end };
}

} // namespace

LanguageDetectionResult Cld3LanguageDetector::detect(const std::string& text) const
{
    const auto normalizedText = trimCopy(text);
    if (normalizedText.empty()) {
        return {};
    }

    // Estos limites son los recomendables para uso general: acepta textos cortos
    // sin forzar demasiado ruido y recorta entradas largas para mantenerlo rapido.
    chrome_lang_id::NNetLanguageIdentifier identifier(
        0,
        1000
    );

    const auto result = identifier.FindLanguage(normalizedText);
    if (result.language.empty() || result.language == "und") {
        return {};
    }

    return {
        .languageCode = result.language,
        .confidence = result.probability,
        .reliable = result.is_reliable
    };
}

} // namespace semantic_fs::language
