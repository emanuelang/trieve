#include "semantic_fs/answer/ollama_client.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace semantic_fs::answer {
namespace {

std::string envOrDefault(const char* name, std::string fallback)
{
    if (const auto* value = std::getenv(name); value != nullptr && value[0] != '\0') {
        return value;
    }
    return fallback;
}

int envIntOrDefault(const char* name, int fallback)
{
    if (const auto* value = std::getenv(name); value != nullptr && value[0] != '\0') {
        try {
            return std::stoi(value);
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

bool envBoolOrDefault(const char* name, bool fallback)
{
    if (const auto* value = std::getenv(name); value != nullptr && value[0] != '\0') {
        const std::string text(value);
        return text != "0" && text != "false" && text != "FALSE";
    }
    return fallback;
}

std::string readBinaryFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not read image file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string base64Encode(const std::string& bytes)
{
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string encoded;
    encoded.reserve(((bytes.size() + 2) / 3) * 4);

    std::size_t index = 0;
    while (index + 3 <= bytes.size()) {
        const auto first = static_cast<unsigned char>(bytes[index++]);
        const auto second = static_cast<unsigned char>(bytes[index++]);
        const auto third = static_cast<unsigned char>(bytes[index++]);

        encoded.push_back(alphabet[first >> 2]);
        encoded.push_back(alphabet[((first & 0x03U) << 4U) | (second >> 4U)]);
        encoded.push_back(alphabet[((second & 0x0FU) << 2U) | (third >> 6U)]);
        encoded.push_back(alphabet[third & 0x3FU]);
    }

    const auto remaining = bytes.size() - index;
    if (remaining == 1) {
        const auto first = static_cast<unsigned char>(bytes[index]);
        encoded.push_back(alphabet[first >> 2]);
        encoded.push_back(alphabet[(first & 0x03U) << 4U]);
        encoded.push_back('=');
        encoded.push_back('=');
    } else if (remaining == 2) {
        const auto first = static_cast<unsigned char>(bytes[index]);
        const auto second = static_cast<unsigned char>(bytes[index + 1]);
        encoded.push_back(alphabet[first >> 2]);
        encoded.push_back(alphabet[((first & 0x03U) << 4U) | (second >> 4U)]);
        encoded.push_back(alphabet[(second & 0x0FU) << 2U]);
        encoded.push_back('=');
    }

    return encoded;
}

std::size_t appendResponse(char* data, std::size_t size, std::size_t count, void* userData)
{
    auto* response = static_cast<std::string*>(userData);
    response->append(data, size * count);
    return size * count;
}

std::string buildUrl(const std::string& baseUrl)
{
    if (!baseUrl.empty() && baseUrl.back() == '/') {
        return baseUrl + "api/generate";
    }
    return baseUrl + "/api/generate";
}

std::string buildChatUrl(const std::string& baseUrl)
{
    if (!baseUrl.empty() && baseUrl.back() == '/') {
        return baseUrl + "api/chat";
    }
    return baseUrl + "/api/chat";
}

std::string buildTagsUrl(const std::string& baseUrl)
{
    if (!baseUrl.empty() && baseUrl.back() == '/') {
        return baseUrl + "api/tags";
    }
    return baseUrl + "/api/tags";
}

bool httpGetOk(const std::string& url, int timeoutSeconds)
{
    std::string responseBody;
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendResponse);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);

    const auto result = curl_easy_perform(curl);
    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    curl_easy_cleanup(curl);

    return result == CURLE_OK && statusCode >= 200 && statusCode < 300;
}

#ifdef _WIN32
std::wstring widen(const std::string& text)
{
    if (text.empty()) {
        return {};
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 0) {
        return {};
    }

    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), size);
    if (!wide.empty() && wide.back() == L'\0') {
        wide.pop_back();
    }
    return wide;
}

std::vector<std::filesystem::path> ollamaExecutableCandidates()
{
    std::vector<std::filesystem::path> candidates;

    if (const auto* configured = std::getenv("SEMANTIC_FS_OLLAMA_EXE");
        configured != nullptr && configured[0] != '\0') {
        candidates.emplace_back(configured);
    }

    if (const auto* localAppData = std::getenv("LOCALAPPDATA");
        localAppData != nullptr && localAppData[0] != '\0') {
        candidates.emplace_back(std::filesystem::path(localAppData) / "Programs" / "Ollama" / "ollama.exe");
    }

    candidates.emplace_back("ollama.exe");
    return candidates;
}

