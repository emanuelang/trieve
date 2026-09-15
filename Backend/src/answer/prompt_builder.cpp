#include "semantic_fs/answer/prompt_builder.h"

#include <fmt/format.h>

#include <cctype>
#include <filesystem>
#include <sstream>
#include <string>

namespace semantic_fs::answer {
namespace {

std::string pathToUtf8String(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::string lowerAscii(std::string text)
{
    for (auto& character : text) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return text;
}

bool asksForImageSearch(const std::string& question)
{
    const auto lower = lowerAscii(question);
    const auto mentionsImage = lower.find("imagen") != std::string::npos ||
        lower.find("foto") != std::string::npos ||
        lower.find("archivo") != std::string::npos;
    const auto asksToRetrieve = lower.find("trae") != std::string::npos ||
        lower.find("busca") != std::string::npos ||
        lower.find("encontra") != std::string::npos ||
        lower.find("encuentra") != std::string::npos ||
        lower.find("dame") != std::string::npos;

    return mentionsImage && asksToRetrieve;
}

} // namespace

PromptRequest PromptBuilder::build(
    const std::string& question,
    const std::vector<semantic_fs::rag::RetrievedChunk>& chunks,
    const std::vector<semantic_fs::knowledge::RetrievedKnowledge>& knowledge,
    const std::vector<std::filesystem::path>& imagePaths,
    const AnswerOptions& options
) const
{
    const auto imageSearch = asksForImageSearch(question);

    std::ostringstream prompt;
    prompt << "Sos el modulo de respuesta de un sistema RAG local.\n";
    prompt << "Responde en espanol usando solo el contexto recuperado y el contexto estructurado.\n";
    prompt << "No repitas la pregunta como respuesta: responde con datos concretos del contexto.\n";
    prompt << "El contexto estructurado tiene relaciones extraidas de documentos; usalo para ordenar procesos, reglas y responsables.\n";
    prompt << "Si una relacion no tiene evidencia suficiente, aclaralo.\n";
    if (imageSearch) {
        prompt << "El usuario pidio traer una imagen por tema. Elegi la fuente mas relevante y responde con este formato:\n";
        prompt << "Imagen encontrada: <nombre del archivo>\n";
        prompt << "Tema: <tema principal inferido del contexto>\n";
        prompt << "Resumen: <2 o 3 frases con detalles concretos del contexto recuperado>\n";
        prompt << "Direccion: <path local exacto>\n";
    } else {
        prompt << "El usuario hizo una pregunta sobre el contenido. Responde directamente esa pregunta.\n";
        prompt << "Formato requerido:\n";
        prompt << "Respuesta: <respuesta concreta basada en el contexto>\n";
        prompt << "Detalles: <lista breve de puntos importantes encontrados en el contexto>\n";
        prompt << "Fuente: <nombre del archivo y path si aparece en el contexto>\n";
    }
    prompt << "Si el contexto no alcanza para explicar el tema, decilo claramente.\n\n";
    prompt << "Pregunta:\n" << question << "\n\n";
    if (!imagePaths.empty()) {
        prompt << "Imagenes adjuntas por ruta local:\n";
        for (std::size_t index = 0; index < imagePaths.size(); ++index) {
            prompt << fmt::format("[imagen {}] {}\n", index + 1, pathToUtf8String(imagePaths[index]));
        }
        prompt << "Si la pregunta pide direccion, responde con la ruta local exacta de la imagen.\n\n";
        prompt << "Formato requerido: descripcion breve, tema inferido y direccion.\n\n";
    }

    prompt << "Contexto recuperado:\n";

    for (std::size_t index = 0; index < chunks.size(); ++index) {
        const auto& retrieved = chunks[index];
        const auto& chunk = retrieved.chunk;
        prompt << fmt::format(
            "[{}] archivo='{}' path='{}' source='{}' score={:.3f}\n{}\n\n",
            index + 1,
            chunk.fileName,
            pathToUtf8String(chunk.filePath),
            chunk.source,
            retrieved.score,
            chunk.text
        );
    }

    if (!knowledge.empty()) {
        prompt << "Contexto estructurado del mapa de conocimiento:\n";
        for (std::size_t index = 0; index < knowledge.size(); ++index) {
            const auto& item = knowledge[index];
            const auto evidence = item.edge.evidence.empty() ? semantic_fs::knowledge::KnowledgeEvidence {} : item.edge.evidence.front();
            prompt << fmt::format(
                "[K{}] {}({}) --{}--> {}({}) score={:.3f} confidence={:.2f} lang={}({:.2f})\nEvidencia: {}\n\n",
                index + 1,
                item.from.name,
                semantic_fs::knowledge::toString(item.from.type),
                semantic_fs::knowledge::toString(item.edge.type),
                item.to.name,
                semantic_fs::knowledge::toString(item.to.type),
                item.score,
                item.edge.confidence,
                evidence.detectedLanguage,
                evidence.languageConfidence,
                evidence.text
            );
        }
    }

    if (options.includeSources) {
        prompt << "Inclui fuentes si aportan valor usando el nombre del archivo.\n";
    }

    return {
        .prompt = prompt.str(),
        .chunks = chunks,
        .knowledge = knowledge,
        .imagePaths = imagePaths
    };
}

} // namespace semantic_fs::answer
