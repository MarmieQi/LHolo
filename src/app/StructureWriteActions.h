#pragma once
#include "structure/write/StructureWriteTypes.h"
#include "structure/write/command/CommandWriteSession.h"
#include <cstdint>
#include <optional>
#include <string>
namespace lholo::app {
struct StructureWriteIntent {
    std::uint64_t id{}, revision{}, worldRevision{};
    std::string taskId, fileId;
    structure::write::WriteMode mode{structure::write::WriteMode::LocalWorld};
    bool skipAir{};
    bool skipPlaced{};
    enum class Action { Write, Resume, Cancel } action{Action::Write};
};
struct StructureWriteStatus {
    std::uint64_t ack{}, worldRevision{};
    bool busy{}, localAvailable{};
    std::optional<int> dimension;
    structure::write::WriteResult result{};
    std::string detail;
    bool commandAvailable{};
    structure::write::WriteMode mode{structure::write::WriteMode::LocalWorld};
    structure::write::command::Status command;
    std::string targetName;
    bool skipAir{};
    bool skipPlaced{};
    // Accepted operation identity and monotonic terminal time, independent of control/validation receipts.
    std::uint64_t operation{}, finishedAt{};
    bool operator==(StructureWriteStatus const&) const = default;
};
bool submitStructureWrite(StructureWriteIntent);
StructureWriteStatus structureWriteStatus();
void processStructureWriteActions();
void resetStructureWriteActions();
}
