#include "semantic_fs/monitoring/path_policy.h"

#include <catch2/catch_test_macros.hpp>

using namespace semantic_fs::monitoring;

namespace {
class LexicalSemantics final : public IPathSemantics {
public:
    std::optional<AbsolutePath> normalizeAbsolute(std::string_view value) const override {
        if (value.empty() || value.front() != '/') return std::nullopt;
        std::vector<std::string> parts;
        std::string part;
        for (const char ch : value) {
            if (ch == '/') { if (part == "..") { if (parts.empty()) return std::nullopt; parts.pop_back(); } else if (!part.empty() && part != ".") parts.push_back(part); part.clear(); }
            else part += ch;
        }
        if (part == "..") { if (parts.empty()) return std::nullopt; parts.pop_back(); } else if (!part.empty() && part != ".") parts.push_back(part);
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
        std::vector<std::string> relative(candidates.begin() + static_cast<std::ptrdiff_t>(roots.size()), candidates.end());
        std::string utf8; for (const auto& item : relative) utf8 += (utf8.empty() ? "" : "/") + item;
        return RelativePath{utf8, relative};
    }
};
}

TEST_CASE("path_policy: owns normalized component boundaries and rejects escaping candidates")
{
    LexicalSemantics paths;
    PathPolicy policy(paths);
    const AbsolutePath root{"/scan/root"};
    REQUIRE(policy.owns(root, {"/scan/root/./sub/../file.txt"}));
    REQUIRE_FALSE(policy.owns(root, {"/scan/root2/file.txt"}));
    REQUIRE_FALSE(policy.owns(root, {"/scan/root/../../outside.txt"}));
    REQUIRE(policy.owns(root, root));
    REQUIRE(policy.rootsOverlap(root, {"/scan/root/sub"}));
    REQUIRE_FALSE(policy.rootsOverlap(root, {"/scan/root2"}));
    REQUIRE(paths.compareComponent("\xC3\x84", "\xC3\xA4") != 0);
}

TEST_CASE("path_policy: defaults are neutral and filters by normalized components")
{
    LexicalSemantics paths;
    PathPolicy policy(paths);
    const RelativePath visible{"folder/report.txt", {"folder", "report.txt"}};
    const RelativePath hidden{".private", {".private"}};
    REQUIRE(policy.admits(FileSystemEntryKind::RegularFile, visible, 10, {}));
    REQUIRE(policy.admits(FileSystemEntryKind::RegularFile, hidden, std::nullopt, {}));
    REQUIRE_FALSE(policy.admits(FileSystemEntryKind::Directory, visible, 10, {}));
    REQUIRE_FALSE(policy.admits(FileSystemEntryKind::LinkOrReparse, visible, 10, {}));
    REQUIRE(policy.admits(FileSystemEntryKind::RegularFile, {"limit.txt", {"limit.txt"}}, 10, PolicyFilterConfig{{}, {}, 10}));
    const PolicyFilterConfig excludedTemp{{}, {RelativePath{"temp", {"temp"}}}, {}};
    REQUIRE_FALSE(policy.admits(FileSystemEntryKind::RegularFile, {"temp/report.txt", {"temp", "report.txt"}}, 10, excludedTemp));
    REQUIRE(policy.admits(FileSystemEntryKind::RegularFile, {"template/report.txt", {"template", "report.txt"}}, 10, excludedTemp));
    REQUIRE(policy.admits(FileSystemEntryKind::RegularFile, {"report.txt", {"report.txt"}}, 10, PolicyFilterConfig{{".txt"}, {}, {}}));
    PolicyFilterConfig configured{{".txt"}, {RelativePath{"folder", {"folder"}}}, 10};
    REQUIRE_FALSE(policy.admits(FileSystemEntryKind::RegularFile, visible, 10, configured));
    REQUIRE_FALSE(policy.admits(FileSystemEntryKind::RegularFile, {"report.jpg", {"report.jpg"}}, 10, PolicyFilterConfig{{".txt"}, {}, {}}));
    REQUIRE_FALSE(policy.admits(FileSystemEntryKind::RegularFile, {"big.txt", {"big.txt"}}, 11, PolicyFilterConfig{{}, {}, 10}));
}