// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/world/ProjectionQueries.h"
#include "block/BlockPlacementRules.h"

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/core/ProjectionState.h"

#include <algorithm>
#include <cmath>
#include <tuple>

#include "mc/util/BlockUtils.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/material/Material.h"

namespace lholo::projection {

float RangeCandidateOrder::distanceSquared(int x, int y, int z) const {
    float const dx = static_cast<float>(x) + 0.5f - center.x;
    float const dy = static_cast<float>(y) + 0.5f - center.y;
    float const dz = static_cast<float>(z) + 0.5f - center.z;
    return dx * dx + dy * dy + dz * dz;
}

bool RangeCandidateOrder::operator()(RangeCandidate const& a, RangeCandidate const& b) const {
    return std::tuple{distanceSquared(a.x, a.y, a.z), a.x, a.y, a.z}
        < std::tuple{distanceSquared(b.x, b.y, b.z), b.x, b.y, b.z};
}

namespace detail {
namespace {

Block const* placementBlock(Block const* body, Block const* liquid, bool localPlacement) {
    auto const* block = body ? body : liquid;
    if (block && block->getBlockType().mMaterial->mLiquid
        && (!localPlacement || !BlockUtils::isLiquidSource(*block))) return nullptr;
    return block;
}

} // namespace

ProjectionQuery queryProjectionCell(
    ProjectionState const& state,
    BlockPos const&        worldPosition,
    bool                   localPlacement
) {
    auto const key = std::tuple{worldPosition.x, worldPosition.y, worldPosition.z};
    auto const foundIndex = state.worldLookup->indexAt(key);
    if (!foundIndex) return {nullptr, false};
    auto const cell = state.worldLookup->cellAt(key);
    if (!cell) return {};
    auto const* block = placementBlock(cell->block, cell->liquid, localPlacement);
    if (!block) return {};
    auto const correction = state.renderCorrectionState(*foundIndex);
    bool const needsPlacement = correction == CorrectionState::Missing || (localPlacement
        && (correction == CorrectionState::WrongType || correction == CorrectionState::WrongState)
        && block::isDoubleSlab(*block));
    return {block, needsPlacement,
        cell->block && cell->liquid && cell->block != cell->liquid};
}

std::vector<RangeCandidate> queryMissingProjectionCells(
    ProjectionState const& state,
    Vec3 const&            center,
    float                  radius,
    bool                   localPlacement
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
    RangeCandidateOrder const order{center};
    auto const append = [&](int x, int y, int z, CorrectionState correction) {
        if (correction != CorrectionState::Missing
            && (!localPlacement || (correction != CorrectionState::WrongType && correction != CorrectionState::WrongState)))
            return;
        if (order.distanceSquared(x, y, z) > r2) return;
        auto const cell = state.worldLookup->cellAt({x, y, z});
        if (!cell) return;
        auto const* block = placementBlock(cell->block, cell->liquid, localPlacement);
        if (!block) return;
        if (correction != CorrectionState::Missing && !block::isDoubleSlab(*block)) return;
        result.push_back({x, y, z, block, cell->block && cell->liquid && cell->block != cell->liquid});
    };
    if (r <= 4) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                for (int x = minX; x <= maxX; ++x) {
                    auto const index = state.worldLookup->indexAt({x, y, z});
                    if (index) append(x, y, z, state.renderCorrectionState(*index));
                }
            }
        }
    } else {
        auto const& structure = *state.structure;
        std::array<long long, 3> const origin{
            static_cast<long long>(state.anchor.x) + state.cachedOffsetX,
            static_cast<long long>(state.anchor.y) + state.cachedOffsetY,
            static_cast<long long>(state.anchor.z) + state.cachedOffsetZ};
        for (std::size_t section = 0; section < state.localSectionKeys.size(); ++section) {
            auto const bounds = projectionSectionCellWorldBounds(
                state.localSectionKeys[section], structure, origin, state.cachedMirror, state.cachedRotation);
            if (bounds.maximum[0] <= minX || bounds.minimum[0] > maxX
                || bounds.maximum[1] <= minY || bounds.minimum[1] > maxY
                || bounds.maximum[2] <= minZ || bounds.minimum[2] > maxZ) continue;
            for (auto const index : state.sectionBlockIndices[section]) {
                auto const correction = state.renderCorrectionState(index);
                if (correction == CorrectionState::Unknown || correction == CorrectionState::Correct) continue;
                auto const cell = transformStructurePosition(
                    structure.renderBlocks[index], structure, state.cachedMirror, state.cachedRotation);
                append(static_cast<int>(origin[0] + cell.x), static_cast<int>(origin[1] + cell.y),
                       static_cast<int>(origin[2] + cell.z), correction);
            }
        }
    }
    std::sort(result.begin(), result.end(), order);
    return result;
}

} // namespace detail
} // namespace lholo::projection
