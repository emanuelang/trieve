#pragma once

#include "semantic_fs/extractors/extraction_types.h"

#include <string>

namespace semantic_fs::extractors {

class IContentExtractor {
public:
    virtual ~IContentExtractor() = default;

    // Ejecuta una extraccion interna sobre una entrada ya clasificada.
    // TesseractOcrExtractor implementa este metodo para convertir imagen -> texto.
    virtual ExtractedSegment extract(const ContentInput& input) const = 0;

    // Permite preguntar si este servicio sabe procesar esa entrada antes de usarlo.
    virtual bool supports(const ContentInput& input) const = 0;

    // Nombre util para logs, debug y metadata de los segmentos extraidos.
    virtual std::string name() const = 0;
};

} // namespace semantic_fs::extractors
