// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Internal value types shared by projection implementation units. These types
// own no game or rendering resources and contain no behavior.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <tuple>

#include "mc/world/level/BlockPos.h"

class Block;
class BlockActor;

namespace lholo::projection::detail {

using SubChunkKey = std::tuple<int, int, int>;

enum class CorrectionState : std::uint8_t { Unknown, Missing, Correct, WrongType, WrongState };
enum class RenderBucket : std::uint8_t { Opaque, Alpha, AlphaOneSided, Blend, Count };

struct ProjectedBlockActor {
    BlockPos     position{};
    Block const* block{};
    BlockActor*  actor{};
    std::size_t  structureIndex{};
};

using ExpectedBlockMap      = std::map<SubChunkKey, Block const*>;
using ExpectedBlockActorMap = std::map<SubChunkKey, std::shared_ptr<BlockActor>>;
using ExpectedBlockIndexMap = std::map<SubChunkKey, std::size_t>;

} // namespace lholo::projection::detail
