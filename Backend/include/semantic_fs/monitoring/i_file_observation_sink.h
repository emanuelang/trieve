#pragma once
#include "semantic_fs/monitoring/monitoring_types.h"
namespace semantic_fs::monitoring { class IFileObservationSink { public: virtual ~IFileObservationSink() = default; virtual ObservationDelivery observe(const FileObservation& observation) = 0; }; }
