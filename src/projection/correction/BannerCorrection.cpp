#include "projection/correction/BannerCorrection.h"

#include "mc/dataloadhelper/NewUniqueIdsDataLoadHelper.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BannerBlockActor.h"
#include "mc/world/level/block/actor/VanillaBlockActorFactory.h"

namespace lholo::projection::detail {

std::optional<BannerAppearance> readBannerAppearance(BlockActor const* actor) {
    if (!actor || actor->getType() != BlockActorType::Banner) return std::nullopt;
    auto const& banner = static_cast<BannerBlockActor const&>(*actor);
    return BannerAppearance{banner.mBaseColor, banner.mBannerType,
        banner.mPatterns.get(), banner.mColors.get()};
}

std::optional<BannerAppearance> loadBannerAppearance(Block const& block, CompoundTag const& nbt, Level& level) {
    auto actor = VanillaBlockActorFactory::createBlockActor(BlockPos{0, 0, 0}, block.getBlockType());
    if (!actor) return std::nullopt;
    NewUniqueIdsDataLoadHelper helper;
    helper.mLevel = &level;
    actor->load(level, nbt, helper);
    return readBannerAppearance(actor.get());
}

} // namespace lholo::projection::detail
