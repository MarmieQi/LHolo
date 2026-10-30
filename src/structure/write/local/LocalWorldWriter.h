#pragma once
#include "structure/write/StructureWriteTypes.h"
#include <optional>
namespace lholo::structure::write {
bool installLocalWorldWriter();
void uninstallLocalWorldWriter();
bool localWorldAvailable();
bool queueLocalWrite(StructureWritePlan, std::uint64_t worldRevision);
std::optional<WriteCompletion> takeLocalWriteResult();
void cancelLocalWrite();
}
