#pragma once
#include "structure/write/StructureWriteTypes.h"
#include <cstdint>
#include <string>
namespace lholo::app {
struct StructureWriteIntent {
    std::uint64_t id{}, revision{}, worldRevision{};
    std::string taskId, fileId;
    structure::write::WriteMode mode{structure::write::WriteMode::LocalWorld};
    bool skipAir{};
};
struct StructureWriteStatus {
    std::uint64_t ack{}, worldRevision{};
    bool busy{}, localAvailable{};
    structure::write::WriteResult result{};
    std::string detail;
    bool operator==(StructureWriteStatus const&) const = default;
};
bool submitStructureWrite(StructureWriteIntent);
StructureWriteStatus structureWriteStatus();
void processStructureWriteActions();
void resetStructureWriteActions();
}
