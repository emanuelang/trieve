#pragma once

#include "semantic_fs/monitoring/i_clock.h"
#include "semantic_fs/monitoring/i_file_change_sink.h"
#include "semantic_fs/monitoring/i_file_observation_sink.h"
#include "semantic_fs/monitoring/i_file_system_view.h"
#include "semantic_fs/monitoring/i_path_semantics.h"

#include <map>

namespace semantic_fs::monitoring::test {
class FixturePaths final : public IPathSemantics {
public:
    std::optional<AbsolutePath> normalizeAbsolute(std::string_view value) const override {
        if (value.empty() || value.front() != '/') return std::nullopt;
        std::vector<std::string> parts; std::string part;
        const auto append = [&] { if (part == "..") { if (parts.empty()) return false; parts.pop_back(); } else if (!part.empty() && part != ".") parts.push_back(part); part.clear(); return true; };
        for (const char ch : value) { if (ch == '/') { if (!append()) return std::nullopt; } else part += ch; }
        if (!append()) return std::nullopt;
        std::string normalized; for (const auto& item : parts) normalized += "/" + item;
        return AbsolutePath{normalized.empty() ? "/" : normalized};
    }
    int compareComponent(std::string_view left, std::string_view right) const override { return left == right ? 0 : left < right ? -1 : 1; }
    std::optional<RelativePath> relativeTo(const AbsolutePath& root, const AbsolutePath& candidate) const override {
        const auto normalizedRoot = normalizeAbsolute(root.utf8), normalizedCandidate = normalizeAbsolute(candidate.utf8);
        if (!normalizedRoot || !normalizedCandidate) return std::nullopt;
        const auto split = [](std::string_view value) { std::vector<std::string> result; std::string item; for (const char ch : value) { if (ch == '/') { if (!item.empty()) { result.push_back(item); item.clear(); } } else item += ch; } if (!item.empty()) result.push_back(item); return result; };
        const auto roots = split(normalizedRoot->utf8), candidates = split(normalizedCandidate->utf8);
        if (candidates.size() < roots.size()) return std::nullopt;
        for (std::size_t index = 0; index < roots.size(); ++index) if (compareComponent(roots[index], candidates[index]) != 0) return std::nullopt;
        std::vector<std::string> components(candidates.begin() + static_cast<std::ptrdiff_t>(roots.size()), candidates.end()); std::string utf8;
        for (const auto& item : components) utf8 += (utf8.empty() ? "" : "/") + item;
        return RelativePath{utf8, components};
    }
};
class FixtureView final : public IFileSystemView {
public:
    std::map<std::string, ListResult> listings; std::map<std::string, MetadataResult> metadataResults;
    ListResult list(const AbsolutePath& directory) const override { const auto it = listings.find(directory.utf8); return it == listings.end() ? ListResult{FsError{FsErrorCode::NotFound}} : it->second; }
    MetadataResult metadata(const AbsolutePath& path) const override { const auto it = metadataResults.find(path.utf8); return it == metadataResults.end() ? MetadataResult{FsError{FsErrorCode::NotFound}} : it->second; }
};
class FixtureClock final : public IClock { public: UtcTimestamp utcNow() const override { return {42}; } };
class FixtureSink final : public IFileObservationSink { public: std::vector<FileObservation> observations; ObservationDelivery next{ObservationDelivery::Accepted}; std::optional<std::size_t> stopAfterAccepted; ObservationDelivery observe(const FileObservation& observation) override { observations.push_back(observation); if (!stopAfterAccepted || observations.size() > *stopAfterAccepted) return next; return ObservationDelivery::Accepted; } };
class ForbiddenChangeSink final : public IFileChangeSink { public: unsigned calls{}; PublishResult publish(const FileChange&) override { ++calls; return PublishResult::Accepted; } };
} // namespace semantic_fs::monitoring::test
