#pragma once

#include "semantic_fs/core/file_document.h"
#include "semantic_fs/extractors/extraction_types.h"
#include "semantic_fs/extractors/extractor_factory.h"

namespace semantic_fs::orchestration {

// IndexingOrchestrator coordina el caso de uso "indexar archivo".
// Por ahora solo conecta FileDocument con el modulo de extractores. Mas adelante
// deberia llamar tambien a processing, chunking, embeddings y storage.
class IndexingOrchestrator {
public:
    using FileDocument = semantic_fs::core::FileDocument;
    using ExtractionResult = semantic_fs::extractors::ExtractionResult;

    IndexingOrchestrator() = default;
    explicit IndexingOrchestrator(semantic_fs::extractors::ExtractorFactory extractorFactory);

    // Devuelve true si existe un extractor compatible con el archivo.
    bool canIndex(const FileDocument& file) const;

    // Ejecuta solo la etapa de extraccion y devuelve el resultado crudo.
    ExtractionResult extractFile(const FileDocument& file) const;

    // Version minima del flujo de indexacion:
    // recibe un FileDocument, ejecuta el extractor correcto y agrega los
    // segmentos extraidos como contextos del archivo.
    FileDocument indexFile(FileDocument file) const;

private:
    semantic_fs::extractors::ExtractorFactory extractorFactory_;
};

} // namespace semantic_fs::orchestration
