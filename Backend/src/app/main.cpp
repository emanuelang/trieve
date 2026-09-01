#include "semantic_fs/orchestration/indexing_orchestrator.h"

#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <exception>
#include <filesystem>
#include <string>

namespace {

std::string sanitizeUtf8(const std::string& text)
{
    std::string clean;
    clean.reserve(text.size());

    for (std::size_t index = 0; index < text.size();) {
        const auto byte = static_cast<unsigned char>(text[index]);
        std::size_t sequenceLength = 0;

        if (byte <= 0x7F) {
            sequenceLength = 1;
        } else if ((byte & 0xE0) == 0xC0) {
            sequenceLength = 2;
        } else if ((byte & 0xF0) == 0xE0) {
            sequenceLength = 3;
        } else if ((byte & 0xF8) == 0xF0) {
            sequenceLength = 4;
        } else {
            ++index;
            continue;
        }

        if (index + sequenceLength > text.size()) {
            break;
        }

        bool valid = true;
        for (std::size_t offset = 1; offset < sequenceLength; ++offset) {
            const auto continuation = static_cast<unsigned char>(text[index + offset]);
            if ((continuation & 0xC0) != 0x80) {
                valid = false;
                break;
            }
        }

        if (valid) {
            clean.append(text, index, sequenceLength);
            index += sequenceLength;
        } else {
            ++index;
        }
    }

    return clean;
}

std::string pathToUtf8String(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::string utf8Preview(const std::string& text, std::size_t maxBytes)
{
    const auto clean = sanitizeUtf8(text);
    if (clean.size() <= maxBytes) {
        return clean;
    }

    std::size_t end = 0;
    while (end < clean.size() && end < maxBytes) {
        const auto byte = static_cast<unsigned char>(clean[end]);
        std::size_t sequenceLength = 1;
        if ((byte & 0xE0) == 0xC0) {
            sequenceLength = 2;
        } else if ((byte & 0xF0) == 0xE0) {
            sequenceLength = 3;
        } else if ((byte & 0xF8) == 0xF0) {
            sequenceLength = 4;
        }

        if (end + sequenceLength > maxBytes) {
            break;
        }
        end += sequenceLength;
    }

    return clean.substr(0, end);
}

} // namespace

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
        fmt::print("Path: {}\n", pathToUtf8String(document.path()));
        fmt::print("Name: {}\n", pathToUtf8String(document.path().filename()));
        fmt::print("Extension: {}\n", pathToUtf8String(document.path().extension()));
        fmt::print("Size bytes: {}\n", document.sizeBytes());
        fmt::print("Extracted contexts: {}\n", document.contextCount());

        for (const auto& context : document.contexts()) {
            fmt::print("Context source: {}\n", sanitizeUtf8(context.source));
            fmt::print("Detected language: {} ({:.2f})\n", sanitizeUtf8(context.detectedLanguage), context.languageConfidence);
            fmt::print("Extraction confidence: {:.2f}\n", context.extractionConfidence);
            fmt::print("Text preview: {}\n", utf8Preview(context.text, 300));
        }

        return 0;
    } catch (const std::exception& error) {
        // Cualquier fallo de lectura, metadata o extractor termina aca con un
        // mensaje claro para debug.
        spdlog::error("Indexing failed: {}", sanitizeUtf8(error.what()));
        return 1;
    }
}
