#pragma once
#include <memory>

class BlockPos;
class BlockSource;
class StructureTemplate;

namespace lholo::structure::capture {
std::unique_ptr<StructureTemplate> captureBedrockStructure(
    BlockSource& region, BlockPos const& minimum, BlockPos const& size, bool includeEntities);
}
