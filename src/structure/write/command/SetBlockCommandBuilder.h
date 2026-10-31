#pragma once
#include "structure/write/command/CommandWriteSession.h"
#include "structure/write/StructureWriteTypes.h"
namespace lholo::structure::write::command {
Plan makeCommandPlan(StructureWritePlan const&);
}
