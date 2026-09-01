#pragma once
#include "semantic_fs/monitoring/i_file_system_view.h"
#include "semantic_fs/monitoring/i_path_semantics.h"
namespace semantic_fs::monitoring {
class PathPolicy { public: explicit PathPolicy(const IPathSemantics& paths) : paths_(paths) {} bool owns(const AbsolutePath& root, const AbsolutePath& candidate) const; bool rootsOverlap(const AbsolutePath& left, const AbsolutePath& right) const; bool acceptsRootSet(const std::vector<AbsolutePath>& roots) const; bool admits(FileSystemEntryKind kind, const RelativePath& path, std::optional<std::uint64_t> size, const PolicyFilterConfig& config) const; private: bool startsWith(const std::vector<std::string>& value, const std::vector<std::string>& prefix) const; const IPathSemantics& paths_; };
} // namespace semantic_fs::monitoring
