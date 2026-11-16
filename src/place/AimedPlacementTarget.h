#pragma once

#include "mc/world/level/BlockPos.h"

#include <memory>
#include <optional>

class Block;
class CompoundTag;
class LocalPlayer;
class Vec3;

namespace lholo::place {

struct AimedPlacementTarget {
    BlockPos cell;
    Block const* block;
    std::shared_ptr<CompoundTag const> blockEntityNbt;
    bool hasLiquid{};
};

std::optional<AimedPlacementTarget> findAimedPlacementTarget(
    LocalPlayer& player, Vec3 const& origin, Vec3 const& direction, float reach, bool localPlacement = false);
std::optional<AimedPlacementTarget> findAimedPlacementTarget(LocalPlayer& player, bool localPlacement = false);

} // namespace lholo::place
