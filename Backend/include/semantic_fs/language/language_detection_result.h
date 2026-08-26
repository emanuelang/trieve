#pragma once

#include <string>

namespace semantic_fs::language {

// Resultado normalizado de una deteccion de idioma.
// languageCode usa codigos ISO cortos cuando el detector puede reconocerlos:
// "es", "en", "fr", etc. Si no hay confianza suficiente, queda "unknown".
struct LanguageDetectionResult {
    std::string languageCode = "unknown";
    double confidence = 0.0;
    bool reliable = false;
};

} // namespace semantic_fs::language
