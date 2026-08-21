#include "semantic_fs/extractors/tesseract_ocr_extractor.h"

#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <exception>

int main(int argc, char* argv[])
{
    // CLI de prueba: el primer argumento debe ser la ruta de la imagen.
    if (argc < 2) {
        spdlog::info("Usage: semantic_fs_backend <image_path>");
        return 0;
    }

    try {
        // Adaptamos el argumento del usuario al contrato interno del extractor.
        // source indica de donde vino la entrada; aca es solo el debug CLI.
        const semantic_fs::extractors::ContentInput input {
            .type = semantic_fs::extractors::ContentType::Image,
            .path = argv[1],
            .source = "debug_cli"
        };

        // Se usa la interfaz IContentExtractor para probar polimorfismo:
        // el resto del sistema podria recibir cualquier extractor interno compatible.
        const semantic_fs::extractors::IContentExtractor& extractor =
            semantic_fs::extractors::TesseractOcrExtractor("eng", "tessdata");

        // Ejecuta OCR y devuelve el texto dentro de ExtractedSegment.
        const auto segment = extractor.extract(input);

        // Para esta prueba, solo imprimimos el texto detectado en consola.
        fmt::print("{}\n", segment.text);
        return 0;
    } catch (const std::exception& error) {
        // Cualquier fallo de lectura, inicializacion de Tesseract o entrada invalida
        // termina aca con un mensaje claro para debug.
        spdlog::error("OCR failed: {}", error.what());
        return 1;
    }
}
