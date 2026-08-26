#pragma once

#include "semantic_fs/extractors/extraction_types.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace semantic_fs::core {

// FileDocument representa el archivo como entidad principal del sistema.
// Guarda metadata del archivo fisico y los distintos contextos/contenidos que
// los extractores hayan producido para ese archivo.
class FileDocument {
public:
    using ExtractedSegment = semantic_fs::extractors::ExtractedSegment;
    using FileType = semantic_fs::extractors::FileType;

    FileDocument() = default;
    explicit FileDocument(std::filesystem::path path, FileType type = FileType::Unknown);

    // Getters de identidad y ubicacion.
    const std::filesystem::path& path() const;
    std::filesystem::path absolutePath() const;
    std::filesystem::path directory() const;
    std::string fileName() const;
    std::string extension() const;

    // Getters de metadata.
    FileType fileType() const;
    const std::string& hash() const;
    std::uintmax_t sizeBytes() const;
    std::filesystem::file_time_type modifiedAt() const;

    // Getters de contenido extraido. Cada segmento funciona como un contexto
    // distinto: OCR, descripcion visual, transcripcion, texto de PDF, etc.
    const std::vector<ExtractedSegment>& contexts() const;
    bool hasContexts() const;
    std::size_t contextCount() const;

    // Setters para permitir que scanner, extractores o storage completen datos
    // a medida que avanza el pipeline.
    void setPath(std::filesystem::path path);
    void setFileType(FileType type);
    void setHash(std::string hash);
    void setSizeBytes(std::uintmax_t sizeBytes);
    void setModifiedAt(std::filesystem::file_time_type modifiedAt);

    // Operaciones sobre los contextos extraidos del archivo.
    void addContext(ExtractedSegment segment);
    void setContexts(std::vector<ExtractedSegment> contexts);
    void clearContexts();

    // Lee metadata basica desde disco si el archivo existe.
    // No calcula hash todavia: eso puede quedar para un servicio especializado.
    void refreshMetadataFromDisk();

private:
    std::filesystem::path path_;
    FileType fileType_ = FileType::Unknown;
    std::string hash_;
    std::uintmax_t sizeBytes_ = 0;
    std::filesystem::file_time_type modifiedAt_ {};
    std::vector<ExtractedSegment> contexts_;
};

} // namespace semantic_fs::core
