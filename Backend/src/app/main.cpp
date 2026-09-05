#include "semantic_fs/answer/answer_module.h"
#include "semantic_fs/answer/ollama_client.h"
#include "semantic_fs/orchestration/indexing_orchestrator.h"

#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

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

bool isImagePath(const std::filesystem::path& path)
{
    auto extension = path.extension().string();
    for (auto& character : extension) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }

    return extension == ".png" ||
        extension == ".jpg" ||
        extension == ".jpeg" ||
        extension == ".webp" ||
        extension == ".bmp";
}

std::filesystem::path registryPath()
{
    return std::filesystem::current_path() / "data" / "loaded_files.txt";
}

std::vector<std::filesystem::path> loadRegisteredPaths()
{
    std::vector<std::filesystem::path> paths;
    std::ifstream input(registryPath());
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty()) {
            paths.emplace_back(line);
        }
    }
    return paths;
}

void registerPath(const std::filesystem::path& path)
{
    const auto absolute = std::filesystem::absolute(path);
    auto paths = loadRegisteredPaths();
    const auto absoluteText = pathToUtf8String(absolute);

    for (const auto& existing : paths) {
        if (pathToUtf8String(std::filesystem::absolute(existing)) == absoluteText) {
            return;
        }
    }

    std::filesystem::create_directories(registryPath().parent_path());
    std::ofstream output(registryPath(), std::ios::app);
    output << absoluteText << "\n";
}

void printUsage()
{
    fmt::print("Usage:\n");
    fmt::print("  semantic_fs_backend ingest <file_path>\n");
    fmt::print("  semantic_fs_backend ingest-folder <folder_path>\n");
    fmt::print("  semantic_fs_backend ask <question> [file_path]\n");
    fmt::print("  semantic_fs_backend <file_path> [question]\n");
}

void printDocumentDebug(const semantic_fs::core::FileDocument& document)
{
    fmt::print("\n[1/5] Archivo cargado\n");
    fmt::print("Path: {}\n", pathToUtf8String(document.path()));
    fmt::print("Name: {}\n", pathToUtf8String(document.path().filename()));
    fmt::print("Extension: {}\n", pathToUtf8String(document.path().extension()));
    fmt::print("Size bytes: {}\n", document.sizeBytes());

    fmt::print("\n[2/5] Extractores\n");
    fmt::print("Extracted contexts: {}\n", document.contextCount());
    for (const auto& context : document.contexts()) {
        fmt::print("Context source: {}\n", sanitizeUtf8(context.source));
        fmt::print("Detected language: {} ({:.2f})\n", sanitizeUtf8(context.detectedLanguage), context.languageConfidence);
        fmt::print("Extraction confidence: {:.2f}\n", context.extractionConfidence);
        fmt::print("Text preview: {}\n", utf8Preview(context.text, 300));
    }

    fmt::print("\n[3/5] Saneamiento RAG\n");
    if (document.hasRagIndexingResult()) {
        const auto& rag = document.ragIndexingResult();
        fmt::print("Raw segments: {}\n", rag.rawSegmentCount);
        fmt::print("Sanitized segments: {}\n", rag.sanitizedSegmentCount);
        fmt::print("Discarded segments: {}\n", rag.discardedSegmentCount);

        fmt::print("\n[4/5] Chunking\n");
        fmt::print("Chunks: {}\n", rag.chunkCount);

        fmt::print("\n[5/5] Embedding\n");
        fmt::print("Embeddings: {}\n", rag.embeddingCount);
    } else {
        fmt::print("RAG result unavailable\n");
    }
}

semantic_fs::answer::AnswerRequest makeAnswerRequest(
    std::string question,
    const std::vector<std::filesystem::path>& imagePaths,
    bool attachImagesToLlm
)
{
    semantic_fs::answer::AnswerRequest request {
        .question = std::move(question),
        .options = {}
    };

    request.imagePaths = imagePaths;
    request.options.attachImagesToLlm = attachImagesToLlm;
    request.options.singleBestSource = !attachImagesToLlm;
    if (!imagePaths.empty()) {
        request.options.topK = attachImagesToLlm ? 2 : 8;
        request.options.maxContextChunks = attachImagesToLlm ? 1 : 4;
        request.options.maxContextCharacters = attachImagesToLlm ? 800 : 2200;
    }

    return request;
}

