#pragma once

#include "projection/core/ProjectionInternalTypes.h"
#include "mc/world/level/block/BannerBlockType.h"

#include <cstdint>
#include <optional>
#include <vector>

class Block;
class BlockActor;
class CompoundTag;
class Level;

namespace lholo::projection::detail {

// Native banner data only; position, custom name and renderer state are not
// part of correction. Source snapshots live with the projection structure.
struct BannerAppearance {
    std::uint8_t baseColor{};
    BannerBlockType type{};
    std::vector<std::uint8_t> patterns;
    std::vector<std::uint8_t> colors;
};

inline CorrectionState compareBannerAppearance(BannerAppearance const& expected, BannerAppearance const& actual) {
    if (expected.baseColor != actual.baseColor || expected.type != actual.type)
        return CorrectionState::WrongType;
    if (expected.patterns != actual.patterns || expected.colors != actual.colors)
        return CorrectionState::WrongState;
    return CorrectionState::Correct;
}

std::optional<BannerAppearance> readBannerAppearance(BlockActor const* actor);
std::optional<BannerAppearance> loadBannerAppearance(Block const& block, CompoundTag const& nbt, Level& level);

} // namespace lholo::projection::detail
