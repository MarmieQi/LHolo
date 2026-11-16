#include "place/AimedPlacementTarget.h"
#include "projection/Projection.h"

#include "mc/client/player/LocalPlayer.h"
#include "mc/util/BlockUtils.h"
#include "mc/world/Facing.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/material/Material.h"

#include <cmath>
#include <limits>

namespace lholo::place {

// Voxel raycast (Amanatides & Woo) against the real world plus the projection's
// virtual grid. The vanilla crosshair hit result never sees LHolo's drawn ghost
// blocks, so the ray is traced manually: real blocks block it, projected
// missing cells are the placement targets.
std::optional<AimedPlacementTarget> findAimedPlacementTarget(
    LocalPlayer& player,
    Vec3 const&  origin,
    Vec3 const&  dir,
    float        maxDist,
    bool         localPlacement
) {
    auto& region = player.getDimensionBlockSource();

    int const stepX = dir.x > 0.0f ? 1 : -1;
    int const stepY = dir.y > 0.0f ? 1 : -1;
    int const stepZ = dir.z > 0.0f ? 1 : -1;
    float const tDeltaX = dir.x != 0.0f ? std::abs(1.0f / dir.x) : std::numeric_limits<float>::infinity();
    float const tDeltaY = dir.y != 0.0f ? std::abs(1.0f / dir.y) : std::numeric_limits<float>::infinity();
    float const tDeltaZ = dir.z != 0.0f ? std::abs(1.0f / dir.z) : std::numeric_limits<float>::infinity();

    int x = static_cast<int>(std::floor(origin.x));
    int y = static_cast<int>(std::floor(origin.y));
    int z = static_cast<int>(std::floor(origin.z));
    BlockPos const originCell{x, y, z};
    auto const firstBoundary = [](float position, int cell, int step, float interval) {
        if (std::isinf(interval)) return interval;
        float const edge = static_cast<float>(cell);
        return (step > 0 ? edge + 1.0f - position : position - edge) * interval;
    };
    float tMaxX = firstBoundary(origin.x, x, stepX, tDeltaX);
    float tMaxY = firstBoundary(origin.y, y, stepY, tDeltaY);
    float tMaxZ = firstBoundary(origin.z, z, stepZ, tDeltaZ);

    for (int step = 0; step < 512; ++step) {
        float tEnter;
        uchar entryFace;
        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            x += stepX;
            tEnter = tMaxX;
            tMaxX += tDeltaX;
            entryFace = stepX > 0 ? static_cast<uchar>(Facing::Name::West) : static_cast<uchar>(Facing::Name::East);
        } else if (tMaxY < tMaxZ) {
            y += stepY;
            tEnter = tMaxY;
            tMaxY += tDeltaY;
            entryFace = stepY > 0 ? static_cast<uchar>(Facing::Name::Down) : static_cast<uchar>(Facing::Name::Up);
        } else {
            z += stepZ;
            tEnter = tMaxZ;
            tMaxZ += tDeltaZ;
            entryFace = stepZ > 0 ? static_cast<uchar>(Facing::Name::North) : static_cast<uchar>(Facing::Name::South);
        }
        if (tEnter > maxDist) break;

        BlockPos const cell{x, y, z};
        auto const& actual = region.getBlock(cell);
        if (!actual.isAir() && !(localPlacement && actual.getBlockType().mMaterial->mLiquid
                                 && !BlockUtils::isLiquidSource(actual))) {
            // A real block blocks the ray. Placing into the camera-side cell
            // (the vanilla placement position) fills an adjacent ghost. Never
            // target the cell the camera itself is standing in.
            BlockPos const neighbor = cell + Facing::DIRECTION()[entryFace];
            if (localPlacement) {
                auto const hit = projection::queryProjection(player, cell, true);
                if (hit.block && hit.missing) return AimedPlacementTarget{cell, hit.block, hit.hasLiquid};
            }
            auto const query = projection::queryProjection(player, neighbor, localPlacement);
            if (neighbor != originCell && query.block && query.missing) {
                return AimedPlacementTarget{neighbor, query.block, query.hasLiquid};
            }
            break;
        }
        auto const query = projection::queryProjection(player, cell, localPlacement);
        if (!query.block || !query.missing) continue;

        return AimedPlacementTarget{cell, query.block, query.hasLiquid};
    }
    return std::nullopt;
}

std::optional<AimedPlacementTarget> findAimedPlacementTarget(LocalPlayer& player, bool localPlacement) {
    auto const origin = player.getEyePos();
    auto const view = player.getViewVector(1.0f);
    float const length = std::sqrt(view.x * view.x + view.y * view.y + view.z * view.z);
    if (length <= 0.0f) return std::nullopt;
    return findAimedPlacementTarget(player, origin, view / length, player.getPickRange(), localPlacement);
}

} // namespace lholo::place
