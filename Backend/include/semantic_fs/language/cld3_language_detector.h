#pragma once

#include "semantic_fs/language/i_language_detector.h"

namespace semantic_fs::language {

// Implementacion real basada en Google Compact Language Detector v3.
// CLD3 es una buena opcion para producto liviano: no requiere modelos grandes
// externos y funciona bien con textos cortos o medianos.
class Cld3LanguageDetector final : public ILanguageDetector {
public:
    LanguageDetectionResult detect(const std::string& text) const override;
};

} // namespace semantic_fs::language
