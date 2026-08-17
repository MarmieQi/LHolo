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

#include "place/PlaceHelper.h"

#include "plugin/LHolo.h"
#include "projection/Projection.h"
#include "structure/StructureLoader.h"

#include "ll/api/memory/Hook.h"
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
#include "mc/world/inventory/transaction/ItemUseInventoryTransaction.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/deps/nbt/ByteTag.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/deps/nbt/Tag.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace lholo::place {
namespace {

// Easy-place searches the full inventory (hotbar 0-8, backpack 9-35) for the
// matching block item and references the found slot directly in the placement
// transaction, so no cross-container inventory request is needed.
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
constexpr std::uint64_t kSwapRetryMs = 50;
// Manual-mode typematic repeat: after the first block on press, holding pauses
// for kManualInitialDelayMs and then auto-repeats every kManualRepeatIntervalMs
// (like keyboard key-repeat), so a tap places one and a hold streams at a steady
// rate.
constexpr std::uint64_t kManualInitialDelayMs   = 150;
constexpr std::uint64_t kManualRepeatIntervalMs = 120;
// A pending first-block request expires if it cannot be fulfilled this long
// after the press, so a stale click never places a block later.
constexpr std::uint64_t kManualRequestTimeoutMs = 400;

std::atomic_bool gEnabled{false};
std::atomic_bool gRangeEnabled{false};
// Manual mode: hold the right mouse button to place, instead of auto-placing.
std::atomic_bool gManualMode{false};
// Manual-mode press/hold tracking (typematic repeat). gManualHeld spans the
// press (startBuildBlock) to the release (stopBuildBlock); the timestamps drive
// the repeat rate.
std::atomic_bool     gManualHeld{false};
// Pending first block of a press; set on press and kept until placed so a quick
// tap (released before the next game tick) still places one block.
std::atomic_bool     gManualPlaceRequested{false};
std::atomic_uint64_t gManualPressAt{0};
std::atomic_uint64_t gLastManualPlaceAt{0};
// Scan radius for range placement (blocks). Actual placement still respects
// the player's reach.
std::atomic_int gPlacementRadius{4};
std::atomic_uint64_t gNextPlaceAt{0};
std::atomic_uint64_t gNextSwapAt{0};
// Recently-placed cells, so a cell placed a tick ago is not targeted again
// before the server applies it and the correction scan catches up. This is what
// stops a slab (or any block) being placed twice into the same cell during the
// server round-trip (the reported "slab becomes a full block"). Keyed by packed
// world position; the value is the millisecond time the lock expires.
std::mutex                                      gRecentMutex;
std::unordered_map<std::int64_t, std::uint64_t> gRecentPlacements;

std::int64_t packBlockPos(BlockPos const& p) {
    return (static_cast<std::int64_t>(p.x) & 0x3FFFFFF) << 38
         | (static_cast<std::int64_t>(p.z) & 0x3FFFFFF) << 12
         | (static_cast<std::int64_t>(p.y) & 0xFFF);
}

bool recentlyPlaced(BlockPos const& cell, std::uint64_t now) {
    std::lock_guard lock(gRecentMutex);
    auto const it = gRecentPlacements.find(packBlockPos(cell));
    return it != gRecentPlacements.end() && now < it->second;
}

void markPlaced(BlockPos const& cell, std::uint64_t now) {
    std::lock_guard lock(gRecentMutex);
    if (gRecentPlacements.size() > 256) {
        for (auto it = gRecentPlacements.begin(); it != gRecentPlacements.end();) {
            it = now >= it->second ? gRecentPlacements.erase(it) : std::next(it);
        }
    }
    gRecentPlacements[packBlockPos(cell)] = now + kCellLockMs;
}
// Name of the block-entity block the crosshair currently points at, shown in
// the HUD so projected chests/signs/hoppers/... can be identified.
std::string gAimedBlockEntityName;
std::mutex  gAimedNameMutex;

void updateAimedBlockEntityName(Block const* block) {
    std::string name;
    if (block && block->getBlockEntityType() != BlockActorType::Undefined) {
        ItemStack const item(*block, 1, nullptr);
        name = item.getName();
    }
    std::lock_guard lock(gAimedNameMutex);
    gAimedBlockEntityName = std::move(name);
}

auto& logger() {
    return LHolo::getInstance().getSelf().getLogger();
}

struct ItemFind {
    int             slot;
    ItemStack const* item;
};

// Blocks whose placing item has a different name than the block. ItemStack(Block)
// does not resolve these to the right inventory item (the item was never found),
// so the item is built from its real name instead.
char const* placingItemName(std::string const& blockName) {
    if (blockName == "minecraft:redstone_wire") return "minecraft:redstone";
    if (blockName == "minecraft:unpowered_comparator" || blockName == "minecraft:powered_comparator")
        return "minecraft:comparator";
    if (blockName == "minecraft:unpowered_repeater" || blockName == "minecraft:powered_repeater")
        return "minecraft:repeater";
    if (blockName == "minecraft:unlit_redstone_torch") return "minecraft:redstone_torch";
    return nullptr;
}

// Find an inventory slot holding the item that places `block`. Match on item +
// aux only (ignoring block/placement data): a plain inventory comparator or
// redstone item carries no placement state, so the stricter
// sameItemAndAuxAndBlockData never matched a ghost that does.
ItemFind findItemSlot(Player& player, Block const& block) {
    char const* const itemName = placingItemName(block.getTypeName());
    ItemStack const want = itemName ? ItemStack(std::string_view{itemName}, 1, 0, nullptr)
                                    : ItemStack(block, 1, nullptr);
    auto& inventory = player.getInventory();
    for (int slot = 0; slot < kInventorySlots; ++slot) {
        auto const& item = inventory.getItem(slot);
        if (item.sameItemAndAux(want)) return {slot, &item};
    }
    return {-1, nullptr};
}

// Server-synced slot exchange expressed as a legacy NormalTransaction: both
// slots swap their items, so it stays valid whether the target slot is empty
// or occupied, and the server keeps its item-stack-net bookkeeping consistent
// (unlike a direct container mutation, which the net manager reverts).
void sendInventorySwap(LocalPlayer& player, int fromSlot, int toSlot, ItemStack const& fromItem, ItemStack const& toItem) {
    auto transaction = ComplexInventoryTransaction::fromType(ComplexInventoryTransaction::Type::NormalTransaction);
    if (!transaction) return;
    auto& invTx = transaction->mTransaction.get();
    InventorySource const source{
        InventorySourceType::ContainerInventory,
        ContainerID::Inventory,
        InventorySource::InventorySourceFlags::NoFlag
    };
    invTx.addAction(InventoryAction{source, static_cast<uint>(fromSlot), fromItem, toItem});
    invTx.addAction(InventoryAction{source, static_cast<uint>(toSlot), toItem, fromItem});
    InventoryTransactionPacket packet(std::move(transaction), true);
    player.getClientInstance().getPacketSender().sendToServer(packet);
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

// Which face of a cell points most along the given (unit) direction.
uchar faceToward(Vec3 const& v) {
    float const absX = std::abs(v.x);
    float const absY = std::abs(v.y);
    float const absZ = std::abs(v.z);
    if (absX >= absY && absX >= absZ) {
        return v.x > 0 ? static_cast<uchar>(Facing::Name::East) : static_cast<uchar>(Facing::Name::West);
    }
    if (absY >= absZ) {
        return v.y > 0 ? static_cast<uchar>(Facing::Name::Up) : static_cast<uchar>(Facing::Name::Down);
    }
    return v.z > 0 ? static_cast<uchar>(Facing::Name::South) : static_cast<uchar>(Facing::Name::North);
}

// Shared by easy-place and range placement: pick the placement target for a
// ghost `cell` given the approach direction (unit vector from the player
// toward the cell). Prefers a real, non-air neighbor as the support; when none
// exists it falls back to pointing mPos at the ghost cell itself (the server
// places at an air mPos).
ProjectionTarget selectPlacementTarget(BlockSource& region, BlockPos const& cell, Vec3 const& approachDir, Block const* block) {
    uchar bestFace = std::numeric_limits<uchar>::max();
    float bestScore = std::numeric_limits<float>::lowest();
    for (uchar face = 0; face < 6; ++face) {
        BlockPos const at = cell.neighbor(face);
        if (region.getBlock(at).isAir()) continue;
        float const offX = static_cast<float>(at.x - cell.x);
        float const offY = static_cast<float>(at.y - cell.y);
        float const offZ = static_cast<float>(at.z - cell.z);
        float const score = -(offX * approachDir.x + offY * approachDir.y + offZ * approachDir.z);
        if (score > bestScore) {
            bestScore = score;
            bestFace = face;
        }
    }
    if (bestFace == std::numeric_limits<uchar>::max()) {
        // No real support nearby. The server places the block AT an air mPos
        // (instead of mPos.neighbor(mFace) for a solid mPos), so point mPos at
        // the ghost cell itself to make the block land there.
        return ProjectionTarget{cell, cell, Facing::getOpposite(faceToward({-approachDir.x, -approachDir.y, -approachDir.z})), block};
    }
    return ProjectionTarget{cell, cell.neighbor(bestFace), Facing::getOpposite(bestFace), block};
}

// Voxel raycast (Amanatides & Woo) against the real world plus the projection's
// virtual grid. The vanilla crosshair hit result never sees LHolo's drawn ghost
// blocks, so the ray is traced manually: real blocks block it, projected
// missing cells are the placement targets.
std::optional<ProjectionTarget> findProjectionTarget(
    LocalPlayer& player,
    Vec3 const&  origin,
    Vec3 const&  dir,
    float        maxDist
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
    float tMaxX = (stepX > 0 ? (static_cast<float>(x) + 1.0f - origin.x) : (origin.x - static_cast<float>(x))) * tDeltaX;
    float tMaxY = (stepY > 0 ? (static_cast<float>(y) + 1.0f - origin.y) : (origin.y - static_cast<float>(y))) * tDeltaY;
    float tMaxZ = (stepZ > 0 ? (static_cast<float>(z) + 1.0f - origin.z) : (origin.z - static_cast<float>(z))) * tDeltaZ;

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
        if (!region.getBlock(cell).isAir()) {
            // A real block blocks the ray. Placing into the camera-side cell
            // (the vanilla placement position) fills an adjacent ghost. Never
            // target the cell the camera itself is standing in.
            BlockPos const neighbor = cell.neighbor(entryFace);
            auto const query = projection::queryProjection(neighbor);
            if (neighbor != originCell && query.block && query.missing) {
                return ProjectionTarget{neighbor, cell, entryFace, query.block};
            }
            break;
        }
        auto const query = projection::queryProjection(cell);
        if (!query.block || !query.missing) continue;

        // The ghost itself is the target; reuse the shared support selection
        // (real neighbor preferred, air-mPos fallback when floating).
        return selectPlacementTarget(region, cell, dir, query.block);
    }
    return std::nullopt;
}

void placeBlock(LocalPlayer& player, ProjectionTarget const& target, int slot, ItemStack const& item) {
    auto& region = player.getDimensionBlockSource();

    // Server-authoritative placement: build an ItemUseInventoryTransaction and
    // deliver it through the client's packet sender (LoopbackPacketSender in
    // single-player, the network sender on a real server). GameMode::useItemOn
    // only predicts locally and Player::sendNetworkPacket does not reach the
    // integrated server, so neither persists.
    ItemUseInventoryTransaction transaction;
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
    transaction.mFromPos = player.getPosition();
    // Click point chosen by the orientation search so the server reproduces the
    // ghost's placement state (which face/half it resolves to).
    transaction.mClickPos = target.clickPos;
    transaction.mClientPredictedResult = ItemUseInventoryTransaction::PredictedResult::Success;
    transaction.mClientCooldownState = ItemUseInventoryTransaction::ClientCooldownState::Off;
    transaction.setTargetBlock(region.getBlock(target.at));
    transaction.setSelectedItem(item);
    // The server's stack-net-id system expects the item descriptor to carry
    // the stack net id. With the flag off the writer omits the net id bytes,
    // the server misaligns the stream while reading and silently drops the
    // packet before the transaction is ever validated.
    transaction.mItem.get().setIncludeNetIds(true);

    InventoryTransactionPacket packet(
        std::make_unique<ItemUseInventoryTransaction>(transaction),
        true
    );
    player.getClientInstance().getPacketSender().sendToServer(packet);

    // logger().info(
    //     "[place] target=({},{},{}) mPos=({},{},{}) mFace={}",
    //     target.cell.x, target.cell.y, target.cell.z,
    //     transaction.mPos.get().x, transaction.mPos.get().y, transaction.mPos.get().z,
    //     static_cast<int>(transaction.mFace)
    // );
    gNextPlaceAt.store(GetTickCount64() + kMinSendIntervalMs, std::memory_order_release);
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

// Orientation gate (easy-place "place only when the orientation is right"). Ask
// vanilla getPlacementBlock what block WOULD result from each reachable
// placement (support face + click point), using the player's current facing,
// and accept the first combo that reproduces the ghost exactly. Returns false
// when nothing reachable matches, so the caller leaves the cell for later rather
// than placing a wrong orientation. This reproduces the game's own computation,
// so face-mounted blocks (torches/levers/buttons) and top/bottom halves are
// solved automatically; rotational blocks (stairs/pistons/...) pass only while
// the player looks the right way.
// Centre of the face of `cell` shared with the support in direction `sf`.
Vec3 sharedFaceCenter(BlockPos const& cell, uchar sf) {
    float const cx = static_cast<float>(cell.x);
    float const cy = static_cast<float>(cell.y);
    float const cz = static_cast<float>(cell.z);
    switch (static_cast<Facing::Name>(sf)) {
    case Facing::Name::Down:  return Vec3{cx + 0.5f, cy + 0.0f, cz + 0.5f};
    case Facing::Name::Up:    return Vec3{cx + 0.5f, cy + 1.0f, cz + 0.5f};
    case Facing::Name::North: return Vec3{cx + 0.5f, cy + 0.5f, cz + 0.0f};
    case Facing::Name::South: return Vec3{cx + 0.5f, cy + 0.5f, cz + 1.0f};
    case Facing::Name::West:  return Vec3{cx + 0.0f, cy + 0.5f, cz + 0.5f};
    case Facing::Name::East:  return Vec3{cx + 1.0f, cy + 0.5f, cz + 0.5f};
    default:                  return Vec3{cx + 0.5f, cy + 0.5f, cz + 0.5f};
    }
}

// Read one serialized state of a block as a string ("" if absent).
std::string serializedState(Block const& block, char const* key) {
    for (auto const& [k, v] : block.getSerializationId()) {
        if (k != "states") continue;
        if (!v.hold<::CompoundTag>()) break;
        for (auto const& [stateKey, stateValue] : v.get<::CompoundTag>()) {
            if (stateKey != key) continue;
            switch (stateValue.getId()) {
            case ::Tag::Type::Byte:   return std::to_string(static_cast<int>(stateValue.get<::ByteTag>().data));
            case ::Tag::Type::Int:    return std::to_string(stateValue.get<::IntTag>().data);
            case ::Tag::Type::String: return static_cast<std::string const&>(stateValue.get<::StringTag>());
            default:                  return {};
            }
        }
        break;
    }
    return {};
}

// Blocks whose final state comes from neighbours (rail curves auto-connect) or
// is purely cosmetic-after-the-fact. The placed block can never equal the ghost
// exactly, so easy-place gates these on the block type only.
bool isTypeOnlyGate(Block const& block) {
    auto const& name = block.getTypeName();
    return name == "minecraft:rail" || name == "minecraft:golden_rail"
        || name == "minecraft:detector_rail" || name == "minecraft:activator_rail"
        // Redstone dust connections form from neighbours, so the placed cross/dot
        // never equals the ghost's connection state.
        || name == "minecraft:redstone_wire";
}

// The block's single facing value (prefixed by which state carries it, so a
// predicted/ghost comparison uses the same one). Empty if the block has no
// facing state.
std::string facingValue(Block const& block) {
    std::string v = serializedState(block, "facing_direction");
    if (!v.empty()) return "fd:" + v;
    v = serializedState(block, "minecraft:cardinal_direction");
    if (!v.empty()) return "cd:" + v;
    v = serializedState(block, "minecraft:facing_direction");
    if (!v.empty()) return "mf:" + v;
    return {};
}

// Blocks whose only relevant orientation is a single facing (no top/bottom half,
// hinge, or stair shape): dispensers, droppers, observers, pistons, comparators,
// repeaters, chests, furnaces, ... They gate on facing alone; other states
// (triggered_bit, subtract mode, delay, lit) are set or derived after placement,
// so an exact match would fail. Detected from the block's own states.
bool isFacingGate(Block const& block) {
    bool hasFacing = false;
    for (auto const& [k, v] : block.getSerializationId()) {
        if (k != "states") continue;
        if (!v.hold<::CompoundTag>()) break;
        for (auto const& [stateKey, stateValue] : v.get<::CompoundTag>()) {
            if (stateKey == "facing_direction" || stateKey == "minecraft:cardinal_direction"
                || stateKey == "minecraft:facing_direction") {
                hasFacing = true;
            } else if (stateKey == "upside_down_bit" || stateKey == "upper_block_bit"
                || stateKey == "minecraft:vertical_half" || stateKey == "top_slot_bit"
                || stateKey == "open_bit" || stateKey == "door_hinge_bit"
                || stateKey == "weirdo_direction") {
                return false;  // has half/hinge/stair shape -> needs the exact gate
            }
        }
        break;
    }
    return hasFacing;
}

bool resolveOrientedPlacement(
    LocalPlayer& player, BlockSource& region, BlockPos const& cell,
    Block const& ghost, int itemAux, bool requireExact, ProjectionTarget& out
) {
    bool const typeOnly = isTypeOnlyGate(ghost);

    // Exact placement: find the support face + click point whose predicted block
    // reproduces the ghost (orientation and all). Uses getPlacementBlock, which
    // is reliable for the orientation-critical full blocks (stairs, slabs,
    // pistons, ...) this path is meant for.
    auto const searchExact = [&]() -> bool {
        auto const tryPlacement = [&](BlockPos const& at, uchar face, Vec3 const& clickPos) {
            Block const& predicted = ghost.getPlacementBlock(player, cell, face, clickPos, itemAux);
            if (!(predicted == ghost)) return false;
            out = ProjectionTarget{cell, at, face, &ghost, clickPos};
            return true;
        };
        for (uchar sf = 0; sf < 6; ++sf) {
            BlockPos const at = cell.neighbor(sf);
            if (region.getBlock(at).isAir()) continue;
            uchar const face = Facing::getOpposite(sf);
            bool matched = false;
            forEachClickCandidate(cell, sf, [&](Vec3 const& clickPos) {
                if (!matched && tryPlacement(at, face, clickPos)) matched = true;
            });
            if (matched) return true;
        }
        float const cx = static_cast<float>(cell.x);
        float const cy = static_cast<float>(cell.y);
        float const cz = static_cast<float>(cell.z);
        for (uchar face = 0; face < 6; ++face) {
            if (tryPlacement(cell, face, Vec3{cx + 0.5f, cy + 0.25f, cz + 0.5f})) return true;
            if (tryPlacement(cell, face, Vec3{cx + 0.5f, cy + 0.75f, cz + 0.5f})) return true;
        }
        return false;
    };

    // Best-effort placement: pick the first real solid support (the block below
    // is preferred) and let the server resolve the block. getPlacementBlock is
    // NOT consulted here -- it returns air/defaults for several redstone
    // components (comparator, redstone dust, hopper, ...), which would otherwise
    // make them impossible to place.
    auto const searchAnySupport = [&]() -> bool {
        for (uchar sf = 0; sf < 6; ++sf) {
            BlockPos const at = cell.neighbor(sf);
            if (region.getBlock(at).isAir()) continue;
            out = ProjectionTarget{cell, at, Facing::getOpposite(sf), &ghost, sharedFaceCenter(cell, sf)};
            return true;
        }
        return false;
    };

    // Type-only families never orient exactly; place them on any real support.
    if (typeOnly) return searchAnySupport();

    // Comparators/repeaters: gate on facing only (mode/delay are toggled after).
    // Facing comes from the player's view, so one support is enough to read the
    // predicted facing. If getPlacementBlock cannot produce the block at all,
    // fall back to placing it rather than getting stuck.
    if (isFacingGate(ghost)) {
        std::string const wantFacing = facingValue(ghost);
        for (uchar sf = 0; sf < 6; ++sf) {
            BlockPos const at = cell.neighbor(sf);
            if (region.getBlock(at).isAir()) continue;
            uchar const face = Facing::getOpposite(sf);
            Vec3 const cp = sharedFaceCenter(cell, sf);
            Block const& predicted = ghost.getPlacementBlock(player, cell, face, cp, itemAux);
            if (predicted.getTypeName() != ghost.getTypeName()) continue;
            if (facingValue(predicted) != wantFacing) return false;  // wrong facing
            out = ProjectionTarget{cell, at, face, &ghost, cp};
            return true;
        }
        return searchAnySupport();
    }

    // Orientation-critical blocks: prefer an exact match. Manual mode
    // (requireExact == false) still places best-effort so a deliberate
    // right-click is never a no-op.
    if (searchExact()) return true;
    if (!requireExact) return searchAnySupport();
    return false;
}

void tickRangePlace(LocalPlayer& player) {
    auto const now = GetTickCount64();
    Vec3 const center = player.getPosition();
    float const radius = static_cast<float>(gPlacementRadius.load(std::memory_order_relaxed));
    auto& region = player.getDimensionBlockSource();

    auto candidates = projection::queryMissingCellsInRange(center, radius);
    for (auto const& cand : candidates) {
        BlockPos const cell{cand.x, cand.y, cand.z};

        // Skip cells placed a moment ago until the server applies them, so a
        // cell is never placed twice mid-round-trip (the slab double-place).
        if (recentlyPlaced(cell, now)) continue;

        // Orientation gate: only place when the resulting block would match the
        // ghost. The search also picks the support face / click point, replacing
        // the old approach-based support selection.
        ProjectionTarget target;
        if (!resolveOrientedPlacement(player, region, cell, *cand.block, 0, true, target)) continue;

        auto const found = findItemSlot(player, *cand.block);
        if (found.slot < 0) continue;

        if (found.slot >= kHotbarSlots) {
            // Back off a rejected swap; see sendInventorySwap and the same
            // logic in tickEasyPlace.
            if (now < gNextSwapAt.load(std::memory_order_acquire)) continue;
            auto& inventory = player.getInventory();
            int const hotbarSlot = player.getSelectedItemSlot();
            auto const& toItem = inventory.getItem(hotbarSlot);
            sendInventorySwap(player, found.slot, hotbarSlot, *found.item, toItem);
            gNextSwapAt.store(now + kSwapRetryMs, std::memory_order_release);
            return;
        }
        player.setSelectedSlot(found.slot);
        markPlaced(cell, now);
        placeBlock(player, target, found.slot, *found.item);
        return;
    }
}

void tickEasyPlace() {
    auto client = ll::service::getClientInstance();
    if (!client) {
        updateAimedBlockEntityName(nullptr);
        return;
    }
    auto* player = client->getLocalPlayer();
    if (!player) {
        updateAimedBlockEntityName(nullptr);
        return;
    }
    // Only act during gameplay: menus, pause screens and the LHolo GUI itself
    // disable in-game input.
    if (!client->isInGameInputEnabled() || structure::isGuiVisible()) {
        updateAimedBlockEntityName(nullptr);
        return;
    }

    // Ray from the camera eye along the view direction against the projection.
    Vec3 const origin = player->getEyePos();
    Vec3 const rawDir = player->getViewVector(1.0f);
    float const length = std::sqrt(rawDir.x * rawDir.x + rawDir.y * rawDir.y + rawDir.z * rawDir.z);
    if (length <= 0.0f) {
        updateAimedBlockEntityName(nullptr);
        return;
    }
    Vec3 const dir{rawDir.x / length, rawDir.y / length, rawDir.z / length};

    auto target = findProjectionTarget(*player, origin, dir, player->getPickRange());
    // Keep the HUD informed even when easy-place is disabled.
    updateAimedBlockEntityName(target ? target->block : nullptr);

    if (!gEnabled.load(std::memory_order_acquire)
        && !gManualMode.load(std::memory_order_acquire)
        && !gRangeEnabled.load(std::memory_order_acquire)) {
        return;
    }
    if (GetTickCount64() < gNextPlaceAt.load(std::memory_order_acquire)) return;

    // Range placement scans everything within the configured radius.
    if (gRangeEnabled.load(std::memory_order_acquire)) {
        tickRangePlace(*player);
        return;
    }

    // Single-crosshair placement: auto (轻松放置) or manual (手动放置), which are
    // mutually exclusive in the UI. Manual mode places exactly one block per
    // right-click press: startBuildBlock sets a one-shot request (consumed by
    // the placement below); the buildBlock hook cancels the vanilla build so
    // nothing is placed twice.
    if (gManualMode.load(std::memory_order_acquire)) {
        auto const nowManual = GetTickCount64();
        bool allowed = false;
        // First block of a press: place it even if the button was already
        // released (a quick tap), until the request goes stale.
        if (gManualPlaceRequested.load(std::memory_order_acquire)) {
            if (nowManual - gManualPressAt.load(std::memory_order_acquire) <= kManualRequestTimeoutMs) {
                allowed = true;
            } else {
                gManualPlaceRequested.store(false, std::memory_order_release);
            }
        }
        // While the button stays held, pause for the initial delay and then
        // repeat at a steady rate (typematic).
        if (!allowed && gManualHeld.load(std::memory_order_acquire)
            && nowManual - gManualPressAt.load(std::memory_order_acquire) >= kManualInitialDelayMs
            && nowManual - gLastManualPlaceAt.load(std::memory_order_acquire) >= kManualRepeatIntervalMs) {
            allowed = true;
        }
        if (!allowed) return;
    }
    if (!target) return;

    // Skip cells placed a moment ago until the server applies them; new cells
    // place immediately.
    auto const now = GetTickCount64();
    if (recentlyPlaced(target->cell, now)) return;

    // Orientation gate: only place when the resulting block would match the
    // ghost. The search picks the click point / support face that reproduces
    // it; rotational blocks pass only while the player looks the right way.
    auto& region = player->getDimensionBlockSource();
    ProjectionTarget placement;
    // Orientation is angle-gated in both modes: an orientable block places only
    // when the current view would produce the ghost's facing (like pistons).
    // Type-only families (rails, redstone, comparators, ...) bypass this.
    bool const requireExact = true;
    bool const resolved =
        resolveOrientedPlacement(*player, region, target->cell, *target->block, 0, requireExact, placement);
    auto const found = resolved ? findItemSlot(*player, *target->block) : ItemFind{-1, nullptr};
    if (!resolved || found.slot < 0) return;

    if (found.slot >= kHotbarSlots) {
        // Back off a rejected swap so it never retries more often than
        // kSwapRetryMs.
        auto const now = GetTickCount64();
        if (now < gNextSwapAt.load(std::memory_order_acquire)) return;
        // The server only accepts placements from the selected hotbar slot.
        // Swap the backpack item into the currently selected slot through a
        // server-synced NormalTransaction. Do not place in the same tick: the
        // server's item-stack-net bookkeeping lags the legacy swap, so an
        // immediate placement can be rejected and then re-throttled by the cell
        // lock. The next tick finds the item in the hotbar and places via the
        // single-packet fast path.
        auto& inventory = player->getInventory();
        int const hotbarSlot = player->getSelectedItemSlot();
        auto const& toItem = inventory.getItem(hotbarSlot);
        sendInventorySwap(*player, found.slot, hotbarSlot, *found.item, toItem);
        gNextSwapAt.store(now + kSwapRetryMs, std::memory_order_release);
        return;
    }
    player->setSelectedSlot(found.slot);
    markPlaced(placement.cell, now);
    placeBlock(*player, placement, found.slot, *found.item);
    // Manual repeat bookkeeping: record this placement and mark the current
    // press as having placed its first block (so holding then auto-repeats).
    gLastManualPlaceAt.store(now, std::memory_order_release);
    gManualPlaceRequested.store(false, std::memory_order_release);
}

LL_TYPE_INSTANCE_HOOK(
    LocalPlayerEasyPlaceHook,
    ll::memory::HookPriority::Normal,
    LocalPlayer,
    &LocalPlayer::$tickWorld,
    void,
    ::Tick const& currentTick
) {
    tickEasyPlace();
    origin(currentTick);
}

// Returns true when manual mode is on and `gm` belongs to the local player, i.e.
// this is the client-side right-click we should take over. The local-player
// check is essential: the server processes LHolo's own placement through these
// same functions on the ServerPlayer, and that must not be suppressed.
bool isLocalManualBuild(GameMode& gm) {
    if (!gManualMode.load(std::memory_order_acquire)) return false;
    auto client = ll::service::getClientInstance();
    auto* localPlayer = client ? client->getLocalPlayer() : nullptr;
    return localPlayer && &gm.mPlayer == static_cast<Player*>(localPlayer);
}

// Manual-mode press edge: the initial right-click. Begin a held sequence (first
// block placed immediately by tickEasyPlace, then typematic repeat) and cancel
// the vanilla build start so nothing is placed twice.
LL_TYPE_INSTANCE_HOOK(
    GameModeStartBuildHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$startBuildBlock,
    void,
    ::BlockPos const& pos,
    uchar             face
) {
    if (isLocalManualBuild(*this)) {
        gManualPressAt.store(GetTickCount64(), std::memory_order_release);
        gManualPlaceRequested.store(true, std::memory_order_release);
        gManualHeld.store(true, std::memory_order_release);
        return;  // LHolo handles the placement from tickEasyPlace.
    }
    origin(pos, face);
}

// Manual-mode release edge: stop the typematic repeat when the button is let go.
LL_TYPE_INSTANCE_HOOK(
    GameModeStopBuildHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$stopBuildBlock,
    void
) {
    if (isLocalManualBuild(*this)) {
        gManualHeld.store(false, std::memory_order_release);
        return;
    }
    origin();
}

// Suppress the vanilla continuous build while the button is held in manual mode;
// LHolo drives placement from the press/hold state above.
LL_TYPE_INSTANCE_HOOK(
    GameModeBuildBlockHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$buildBlock,
    bool,
    ::BlockPos const& pos,
    uchar             face,
    bool const        isSimTick
) {
    if (isLocalManualBuild(*this)) return false;
    return origin(pos, face, isSimTick);
}

} // namespace

void setEnabled(bool enabled) {
    if (enabled) {
        logger().info("Easy-place enabled");
    } else {
        logger().info("Easy-place disabled");
    }
    gEnabled.store(enabled, std::memory_order_release);
}

bool isEnabled() {
    return gEnabled.load(std::memory_order_acquire);
}

void setRangeEnabled(bool enabled) {
    if (enabled) {
        logger().info("Range placement enabled (radius {})", gPlacementRadius.load(std::memory_order_relaxed));
    } else {
        logger().info("Range placement disabled");
    }
    gRangeEnabled.store(enabled, std::memory_order_release);
}

bool isRangeEnabled() {
    return gRangeEnabled.load(std::memory_order_acquire);
}

void setPlacementRadius(int radius) {
    gPlacementRadius.store(std::clamp(radius, 1, 4), std::memory_order_release);
}

int getPlacementRadius() {
    return gPlacementRadius.load(std::memory_order_relaxed);
}

void setManualMode(bool manual) {
    gManualMode.store(manual, std::memory_order_release);
}

bool isManualMode() {
    return gManualMode.load(std::memory_order_acquire);
}

std::string getAimedBlockEntityName() {
    std::lock_guard lock(gAimedNameMutex);
    return gAimedBlockEntityName;
}

bool installHook() {
    if (LocalPlayerEasyPlaceHook::hook() < 0) {
        logger().error("Failed to install easy-place tick hook");
        return false;
    }
    if (GameModeStartBuildHook::hook() < 0) {
        logger().warn("Failed to install manual-place start hook; manual mode will be unavailable");
    }
    if (GameModeStopBuildHook::hook() < 0) {
        logger().warn("Failed to install manual-place stop hook; manual mode may keep repeating");
    }
    if (GameModeBuildBlockHook::hook() < 0) {
        logger().warn("Failed to install manual-place build hook; manual mode may double-place");
    }
    return true;
}

void uninstallHook() {
    GameModeBuildBlockHook::unhook();
    GameModeStopBuildHook::unhook();
    GameModeStartBuildHook::unhook();
    LocalPlayerEasyPlaceHook::unhook();
}

} // namespace lholo::place
