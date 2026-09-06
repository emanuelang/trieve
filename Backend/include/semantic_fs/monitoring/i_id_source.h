#pragma once

#include <string>

namespace semantic_fs::monitoring {
class IIdSource {
public:
    virtual ~IIdSource() = default;
    virtual std::string nextObservationId() = 0;
    virtual std::string nextEventId() = 0;
};
} // namespace semantic_fs::monitoring