bool startOllamaServe()
{
    for (const auto& executable : ollamaExecutableCandidates()) {
        if (executable.has_parent_path() && !std::filesystem::exists(executable)) {
            continue;
        }

        auto command = L"\"" + widen(executable.string()) + L"\" serve";
        STARTUPINFOW startupInfo {};
        startupInfo.cb = sizeof(startupInfo);
        startupInfo.dwFlags = STARTF_USESHOWWINDOW;
        startupInfo.wShowWindow = SW_HIDE;

        PROCESS_INFORMATION processInfo {};
        const auto created = CreateProcessW(
            nullptr,
            command.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW | DETACHED_PROCESS,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo
        );

        if (created != FALSE) {
            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);
            return true;
        }
    }

    return false;
}
#else
bool startOllamaServe()
{
    return std::system("ollama serve >/dev/null 2>&1 &") == 0;
}
#endif

void ensureOllamaServer(const OllamaClientOptions& options)
{
    const auto tagsUrl = buildTagsUrl(options.baseUrl);
    if (httpGetOk(tagsUrl, 2)) {
        return;
    }

    if (!options.autoStart || !startOllamaServe()) {
        throw std::runtime_error("Ollama server is not running and could not be started");
    }

    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(options.startupWaitSeconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (httpGetOk(tagsUrl, 2)) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    throw std::runtime_error("Ollama server did not become ready before timeout");
}

} // namespace

OllamaClient::OllamaClient()
    : options_({
          .baseUrl = envOrDefault("SEMANTIC_FS_OLLAMA_URL", "http://localhost:11434"),
          .modelName = envOrDefault("SEMANTIC_FS_LLM_MODEL", "qwen2.5vl:3b"),
          .timeoutSeconds = envIntOrDefault("SEMANTIC_FS_OLLAMA_TIMEOUT", 300),
          .numContext = envIntOrDefault("SEMANTIC_FS_NUM_CTX", 1024),
          .numPredict = envIntOrDefault("SEMANTIC_FS_NUM_PREDICT", 64),
          .temperature = 0.2,
          .keepAlive = envOrDefault("SEMANTIC_FS_KEEP_ALIVE", "0"),
          .autoStart = envBoolOrDefault("SEMANTIC_FS_OLLAMA_AUTO_START", true),
          .startupWaitSeconds = envIntOrDefault("SEMANTIC_FS_OLLAMA_STARTUP_WAIT", 30)
      })
{
}

OllamaClient::OllamaClient(OllamaClientOptions options)
    : options_(std::move(options))
{
}

LlmResponse OllamaClient::generate(const PromptRequest& request) const
{
    ensureOllamaServer(options_);

    nlohmann::json payload;
    std::string url;

    if (request.imagePaths.empty()) {
        url = buildUrl(options_.baseUrl);
        payload = {
            {"model", options_.modelName},
            {"prompt", request.prompt},
            {"stream", false},
            {"keep_alive", options_.keepAlive},
            {"options", {
                {"num_ctx", options_.numContext},
                {"num_predict", options_.numPredict},
                {"temperature", options_.temperature}
            }}
        };
    } else {
        url = buildChatUrl(options_.baseUrl);
        nlohmann::json userMessage = {
            {"role", "user"},
            {"content", request.prompt},
            {"images", nlohmann::json::array()}
        };

        for (const auto& imagePath : request.imagePaths) {
            userMessage["images"].push_back(base64Encode(readBinaryFile(imagePath)));
        }

        payload = {
            {"model", options_.modelName},
            {"messages", nlohmann::json::array({userMessage})},
            {"stream", false},
            {"keep_alive", options_.keepAlive},
            {"options", {
                {"num_ctx", options_.numContext},
                {"num_predict", options_.numPredict},
                {"temperature", options_.temperature}
            }}
        };
    }

    const auto body = payload.dump();
    std::string responseBody;

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        throw std::runtime_error("Could not initialize curl for Ollama request");
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendResponse);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, options_.timeoutSeconds);

    const auto result = curl_easy_perform(curl);
    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        throw std::runtime_error(std::string("Ollama request failed: ") + curl_easy_strerror(result));
    }

    nlohmann::json response;
    try {
        response = nlohmann::json::parse(responseBody);
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string("Ollama returned invalid JSON: ") + error.what());
    }

    if (statusCode < 200 || statusCode >= 300) {
        const auto message = response.value("error", responseBody);
        throw std::runtime_error("Ollama returned HTTP " + std::to_string(statusCode) + ": " + message);
    }

    return {
        .text = request.imagePaths.empty()
            ? response.value("response", "")
            : response.value("message", nlohmann::json::object()).value("content", ""),
        .modelName = response.value("model", options_.modelName),
        .usedFallback = false
    };
}

} // namespace semantic_fs::answer
