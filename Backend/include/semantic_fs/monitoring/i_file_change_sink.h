#pragma once
#include "semantic_fs/monitoring/monitoring_types.h"
namespace semantic_fs::monitoring { class IFileChangeSink { public: virtual ~IFileChangeSink() = default; virtual PublishResult publish(const FileChange& change) = 0; }; }
