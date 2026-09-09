// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/world/ProjectionQueries.h"

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/core/ProjectionState.h"

#include <algorithm>
#include <cmath>
#include <tuple>

#include "mc/world/level/block/Block.h"
#include "mc/world/level/material/Material.h"

namespace lholo::projection::detail {

ProjectionQuery queryProjectionCell(
    ProjectionState const& state,
    BlockPos const&        worldPosition
) {
    auto const key = std::tuple{worldPosition.x, worldPosition.y, worldPosition.z};
    auto const foundIndex = state.expectedWorldBlockIndices->find(key);
    if (foundIndex == state.expectedWorldBlockIndices->end()) return {nullptr, false};
    auto const foundBlock = state.expectedWorldBlocks->find(key);
    Block const* block = foundBlock == state.expectedWorldBlocks->end()
        ? nullptr
        : foundBlock->second;
    // Liquids have no normal block item, so they are never a valid place target.
    if (block && block->getMaterial().isLiquid()) block = nullptr;
    bool const missing = state.correctionStates[foundIndex->second] == CorrectionState::Missing;
    return {block, missing};
}

std::vector<RangeCandidate> queryMissingProjectionCells(
    ProjectionState const& state,
    Vec3 const&            center,
    float                  radius
) {
    std::vector<RangeCandidate> result;
    // Only visit cells in the axis-aligned box around the center: with a small
    // radius this is far cheaper than walking the whole virtual-world map.
    int const r = static_cast<int>(std::ceil(radius));
    int const minX = static_cast<int>(std::floor(center.x)) - r;
    int const maxX = static_cast<int>(std::floor(center.x)) + r;
    int const minY = static_cast<int>(std::floor(center.y)) - r;
    int const maxY = static_cast<int>(std::floor(center.y)) + r;
    int const minZ = static_cast<int>(std::floor(center.z)) - r;
    int const maxZ = static_cast<int>(std::floor(center.z)) + r;
    float const r2 = radius * radius;
    for (int y = minY; y <= maxY; ++y) {
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                auto const key = std::tuple{x, y, z};
                auto const foundIndex = state.expectedWorldBlockIndices->find(key);
                if (foundIndex == state.expectedWorldBlockIndices->end()) continue;
                if (state.correctionStates[foundIndex->second] != CorrectionState::Missing) continue;
                float const dx = static_cast<float>(x) + 0.5f - center.x;
                float const dy = static_cast<float>(y) + 0.5f - center.y;
                float const dz = static_cast<float>(z) + 0.5f - center.z;
                if (dx * dx + dy * dy + dz * dz > r2) continue;
                auto const foundBlock = state.expectedWorldBlocks->find(key);
                Block const* block = foundBlock == state.expectedWorldBlocks->end()
                    ? nullptr
                    : foundBlock->second;
                // Liquids have no normal block item, so they are never a valid place target.
                if (block && block->getMaterial().isLiquid()) block = nullptr;
                if (!block) continue;
                result.push_back({x, y, z, block});
            }
        }
    }
    std::sort(result.begin(), result.end(), [&center](RangeCandidate const& a, RangeCandidate const& b) {
        auto const distSq = [&center](RangeCandidate const& candidate) {
            float const dx = static_cast<float>(candidate.x) + 0.5f - center.x;
            float const dy = static_cast<float>(candidate.y) + 0.5f - center.y;
            float const dz = static_cast<float>(candidate.z) + 0.5f - center.z;
            return dx * dx + dy * dy + dz * dz;
        };
        return distSq(a) < distSq(b);
    });
    return result;
}

} // namespace lholo::projection::detail
