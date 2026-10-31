#pragma once
#include "structure/write/StructureWriteTypes.h"
#include <memory>
class Level;
class StructureTemplate;
namespace lholo::structure::write {
std::unique_ptr<StructureTemplate> buildBedrockTemplate(StructureWritePlan const&, Level&);
}
