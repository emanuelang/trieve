#pragma once

#include "semantic_fs/language/language_detection_result.h"

#include <string>

namespace semantic_fs::language {

class ILanguageDetector {
public:
    virtual ~ILanguageDetector() = default;

    // Detecta el idioma principal de un bloque de texto.
    // El texto puede venir de OCR, TXT, PDF o transcripciones futuras.
    virtual LanguageDetectionResult detect(const std::string& text) const = 0;
};

} // namespace semantic_fs::language
