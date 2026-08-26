#include "semantic_fs/orchestration/indexing_orchestrator.h"

#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <exception>

int main(int argc, char* argv[])
{
    // CLI de prueba: el primer argumento simula la ruta que mas adelante
    // llegara desde el modulo conectado al sistema operativo.
    if (argc < 2) {
        spdlog::info("Usage: semantic_fs_backend <image_path>");
        return 0;
    }

    try {
        const semantic_fs::orchestration::IndexingOrchestrator orchestrator {};

        // En esta prueba main solo envia una ruta. El orquestador crea el
        // FileDocument, carga metadata basica y llama al extractor compatible.
        const auto document = orchestrator.indexPath(argv[1]);

        fmt::print("Document created\n");
        fmt::print("Path: {}\n", document.path().string());
        fmt::print("Name: {}\n", document.fileName());
        fmt::print("Extension: {}\n", document.extension());
        fmt::print("Size bytes: {}\n", document.sizeBytes());
        fmt::print("Extracted contexts: {}\n", document.contextCount());

        for (const auto& context : document.contexts()) {
            fmt::print("Context source: {}\n", context.source);
            fmt::print("Detected language: {} ({:.2f})\n", context.detectedLanguage, context.languageConfidence);
            fmt::print("Text preview: {}\n", context.text.substr(0, 300));
        }

        return 0;
    } catch (const std::exception& error) {
        // Cualquier fallo de lectura, metadata o extractor termina aca con un
        // mensaje claro para debug.
        spdlog::error("Indexing failed: {}", error.what());
        return 1;
    }
}
