#pragma once

#include "semantic_fs/core/file_document.h"
#include "semantic_fs/extractors/extraction_types.h"
#include "semantic_fs/extractors/extractor_factory.h"
#include "semantic_fs/knowledge/knowledge_module.h"
#include "semantic_fs/rag/rag_module.h"

#include <filesystem>

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
    IndexingOrchestrator(
        semantic_fs::extractors::ExtractorFactory extractorFactory,
        semantic_fs::rag::RagModule ragModule,
        semantic_fs::knowledge::KnowledgeModule knowledgeModule = semantic_fs::knowledge::KnowledgeModule {}
    );

    // Crea el objeto documento a partir de una ruta recibida desde afuera.
    // Hoy lo usa main como prueba; en el futuro puede llamarlo el modulo que
    // recibe metadata desde el sistema operativo.
    FileDocument createDocument(const std::filesystem::path& filePath) const;

    // Devuelve true si existe un extractor compatible con el archivo.
    bool canIndex(const FileDocument& file) const;

    // Ejecuta solo la etapa de extraccion y devuelve el resultado crudo.
    ExtractionResult extractFile(const FileDocument& file) const;

    // Version minima del flujo de indexacion:
    // recibe un FileDocument, ejecuta el extractor correcto y agrega los
    // segmentos extraidos como contextos del archivo.
    FileDocument indexFile(FileDocument file) const;

    // Atajo para la prueba actual: recibe una ruta, crea FileDocument y lo
    // pasa por el flujo minimo de indexacion.
    FileDocument indexPath(const std::filesystem::path& filePath) const;
    const semantic_fs::rag::RagModule& ragModule() const;
    const semantic_fs::knowledge::KnowledgeModule& knowledgeModule() const;

private:
    semantic_fs::extractors::ExtractorFactory extractorFactory_;
    semantic_fs::rag::RagModule ragModule_;
    semantic_fs::knowledge::KnowledgeModule knowledgeModule_;
};

} // namespace semantic_fs::orchestration
