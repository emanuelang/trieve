#pragma once
#include "semantic_fs/monitoring/monitoring_types.h"
#include <memory>
namespace semantic_fs::monitoring {
class IPathSemantics { public: virtual ~IPathSemantics() = default; virtual std::optional<AbsolutePath> normalizeAbsolute(std::string_view value) const = 0; virtual int compareComponent(std::string_view left, std::string_view right) const = 0; virtual std::optional<RelativePath> relativeTo(const AbsolutePath& root, const AbsolutePath& candidate) const = 0; };
#ifdef _WIN32
std::unique_ptr<IPathSemantics> makeWindowsPathSemantics();
#endif
} // namespace semantic_fs::monitoring