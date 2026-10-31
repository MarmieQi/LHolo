#pragma once
#include "structure/write/command/CommandWriteSession.h"
#include "structure/write/StructureWriteTypes.h"
namespace lholo::structure::write::command {
bool installCommandWorldWriter();
void uninstallCommandWorldWriter();
bool commandWorldAvailable();
void startCommandWrite(StructureWritePlan const&, std::uint64_t worldRevision, bool skipPlaced);
Status commandWriteStatus();
void resumeCommandWrite();
void cancelCommandWrite();
}
