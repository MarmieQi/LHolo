#pragma once
#include "structure/write/StructureWriteTypes.h"
#include <optional>
namespace lholo::structure::write {
bool installLocalWorldWriter();
void uninstallLocalWorldWriter();
bool localWorldAvailable();
bool startLocalWrite(StructureWritePlan, std::uint64_t worldRevision);
WriteProgress localWriteStatus();
void resumeLocalWrite();
std::optional<WriteCompletion> takeLocalWriteResult();
void cancelLocalWrite();
}
