// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "place/legacy/PlacementExecutor.h"
#include "place/AimedPlacementTarget.h"

#include "place/PlacementState.h"

#include "block/BlockPlacementRules.h"
#include "projection/Projection.h"
#include "structure/StructureLoader.h"

#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/Bedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/network/PacketSender.h"
#include "mc/network/packet/InventoryTransactionPacket.h"
#include "mc/world/Facing.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/ContainerID.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/inventory/transaction/ComplexInventoryTransaction.h"
#include "mc/world/inventory/transaction/InventoryAction.h"
#include "mc/world/inventory/transaction/InventorySource.h"
#include "mc/world/inventory/transaction/InventorySourceType.h"
#include "mc/world/inventory/transaction/InventoryTransaction.h"
#include "mc/world/inventory/transaction/InventoryTransactionItemGroup.h"
#include "mc/world/inventory/transaction/ItemUseInventoryTransaction.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/deps/nbt/ByteTag.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/deps/nbt/Tag.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace lholo::place {

namespace {

using detail::FailedPlanKey;
using detail::PlacementContext;
using detail::packPlacementCell;

auto& placementState() {
    return detail::PlacementState::getInstance();
}
constexpr int kHotbarSlots = 9;
constexpr int kInventorySlots = 36;
// A cell that was just placed must not be re-targeted until the server applies
// it and the correction scan catches up. This window prevents hammering one
// cell; new cells along the ray are placed immediately (bounded by the tick).
constexpr std::uint64_t kCellLockMs = 500;
// Safety floor between any two placements. The tick hook already runs at 20 Hz,
// so this just guards against double-sends on unusual tick rates.
constexpr std::uint64_t kMinSendIntervalMs = 40;
// Backoff for a rejected inventory swap. Without it a failed swap retries every
// tick and spams the server.
constexpr std::uint64_t kSwapRetryMs = 200;
// Bound expensive getPlacementBlock planning in range mode. Failed plans are
// cached by target, exact block state, item aux, eye position and view vector;
// changing the player's aim invalidates the cache immediately.
constexpr int           kRangePlanBudgetPerTick = 16;
constexpr std::uint64_t kFailedPlanCacheMs      = 250;

bool recentlyPlaced(BlockPos const& cell, std::uint64_t now) {
    return placementState().recentPlacementActive(packPlacementCell(cell.x, cell.y, cell.z), now);
}

void markPlaced(BlockPos const& cell, std::uint64_t now) {
    placementState().recordRecentPlacement(packPlacementCell(cell.x, cell.y, cell.z), now, now + kCellLockMs);
}

void updateAimedProjectedBlock(Block const* block) {
    placementState().setAimedProjectedBlock(block ? block->buildDescriptionName() : std::string{});
}

struct ItemFind {
    int             slot;
    ItemStack const* item;
};

BlockPos neighborOf(BlockPos const& position, uchar face) {
    return position + Facing::DIRECTION()[face];
}

uchar oppositeFace(uchar face) {
    return Facing::OPPOSITE_FACING()[face];
}

PlacementContext makePlacementContext(Vec3 const& eye, Vec3 const& view, float reach) {
    auto const quantize = [](float value, float scale) {
        return static_cast<int>(std::lround(value * scale));
    };
    return {
        eye,
        reach * reach,
        quantize(eye.x, 4.0f),
        quantize(eye.y, 4.0f),
        quantize(eye.z, 4.0f),
        quantize(view.x, 32.0f),
        quantize(view.y, 32.0f),
        quantize(view.z, 32.0f),
    };
}

FailedPlanKey makeFailedPlanKey(
    PlacementContext const& context,
    BlockPos const&         cell,
    Block const&            block,
    int                     itemAux
) {
    return {
        packPlacementCell(cell.x, cell.y, cell.z),
        block.mNetworkId,
        itemAux,
        context.eyeX,
        context.eyeY,
        context.eyeZ,
        context.viewX,
        context.viewY,
        context.viewZ,
    };
}

bool isFailedPlanCached(FailedPlanKey const& key, std::uint64_t now) {
    return placementState().failedPlanCached(key, now);
}

void cacheFailedPlan(FailedPlanKey const& key, std::uint64_t now) {
    placementState().cacheFailedPlan(key, now, now + kFailedPlanCacheMs);
}

// Find an inventory slot holding the item that places `block`. Match on item +
// aux only (ignoring block/placement data): a plain inventory comparator or
// redstone item carries no placement state, so the stricter
// sameItemAndAuxAndBlockData never matched a ghost that does.
ItemFind findItemSlot(Player& player, Block const& block) {
    ItemStack const want = block::makePlacementItem(block);
    if (!block::hasUsableItem(want)) return {-1, nullptr};
    auto& inventory = player.getInventory();
    for (int slot = 0; slot < kInventorySlots; ++slot) {
        auto const& item = inventory.getItem(slot);
        if (block::hasUsableItem(item) && item.getIdAux() == want.getIdAux()) return {slot, &item};
    }
    return {-1, nullptr};
}

// Range placement may inspect hundreds of cells in one tick. Index the 36
// inventory slots once by the official combined item/aux key, then verify the
// final match with sameItemAndAux so hash collisions cannot select a wrong item.
using InventorySnapshot = std::unordered_multimap<int, ItemFind>;

InventorySnapshot snapshotInventory(Player& player) {
    InventorySnapshot snapshot;
    snapshot.reserve(kInventorySlots);
    auto& inventory = player.getInventory();
    for (int slot = 0; slot < kInventorySlots; ++slot) {
        auto const& item = inventory.getItem(slot);
        if (block::hasUsableItem(item)) snapshot.emplace(item.getIdAux(), ItemFind{slot, &item});
    }
    return snapshot;
}

ItemFind findItemSlot(InventorySnapshot const& snapshot, Block const& block) {
    ItemStack const want = block::makePlacementItem(block);
    if (!block::hasUsableItem(want)) return {-1, nullptr};
    auto const [first, last] = snapshot.equal_range(want.getIdAux());
    for (auto it = first; it != last; ++it) {
        if (it->second.item->getIdAux() == want.getIdAux()) return it->second;
    }
    return {-1, nullptr};
}

// Server-synced slot exchange expressed as a legacy NormalTransaction: both
// slots swap their items, so it stays valid whether the target slot is empty
// or occupied, and the server keeps its item-stack-net bookkeeping consistent
// (unlike a direct container mutation, which the net manager reverts).
void sendInventorySwap(int fromSlot, int toSlot, ItemStack const& fromItem, ItemStack const& toItem) {
    auto transaction = ComplexInventoryTransaction::fromType(
        ComplexInventoryTransaction::Type::NormalTransaction, InventoryTransaction{}
    );
    if (!transaction) return;
    auto& invTx = transaction->mTransaction.get();
    InventorySource const source{
        InventorySourceType::ContainerInventory,
        ContainerID::Inventory,
        InventorySource::InventorySourceFlags::NoFlag
    };
    invTx.addAction(InventoryAction{source, static_cast<uint>(fromSlot), fromItem, toItem});
    invTx.addAction(InventoryAction{source, static_cast<uint>(toSlot), toItem, fromItem});
    InventoryTransactionPacket packet(
        InventoryTransactionPacketPayload(std::move(transaction), true)
    );
    auto client = ll::service::getClientInstance();
    if (!client) return;
    client->getPacketSender().sendToServer(packet);
}

// Pick which hotbar slot a backpack item should swap into. Prefer an empty slot
// so the player's held items are never evicted, and so two materials never
// thrash through a single slot — each lands in its own empty slot and stays
// there (next tick it is already in the bar, no further swap). Only when the
// hotbar is completely full do we fall back to the currently selected slot.
int chooseHotbarSwapTarget(Player& player) {
    auto& inventory = player.getInventory();
    for (int slot = 0; slot < kHotbarSlots; ++slot) {
        if (inventory.getItem(slot).isNull()) return slot;
    }
    return player.getSelectedItemSlot();
}

// A projected ghost cell the player aims at: the cell itself, the real block
// to place against, the face such that at.neighbor(face) == cell, and the
// expected block that fills the cell.
struct ProjectionTarget {
    BlockPos     cell;
    BlockPos     at;
    uchar        face;
    Block const* block;
    Vec3         clickPos{};  // Exact click point; chosen to reproduce the ghost.
};

bool placeBlock(LocalPlayer& player, ProjectionTarget const& target, int slot, ItemStack const& item) {
    auto& region = player.getDimensionBlockSource();
    if (!region.getBlock(target.cell).isAir()) return false;

    // Server-authoritative placement: build an ItemUseInventoryTransaction and
    // deliver it through the client's packet sender (LoopbackPacketSender in
    // single-player, the network sender on a real server). GameMode::useItemOn
    // only predicts locally and Player::sendNetworkPacket does not reach the
    // integrated server, so neither persists.
    auto transactionBase = ComplexInventoryTransaction::fromType(
        ComplexInventoryTransaction::Type::ItemUseTransaction, InventoryTransaction{}
    );
    if (!transactionBase) return false;
    auto& transaction = static_cast<ItemUseInventoryTransaction&>(*transactionBase);
    transaction.mType = ComplexInventoryTransaction::Type::ItemUseTransaction;
    transaction.mActionType = ItemUseInventoryTransaction::ActionType::Place;
    transaction.mTriggerType = ItemUseInventoryTransaction::TriggerType::PlayerInput;
    // mPos is the block that was clicked on; the server places the new block
    // at mPos.neighbor(mFace). Setting it to the support block makes the
    // placement land exactly on the target ghost cell.
    transaction.mPos = target.at;
    transaction.mFace = target.face;
    // The item is always in the selected hotbar slot by the time we place:
    // hotbar items are selected directly, backpack items were swapped in.
    transaction.mSlot = slot;
    transaction.mHand = HandSlot::Mainhand;
    transaction.mFromPos = player.getPosition();
    // ItemUseInventoryTransaction serializes the hit location relative to the
    // clicked block (mPos), while the planner stores an absolute world point for
    // reach checks. Sending the absolute coordinate made the server resolve the
    // stair half differently at some world heights.
    transaction.mClickPos = Vec3{
        target.clickPos.x - static_cast<float>(target.at.x),
        target.clickPos.y - static_cast<float>(target.at.y),
        target.clickPos.z - static_cast<float>(target.at.z),
    };
    transaction.mClientPredictedResult = ItemUseInventoryTransaction::PredictedResult::Success;
    transaction.mClientCooldownState = ItemUseInventoryTransaction::ClientCooldownState::Off;
    transaction.mTargetBlockId = region.getBlock(target.at).mNetworkId;
    transaction.setSelectedItem(item);
    // The server's stack-net-id system expects the item descriptor to carry
    // the stack net id. With the flag off the writer omits the net id bytes,
    // the server misaligns the stream while reading and silently drops the
    // packet before the transaction is ever validated.
    transaction.mItem.get().mIncludeNetIds = true;

    InventoryTransactionPacket packet(
        InventoryTransactionPacketPayload(std::move(transactionBase), true)
    );
    auto client = ll::service::getClientInstance();
    if (!client) return false;
    client->getPacketSender().sendToServer(packet);
    placementState().setNextPlaceAt(GetTickCount64() + kMinSendIntervalMs);
    return true;
}

// Candidate click points on the face of `cell` shared with the support in
// direction `sf`. Side faces get a low and a high point so the search can reach
// both halves (top/bottom slabs, upside-down stairs); top/bottom faces have a
// single natural point.
template <class F>
void forEachClickCandidate(BlockPos const& cell, uchar sf, F&& fn) {
    float const cx = static_cast<float>(cell.x);
    float const cy = static_cast<float>(cell.y);
    float const cz = static_cast<float>(cell.z);
    switch (static_cast<Facing::Name>(sf)) {
    case Facing::Name::Down:  fn(Vec3{cx + 0.5f, cy + 0.0f, cz + 0.5f}); break;
    case Facing::Name::Up:    fn(Vec3{cx + 0.5f, cy + 1.0f, cz + 0.5f}); break;
    case Facing::Name::North: fn(Vec3{cx + 0.5f, cy + 0.25f, cz + 0.0f});
                              fn(Vec3{cx + 0.5f, cy + 0.75f, cz + 0.0f}); break;
    case Facing::Name::South: fn(Vec3{cx + 0.5f, cy + 0.25f, cz + 1.0f});
                              fn(Vec3{cx + 0.5f, cy + 0.75f, cz + 1.0f}); break;
    case Facing::Name::West:  fn(Vec3{cx + 0.0f, cy + 0.25f, cz + 0.5f});
                              fn(Vec3{cx + 0.0f, cy + 0.75f, cz + 0.5f}); break;
    case Facing::Name::East:  fn(Vec3{cx + 1.0f, cy + 0.25f, cz + 0.5f});
                              fn(Vec3{cx + 1.0f, cy + 0.75f, cz + 0.5f}); break;
    default: break;
    }
}

// Ask vanilla getPlacementBlock what block WOULD result from each reachable
// support/click point using the player's real current rotation. Placements that
// need another facing are left for the player to aim correctly; no movement or
// input packet is altered.
bool isWithinPlacementReach(PlacementContext const& context, Vec3 const& clickPos) {
    float const dx = clickPos.x - context.eye.x;
    float const dy = clickPos.y - context.eye.y;
    float const dz = clickPos.z - context.eye.z;
    return dx * dx + dy * dy + dz * dz <= context.reachSquared;
}

// Read a serialized Bedrock block-state value using the official Block API.
// Empty means the state is absent (all relevant numeric/string values below are
// non-empty, including zero as "0").
std::string serializedState(Block const& block, char const* key) {
    for (auto const& [rootKey, rootValue] : block.mSerializationId.get()) {
        if (rootKey != "states") continue;
        if (!rootValue.hold<::CompoundTag>()) break;
        for (auto const& [stateKey, stateValue] : rootValue.get<::CompoundTag>()) {
            if (stateKey != key) continue;
            auto const type = stateValue.getId();
            if (type == ::Tag::Type::Byte)
                return std::to_string(static_cast<int>(stateValue.get<::ByteTag>().data));
            if (type == ::Tag::Type::Int)
                return std::to_string(stateValue.get<::IntTag>().data);
            if (type == ::Tag::Type::String)
                return static_cast<std::string const&>(stateValue.get<::StringTag>());
            return {};
        }
        break;
    }
    return {};
}

bool sameSerializedState(Block const& predicted, Block const& ghost, char const* key) {
    std::string const expected = serializedState(ghost, key);
    return !expected.empty() && serializedState(predicted, key) == expected;
}

bool isTwoBlockDoor(Block const& block) {
    return block.hasProperty(BlockProperty::Door)
        && !serializedState(block, "upper_block_bit").empty();
}

bool wouldMergeClickedSlab(Block const& ghost, Block const& support) {
    if (!ghost.getBlockType().isSlabBlock() || !support.getBlockType().isSlabBlock()
        || block::isDoubleSlab(ghost) || block::isDoubleSlab(support)) {
        return false;
    }

    ItemInstance const ghostItem = ghost.getBlockType().asItemInstance(ghost, nullptr);
    ItemInstance const supportItem = support.getBlockType().asItemInstance(support, nullptr);
    return block::hasUsableItem(ghostItem) && block::hasUsableItem(supportItem)
        && ghostItem.getIdAux() == supportItem.getIdAux();
}

// RuntimeId is intentionally retained for ordinary blocks. For the three
// reported placement-controlled families, compare only the states that the
// click face/player rotation determines. Other permutation bits may be updated
// from neighbours after placement and must not suppress a correct action.
bool placementPredictionMatches(
    Block const& predicted,
    Block const& ghost,
    Block const* expectedDoorUpper = nullptr
) {
    if (block::placeableBaseName(predicted.getTypeName())
        != block::placeableBaseName(ghost.getTypeName())) return false;

    auto const& name = ghost.getTypeName();
    // Bamboo has only dynamic botanical growth states (age_bit, leaves, thickness)
    // simulated by the game engine, not chosen at placement. Type identity is sufficient.
    if (name == "minecraft:bamboo") return true;

    if (name.ends_with("_stairs")) {
        return sameSerializedState(predicted, ghost, "weirdo_direction")
            && sameSerializedState(predicted, ghost, "upside_down_bit");
    }
    // A slab's top/bottom half is chosen by the click face and hit height, so it
    // must be matched explicitly — otherwise a bottom-slab prediction (the first
    // click candidate) can satisfy a top-slab ghost and place the wrong half.
    // Newer versions store the half as minecraft:vertical_half, older ones as
    // top_slot_bit; a double slab has neither and stays on the strict compare.
    if (ghost.getBlockType().isSlabBlock() && !block::isDoubleSlab(ghost)) {
        if (!serializedState(ghost, "minecraft:vertical_half").empty())
            return sameSerializedState(predicted, ghost, "minecraft:vertical_half");
        if (!serializedState(ghost, "top_slot_bit").empty())
            return sameSerializedState(predicted, ghost, "top_slot_bit");
    }
    if (!serializedState(ghost, "torch_facing_direction").empty()) {
        return sameSerializedState(predicted, ghost, "torch_facing_direction");
    }
    if (!serializedState(ghost, "pillar_axis").empty()) {
        return sameSerializedState(predicted, ghost, "pillar_axis");
    }
    // Walls, fences, glass panes and iron bars derive every connection state from
    // their neighbours after placement (nothing is chosen at placement), so accept
    // the placement on block identity alone — the connections resolve as the
    // surrounding blocks fill in.
    // Repeaters and comparators: only facing is chosen at placement (delay/mode
    // are set by right-clicking afterwards, the powered bit is redstone-driven),
    // so match on facing alone — including the powered name variants, which reach
    // here via the shared placeable-base rule. This also lets a delay-adjusted repeater place.
    if (name == "minecraft:unpowered_repeater" || name == "minecraft:powered_repeater"
        || name == "minecraft:unpowered_comparator" || name == "minecraft:powered_comparator") {
        return sameSerializedState(predicted, ghost, "minecraft:cardinal_direction");
    }
    if (isTwoBlockDoor(ghost)) {
        // A door item places both cells. The lower ghost owns direction/open,
        // the upper owns the hinge. Verify direction/open/half always; the hinge
        // only when the upper half is visible (expectedDoorUpper). A layer cut
        // hides the upper, leaving the hinge unverifiable — one DoorItem use
        // still creates both halves, so accept the placement without it.
        bool matched = sameSerializedState(predicted, ghost, "upper_block_bit")
            && sameSerializedState(predicted, ghost, "direction")
            && sameSerializedState(predicted, ghost, "open_bit");
        if (matched && expectedDoorUpper) {
            std::string const expectedHinge = serializedState(*expectedDoorUpper, "door_hinge_bit");
            matched = !expectedHinge.empty()
                && serializedState(predicted, "door_hinge_bit") == expectedHinge;
        }
        return matched;
    }
    // A runtime lit/heat variant (lit lamp/ore, burning furnace) is placed as its
    // base block and switched on by the game afterwards, so the names only matched
    // after normalization and the runtime id is expected to differ. Accept on
    // facing where directional (furnaces), otherwise unconditionally (lamp/ore).
    if (block::placeableBaseName(ghost.getTypeName()) != ghost.getTypeName()) {
        if (!serializedState(ghost, "facing_direction").empty())
            return sameSerializedState(predicted, ghost, "facing_direction");
        if (!serializedState(ghost, "minecraft:cardinal_direction").empty())
            return sameSerializedState(predicted, ghost, "minecraft:cardinal_direction");
        return true;
    }
    // Bedrock owns neighbour-derived placement-state tolerance (walls, fences,
    // panes, bars and future equivalents). Keep specialized player-controlled
    // states above strict, then defer all remaining exceptions to the official
    // API instead of maintaining block-name suffix lists.
    if (ghost.getBlockType().allowStateMismatchOnPlacement(predicted, ghost)) return true;
    return predicted == ghost;
}

bool resolveOrientedPlacement(
    LocalPlayer&            player,
    BlockSource&            region,
    PlacementContext const& context,
    BlockPos const&         cell,
    Block const&            ghost,
    int                     itemAux,
    ProjectionTarget&       out
) {
    if (!region.getBlock(cell).isAir()) return false;

    Block const* expectedDoorUpper = nullptr;
    bool const   isDoor = isTwoBlockDoor(ghost);

    if (isDoor) {
        // The upper projected half is never an independent placement target.
        // One DoorItem use on the lower cell creates both halves.
        if (serializedState(ghost, "upper_block_bit") != "0") return false;
        BlockPos const upperCell = neighborOf(cell, static_cast<uchar>(Facing::Name::Up));
        // The upper cell must be free for the door's second half.
        if (!region.getBlock(upperCell).isAir()) return false;
        // When the upper half is visible we use its hinge to verify the placement
        // exactly. When a layer cut hides it, the cell is not in the query-able
        // virtual world (hasBlock=false) — we still place the door and let the
        // hinge be best-effort, since one DoorItem use creates both halves anyway.
        auto const upper = projection::queryProjection(player, upperCell);
        if (upper.block && upper.missing && upper.block->getTypeName() == ghost.getTypeName()
            && serializedState(*upper.block, "upper_block_bit") == "1") {
            expectedDoorUpper = upper.block;
        }
        // DoorBlock::mayPlace is the official two-cell/support validation. It is
        // intentionally used only for doors; treating it as a universal gate
        // previously rejected valid stairs and wall-mounted blocks.
        if (!ghost.getBlockType().mayPlace(region, cell)) return false;
    }

    // Only accept a real support and a click point for which the official
    // placement predictor reproduces the complete projected block state.
    // Do not call BlockSource::mayPlace here: LHolo sends a legacy transaction
    // directly, while mayPlace belongs to the full vanilla interaction path;
    // treating its result as a hard gate blocked valid torch/stair placements.
    auto const tryPlacement = [&](BlockPos const& at, uchar face, Vec3 const& clickPos, ProjectionTarget& result) {
        if (!isWithinPlacementReach(context, clickPos)) return false;

        // Mirror the official use-on chain: `at` is the block being clicked,
        // matching GameMode::useItemOn(... at, face, hit ...) and the
        // transaction's mPos. Passing the target air cell here made mounted
        // blocks see no support and gave stairs the wrong placement context.
        Vec3 const relativeClick{
            clickPos.x - static_cast<float>(at.x),
            clickPos.y - static_cast<float>(at.y),
            clickPos.z - static_cast<float>(at.z),
        };
        // BlockItem first converts the clicked support position to the target
        // placement cell, then asks the block for its permutation. Feed the same
        // target position and relative hit vector used by the item-use path.
        Block const& predicted = ghost.getBlockType().getPlacementBlock(
            player, cell, face, relativeClick, itemAux
        );
        if (!placementPredictionMatches(predicted, ghost, expectedDoorUpper)) return false;

        result = ProjectionTarget{cell, at, face, &ghost, clickPos};
        return true;
    };

    auto const searchCurrentRotation = [&](ProjectionTarget& result) {
        uchar const firstSupport = isDoor ? static_cast<uchar>(Facing::Name::Down) : 0;
        uchar const supportEnd   = isDoor ? firstSupport + 1 : 6;
        for (uchar sf = firstSupport; sf < supportEnd; ++sf) {
            BlockPos const at = neighborOf(cell, sf);
            Block const& support = region.getBlock(at);
            // A matching slab only merges into a double slab when clicked from
            // below/above; a side slab is a valid support that places into the
            // target cell, so a slab-next-to-slab must not skip it.
            bool const verticalSupport = sf == static_cast<uchar>(Facing::Name::Down)
                || sf == static_cast<uchar>(Facing::Name::Up);
            if (support.isAir() || (verticalSupport && wouldMergeClickedSlab(ghost, support))) continue;

            uchar const face = oppositeFace(sf);
            bool matched = false;
            forEachClickCandidate(cell, sf, [&](Vec3 const& clickPos) {
                if (!matched && tryPlacement(at, face, clickPos, result)) matched = true;
            });
            if (matched) return true;
        }

        // Preserve the original LHolo air-mPos path even when an adjacent
        // support exists. A support below can only yield a vertical pillar;
        // using the air target itself lets the official predictor select a side
        // face for horizontal logs and other face-dependent states. Exact
        // RuntimeId matching still prevents an incorrect placement.
        // DoorItem only accepts a floor-supported lower cell and creates its
        // upper half itself; never use the floating/air-mPos fallback for doors.
        if (isDoor) return false;
        // A slab's half follows the click height, but this air-mPos fallback
        // predicts from the clicked face — the two diverge (a low click for a top
        // slab passes the predictor yet the server places the bottom half). Slabs
        // place through a real support above/beside (handled above); skip the
        // fallback so a wrong-half slab is never sent.
        if (ghost.getBlockType().isSlabBlock()) return false;

        float const cx = static_cast<float>(cell.x);
        float const cy = static_cast<float>(cell.y);
        float const cz = static_cast<float>(cell.z);
        for (uchar face = 0; face < 6; ++face) {
            if (tryPlacement(cell, face, Vec3{cx + 0.5f, cy + 0.25f, cz + 0.5f}, result)) return true;
            if (tryPlacement(cell, face, Vec3{cx + 0.5f, cy + 0.75f, cz + 0.5f}, result)) return true;
        }
        return false;
    };

    return searchCurrentRotation(out);
}

void tickRangePlaceImpl(LocalPlayer& player, PlacementContext const& placementContext) {
    auto const now = GetTickCount64();
    Vec3 const center = player.getPosition();
    float const radius = static_cast<float>(placementState().radius());
    auto& region = player.getDimensionBlockSource();

    auto candidates = projection::queryMissingCellsInRange(player, center, radius);
    bool const suppressionsActive = placementState().autoPlacementSuppressionsActive(now);
    auto const inventorySnapshot = snapshotInventory(player);
    int plannedCandidates = 0;
    for (auto const& cand : candidates) {
        BlockPos const cell{cand.x, cand.y, cand.z};

        // Real-world occupancy guard: if a block already occupies this cell in
        // the world, skip it immediately. This skips already-placed blocks,
        // waterlogged blocks whose solid body is in place, and saves expensive
        // inventory scans, hashing, and placement resolution.
        if (!region.getBlock(cell).isAir()) continue;

        // Empty suppression state takes one branch for the entire candidate
        // batch; hash lookups happen only during an active ten-second window.
        if (suppressionsActive
            && placementState().autoPlacementSuppressed(packPlacementCell(cell.x, cell.y, cell.z), now)) {
            continue;
        }

        // Skip cells placed a moment ago until the server applies them, so a
        // cell is never placed twice mid-round-trip (the slab double-place).
        if (recentlyPlaced(cell, now)) continue;

        auto const found = findItemSlot(inventorySnapshot, *cand.block);
        if (found.slot < 0) continue;

        FailedPlanKey const failedKey =
            makeFailedPlanKey(placementContext, cell, *cand.block, found.item->getAuxValue());
        if (isFailedPlanCached(failedKey, now)) continue;
        if (plannedCandidates >= kRangePlanBudgetPerTick) return;
        ++plannedCandidates;

        // Use the actual inventory stack's aux value for the same prediction
        // the server will perform. Reject impossible placements before sending.
        ProjectionTarget target;
        if (!resolveOrientedPlacement(
                player,
                region,
                placementContext,
                cell,
                *cand.block,
                found.item->getAuxValue(),
                target
            )) {
            cacheFailedPlan(failedKey, now);
            continue;
        }

        if (found.slot >= kHotbarSlots) {
            // Back off a rejected swap; see sendInventorySwap and the same
            // logic in tickEasyPlaceImpl.
            if (now < placementState().nextSwapAt()) continue;
            auto& inventory = player.getInventory();
            int const hotbarSlot = chooseHotbarSwapTarget(player);
            auto const& toItem = inventory.getItem(hotbarSlot);
            sendInventorySwap(found.slot, hotbarSlot, *found.item, toItem);
            placementState().setNextSwapAt(now + kSwapRetryMs);
            return;
        }
        player.setSelectedSlot(found.slot);
        if (placeBlock(player, target, found.slot, *found.item)) {
            markPlaced(cell, now);
            return;
        }
        cacheFailedPlan(failedKey, now);
    }
}

void tickEasyPlaceImpl() {
    auto client = ll::service::getClientInstance();
    if (!client) {
        updateAimedProjectedBlock(nullptr);
        return;
    }
    auto* player = client->getLocalPlayer();
    if (!player) {
        updateAimedProjectedBlock(nullptr);
        return;
    }
    auto const tickNow = GetTickCount64();
    // Only act during gameplay: menus, pause screens and the LHolo GUI itself
    // disable in-game input.
    if (!client->isInGameInputEnabled()) {
        updateAimedProjectedBlock(nullptr);
        return;
    }

    bool const placementActive = placementState().enabled()
        || placementState().manualMode()
        || placementState().rangeEnabled();
    bool const showProjectedBlockName = structure::shouldShowProjectedBlockName();
    if (!placementActive && !showProjectedBlockName) {
        updateAimedProjectedBlock(nullptr);
        return;
    }

    // Ray from the camera eye along the view direction against the projection.
    Vec3 const origin = player->getEyePos();
    Vec3 const rawDir = player->getViewVector(1.0f);
    float const length = std::sqrt(rawDir.x * rawDir.x + rawDir.y * rawDir.y + rawDir.z * rawDir.z);
    if (length <= 0.0f) {
        updateAimedProjectedBlock(nullptr);
        return;
    }
    Vec3 const dir{rawDir.x / length, rawDir.y / length, rawDir.z / length};

    float const pickRange = player->getPickRange();
    auto target = findAimedPlacementTarget(*player, origin, dir, pickRange);
    updateAimedProjectedBlock(showProjectedBlockName && target ? target->block : nullptr);
    if (!placementActive) return;
    PlacementContext const placementContext = makePlacementContext(origin, dir, pickRange);
    if (tickNow < placementState().nextPlaceAt()) return;

    // Range placement scans everything within the configured radius.
    if (placementState().rangeEnabled()) {
        tickRangePlaceImpl(*player, placementContext);
        return;
    }

    // Single-crosshair placement: auto (轻松放置) or manual (手动放置), which are
    // mutually exclusive in the UI. Manual mode accepts the first block from
    // the press request, then repeats after the hold delay; the buildBlock hook
    // cancels vanilla placement so nothing is placed twice.
    bool const manualPlacement = placementState().manualMode();
    auto const manualPress = manualPlacement ? placementState().dueManualPress(GetTickCount64()) : std::nullopt;
    if (manualPlacement && !manualPress) return;
    if (!target) return;

    // Skip cells placed a moment ago until the server applies them; new cells
    // place immediately.
    auto const now = GetTickCount64();
    if (!manualPlacement
        && placementState().autoPlacementSuppressionsActive(now)
        && placementState().autoPlacementSuppressed(packPlacementCell(target->cell.x, target->cell.y, target->cell.z), now)) {
        return;
    }
    if (recentlyPlaced(target->cell, now)) return;

    auto const found = findItemSlot(*player, *target->block);
    if (found.slot < 0) return;

    // Only send a placement whose full predicted state and official placement
    // checks match the projection, using the real stack's aux value.
    auto& region = player->getDimensionBlockSource();
    ProjectionTarget placement;
    if (!resolveOrientedPlacement(
            *player,
            region,
            placementContext,
            target->cell,
            *target->block,
            found.item->getAuxValue(),
            placement
        )) {
        return;
    }

    if (found.slot >= kHotbarSlots) {
        // Back off a rejected swap so it never retries more often than
        // kSwapRetryMs.
        if (now < placementState().nextSwapAt()) return;
        // The server only accepts placements from the selected hotbar slot.
        // Swap the backpack item into the currently selected slot through a
        // server-synced NormalTransaction. Do not place in the same tick: the
        // server's item-stack-net bookkeeping lags the legacy swap, so an
        // immediate placement can be rejected and then re-throttled by the cell
        // lock. The next tick finds the item in the hotbar and places via the
        // single-packet fast path.
        auto& inventory = player->getInventory();
        int const hotbarSlot = chooseHotbarSwapTarget(*player);
        auto const& toItem = inventory.getItem(hotbarSlot);
        sendInventorySwap(found.slot, hotbarSlot, *found.item, toItem);
        placementState().setNextSwapAt(now + kSwapRetryMs);
        return;
    }
    player->setSelectedSlot(found.slot);
    if (!placeBlock(*player, placement, found.slot, *found.item)) return;
    markPlaced(placement.cell, now);
    if (manualPress) placementState().recordManualAttempt(*manualPress, now);
}
} // namespace

namespace detail {

ManualTargetStatus manualTargetStatusUnderCrosshair(LocalPlayer& player) {
    auto const target = findAimedPlacementTarget(player);
    if (!target) return ManualTargetStatus::None;
    return findItemSlot(player, *target->block).slot >= 0
        ? ManualTargetStatus::Ready
        : ManualTargetStatus::MissingMaterial;
}

void tickEasyPlace() {
    tickEasyPlaceImpl();
}

} // namespace lholo::place::detail

} // namespace lholo::place