void printAnswerDebug(const semantic_fs::answer::AnswerResult& answer)
{
    fmt::print("\nRespuesta\n");
    fmt::print("{}\n", sanitizeUtf8(answer.answer));
    fmt::print("\nFuentes: {}\n", answer.sources.size());
    for (const auto& source : answer.sources) {
        fmt::print(
            "- {} | score {:.3f} | {}\n",
            sanitizeUtf8(source.fileName),
            source.score,
            sanitizeUtf8(source.filePath)
        );
    }
    fmt::print("\nDebug respuesta\n");
    fmt::print("Retrieved chunks: {}\n", answer.debug.retrievedChunks);
    fmt::print("Used chunks: {}\n", answer.debug.usedChunks);
    fmt::print("Prompt chars: {}\n", answer.debug.promptCharacters);
    fmt::print("Model: {}\n", answer.debug.modelName);
    fmt::print("Fallback: {}\n", answer.debug.usedFallback ? "true" : "false");
    fmt::print("Latency ms: {}\n", answer.debug.latencyMs);
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc < 2) {
        printUsage();
        return 0;
    }

    try {
        const std::string command = argv[1];
        const semantic_fs::orchestration::IndexingOrchestrator orchestrator {};

        if (command == "ingest") {
            if (argc < 3) {
                printUsage();
                return 1;
            }

            fmt::print("Iniciando carga/indexacion\n");
            const auto document = orchestrator.indexPath(argv[2]);
            printDocumentDebug(document);
            registerPath(document.path());
            fmt::print("\nListo: archivo registrado para consultas.\n");
            fmt::print("Registry: {}\n", pathToUtf8String(registryPath()));
            return 0;
        }

        if (command == "ingest-folder") {
            if (argc < 3) {
                printUsage();
                return 1;
            }

            const std::filesystem::path folder = argv[2];
            if (!std::filesystem::exists(folder) || !std::filesystem::is_directory(folder)) {
                spdlog::error("Folder not found: {}", pathToUtf8String(folder));
                return 1;
            }

            std::size_t indexedCount = 0;
            fmt::print("Iniciando carga/indexacion de carpeta\n");
            for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
                if (!entry.is_regular_file() || !isImagePath(entry.path())) {
                    continue;
                }

                fmt::print("\n==============================\n");
                fmt::print("Imagen candidata: {}\n", pathToUtf8String(entry.path()));
                const auto document = orchestrator.indexPath(entry.path());
                printDocumentDebug(document);
                registerPath(document.path());
                ++indexedCount;
            }

            fmt::print("\nListo: imagenes registradas para consultas: {}\n", indexedCount);
            fmt::print("Registry: {}\n", pathToUtf8String(registryPath()));
            return 0;
        }

        if (command == "ask") {
            if (argc < 3) {
                printUsage();
                return 1;
            }

            std::vector<std::filesystem::path> paths;
            if (argc >= 4) {
                paths.emplace_back(argv[3]);
            } else {
                paths = loadRegisteredPaths();
            }

            if (paths.empty()) {
                spdlog::error("No hay archivos cargados. Primero ejecuta: semantic_fs_backend ingest <file_path>");
                return 1;
            }

            std::vector<std::filesystem::path> imagePaths;
            fmt::print("Preparando indice temporal para responder\n");
            for (const auto& path : paths) {
                const auto document = orchestrator.indexPath(path);
                printDocumentDebug(document);
                if (isImagePath(document.path())) {
                    imagePaths.push_back(document.path());
                }
            }

            try {
                const semantic_fs::answer::AnswerModule answerModule(
                    semantic_fs::answer::RagQueryService(orchestrator.ragModule()),
                    semantic_fs::answer::ContextRanker {},
                    semantic_fs::answer::PromptBuilder {},
                    std::make_shared<semantic_fs::answer::OllamaClient>()
                );

                fmt::print("\nConsultando modelo generativo\n");
                const auto answerRequest = makeAnswerRequest(argv[2], imagePaths, false);
                const auto answer = answerModule.answer(answerRequest);
                printAnswerDebug(answer);
            } catch (const std::exception& error) {
                spdlog::error("Answer failed: {}", sanitizeUtf8(error.what()));
                spdlog::error("Start Ollama and set SEMANTIC_FS_LLM_MODEL to an installed model. For images, use a vision model.");
                return 2;
            }

            return 0;
        }

        const auto document = orchestrator.indexPath(argv[1]);
        fmt::print("Document created\n");
        printDocumentDebug(document);

        if (argc >= 3) {
            try {
                const semantic_fs::answer::AnswerModule answerModule(
                    semantic_fs::answer::RagQueryService(orchestrator.ragModule()),
                    semantic_fs::answer::ContextRanker {},
                    semantic_fs::answer::PromptBuilder {},
                    std::make_shared<semantic_fs::answer::OllamaClient>()
                );

                std::vector<std::filesystem::path> imagePaths;
                if (isImagePath(document.path())) {
                    imagePaths.push_back(document.path());
                }

                const auto answerRequest = makeAnswerRequest(argv[2], imagePaths, true);
                const auto answer = answerModule.answer(answerRequest);
                printAnswerDebug(answer);
            } catch (const std::exception& error) {
                spdlog::error("Answer failed: {}", sanitizeUtf8(error.what()));
                spdlog::error("Start Ollama and set SEMANTIC_FS_LLM_MODEL to an installed model. For images, use a vision model.");
                return 2;
            }
        }

        return 0;
    } catch (const std::exception& error) {
        // Cualquier fallo de lectura, metadata o extractor termina aca con un
        // mensaje claro para debug.
        spdlog::error("Indexing failed: {}", sanitizeUtf8(error.what()));
        return 1;
    }
}
