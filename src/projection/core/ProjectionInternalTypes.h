// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Internal value types shared by projection implementation units. These types
// own no game or rendering resources and contain no behavior.

#pragma once

#include "projection/correction/CorrectionState.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <unordered_map>

#include "mc/world/level/BlockPos.h"

class Block;
class BlockActor;

namespace lholo::projection::detail {

using SubChunkKey = std::tuple<int, int, int>;

struct SubChunkKeyHash {
    [[nodiscard]] std::size_t operator()(SubChunkKey const& key) const noexcept {
        auto const [x, y, z] = key;
        std::uint64_t h = static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) * 73856093ULL;
        h ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) * 19349663ULL;
        h ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(z)) * 83492791ULL;
        h ^= h >> 16;
        h *= 0x85ebca6bULL;
        h ^= h >> 13;
        h *= 0xc2b2ae35ULL;
        h ^= h >> 16;
        return static_cast<std::size_t>(h);
    }
};

enum class RenderBucket : std::uint8_t { Opaque, Alpha, AlphaOneSided, Blend, Count };

struct ProjectedBlockActor {
    BlockPos     position{};
    Block const* block{};
    BlockActor*  actor{};
    std::size_t  structureIndex{};
};

using ExpectedBlockActorMap = std::unordered_map<SubChunkKey, std::shared_ptr<BlockActor>, SubChunkKeyHash>;

} // namespace lholo::projection::detail
