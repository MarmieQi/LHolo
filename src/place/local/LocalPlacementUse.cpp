#include "place/local/LocalPlacementUse.h"
#include "place/local/PlacementRestock.h"

#include "block/BlockPlacementRules.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/nbt/ByteTag.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/util/BlockUtils.h"
#include "mc/world/Facing.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/BlockItem.h"
#include "mc/world/item/BucketItem.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/material/Material.h"

#include <memory>
#include <optional>

namespace lholo::place::local {
namespace {

struct PlacementOverride {
    Dimension& dimension;
    BlockPos const& cell;
    Block const& target;
    Block const* upper;
    bool applied{};
    bool upperApplied{};
};

thread_local PlacementOverride* activeOverride{};

std::optional<uchar> slabMergeFace(Block const& slab) {
    auto const face = [](bool top) { return static_cast<uchar>(top ? Facing::Name::Down : Facing::Name::Up); };
    for (auto const& [key, value] : slab.mSerializationId.get()) {
        if (key != "states" || !value.hold<CompoundTag>()) continue;
        for (auto const& [state, tag] : value.get<CompoundTag>()) {
            if (state == "minecraft:vertical_half" && tag.hold<StringTag>()) {
                auto const& half = static_cast<std::string const&>(tag.get<StringTag>());
                if (half == "top" || half == "bottom") return face(half == "top");
            }
            if (state == "top_slot_bit" && tag.hold<ByteTag>()) return face(tag.get<ByteTag>().data != 0);
        }
    }
    return std::nullopt;
}

class ScopedPlacementOverride {
public:
    explicit ScopedPlacementOverride(PlacementOverride& request)
        : previous(activeOverride) { activeOverride = &request; }
    ~ScopedPlacementOverride() { activeOverride = previous; }

private:
    PlacementOverride* const previous;
};

Block const& placementState(BlockSource& source, BlockPos const& pos, Block const& native) {
    auto* request = activeOverride;
    if (!request || &source.getDimension() != &request->dimension
        || &native.getBlockType() != &request->target.getBlockType()) return native;
    if (native.getBlockType().mMaterial->mLiquid) return native;
    if (!request->applied && request->cell == pos) {
        request->applied = true;
        return request->target;
    }
    if (request->upper && !request->upperApplied && request->cell + BlockPos{0, 1, 0} == pos) {
        request->upperApplied = true;
        return *request->upper;
    }
    return native;
}

LL_TYPE_INSTANCE_HOOK(
    LocalPlacementPosition, ll::memory::HookPriority::Normal, BlockItem, &BlockItem::$_calculatePlacePos, bool,
    ItemStackBase const& item, Actor const& actor, uchar& face, BlockPos& pos
) {
    bool const valid = origin(item, actor, face, pos);
    return valid && (!activeOverride || pos == activeOverride->cell);
}

LL_TYPE_INSTANCE_HOOK(
    LocalBucketPosition, ll::memory::HookPriority::Normal, BucketItem, &BucketItem::_emptyBucket, bool,
    BlockSource& region, Block const& contents, BlockPos const& pos, Actor* placer, ItemStack const& item, uchar face
) {
    if (activeOverride && &region.getDimension() == &activeOverride->dimension && pos != activeOverride->cell)
        return false;
    return origin(region, contents, pos, placer, item, face);
}

LL_TYPE_INSTANCE_HOOK(
    LocalPlacementSetBlock, ll::memory::HookPriority::Normal, BlockSource, &BlockSource::$setBlock, bool,
    BlockPos const& pos, Block const& block, int flags,
    ActorBlockSyncMessage const* sync, BlockChangeContext const& context
) {
    return origin(pos, placementState(*this, pos, block), flags, sync, context);
}

LL_TYPE_INSTANCE_HOOK(
    LocalPlacementSetBlockWithActor, ll::memory::HookPriority::Normal, BlockSource,
    static_cast<bool (BlockSource::*)(BlockPos const&, Block const&, int, std::shared_ptr<BlockActor>,
        ActorBlockSyncMessage const*, BlockChangeContext const&)>(&BlockSource::setBlock), bool,
    BlockPos const& pos, Block const& block, int flags, std::shared_ptr<BlockActor> actor,
    ActorBlockSyncMessage const* sync, BlockChangeContext const& context
) {
    return origin(pos, placementState(*this, pos, block), flags, std::move(actor), sync, context);
}

} // namespace

bool installPlacementOverride() {
    if (LocalPlacementSetBlock::hook() != 0) return false;
    if (LocalPlacementSetBlockWithActor::hook() == 0) {
        if (LocalPlacementPosition::hook() == 0) {
            if (LocalBucketPosition::hook() == 0) return true;
            LocalPlacementPosition::unhook();
        }
        LocalPlacementSetBlockWithActor::unhook();
    }
    LocalPlacementSetBlock::unhook();
    return false;
}

void uninstallPlacementOverride() {
    LocalBucketPosition::unhook();
    LocalPlacementPosition::unhook();
    LocalPlacementSetBlockWithActor::unhook();
    LocalPlacementSetBlock::unhook();
}

PlacementUseResult usePlacementItem(Player& player, BlockSource& region, BlockPos const& cell,
    Block const& target, Block const* upper, int slot, bool mergeSlab, PlacementRestock* restock) {
    auto const& directions = Facing::DIRECTION();
    auto& inventory = player.getInventory();
    int const selected = player.getSelectedItemSlot();
    constexpr int hotbarSize = 9;
    auto const& original = region.getBlock(cell);
    bool const liquid = target.getBlockType().mMaterial->mLiquid;
    bool tried = false;
    auto const useOn = [&](BlockPos const& at, uchar face, Vec3 const& hit) -> std::optional<PlacementUseResult> {
        auto const& clickedBlock = region.getBlock(at);
        if (!tried) {
            if (restock) restock->apply(player);
            if (slot >= hotbarSize) inventory.swapSlots(slot, selected);
            else player.setSelectedSlot(slot);
            tried = true;
        }
        ItemStack item = player.getSelectedItem();
        PlacementOverride request{region.getDimension(), cell, target, upper};
        InteractionResult result;
        {
            ScopedPlacementOverride scope(request);
            result = player.mGameMode->useItemOn(item, at, face, hit, HandSlot::Mainhand, &clickedBlock, true);
        }
        if (!result.mSuccess && !request.applied && item == player.getSelectedItem()
            && region.getBlock(cell) == original) return std::nullopt;
        player.setSelectedItem(item);
        player.sendInventory(true);
        if (liquid) {
            auto const& placed = region.getLiquidBlock(cell);
            return block::sameLiquid(target, placed) && BlockUtils::isLiquidSource(placed)
                ? PlacementUseResult::Placed : PlacementUseResult::UnmatchedWrite;
        }
        return request.applied && (!upper || request.upperApplied) && region.getBlock(cell) == target
            && (!upper || region.getBlock(cell + BlockPos{0, 1, 0}) == *upper)
            ? PlacementUseResult::Placed : PlacementUseResult::UnmatchedWrite;
    };
    if (mergeSlab) {
        if (auto const face = slabMergeFace(original))
            if (auto const outcome = useOn(cell, *face, cell.center())) return *outcome;
    } else if (liquid && !original.isAir()) {
        if (auto const outcome = useOn(cell, static_cast<uchar>(Facing::Name::Up), cell.center())) return *outcome;
    }
    for (std::size_t side = 0; !mergeSlab && side < directions.size(); ++side) {
        if (upper && directions[side].y != -1) continue;
        BlockPos const at = cell + directions[side];
        auto const& clicked = region.getBlock(at);
        if (clicked.isAir() || clicked.getBlockType().isInteractiveBlock()) continue;
        if (directions[side].y != 0 && target.getBlockType().isSlabBlock()
            && &clicked.getBlockType() == &target.getBlockType()) continue;
        auto const hit = (cell.center() + at.center()) * 0.5f;
        if (auto const outcome = useOn(at, Facing::OPPOSITE_FACING()[side], hit)) return *outcome;
    }
    bool const floating = !mergeSlab && !tried && !upper && !liquid;
    if (floating) {
        // Like legacy range placement, use the replaceable target as the
        // clicked block when there is no support face for a floating target.
        if (auto const outcome = useOn(cell, static_cast<uchar>(Facing::Name::Up), cell.center())) return *outcome;
    }
    if (!tried) return PlacementUseResult::NoFace;
    if (slot >= hotbarSize) inventory.swapSlots(slot, selected);
    else player.setSelectedSlot(selected);
    if (restock) restock->restore(player);
    player.sendInventory(true);
    return floating ? PlacementUseResult::FloatingRejected : PlacementUseResult::Rejected;
}

} // namespace lholo::place::local
