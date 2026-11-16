#include "place/local/LocalPlacement.h"
#include "place/AimedPlacementTarget.h"
#include "place/local/LocalPlacementUse.h"
#include "place/local/ShulkerRestock.h"

#include "block/BlockPlacementRules.h"
#include "place/PlacementState.h"
#include "plugin/LHolo.h"
#include "projection/Projection.h"
#include "structure/StructureFileLoader.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/mod/NativeMod.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/util/BlockUtils.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/SlabBlock.h"
#include "mc/world/level/block/VanillaStates.h"
#include "mc/world/level/material/Material.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace lholo::place::local {
namespace {

struct Request {
    mce::UUID player;
    std::uint64_t worldRevision;
    std::uint64_t cancellation;
    int dimension;
    LocalPlacementMode mode;
    float reach;
    std::uint64_t sampledAt;
    Vec3 center;
    std::vector<projection::RangeCandidate> candidates;
    std::optional<std::uint64_t> manualPress;
};

ll::event::ListenerPtr listener;
std::mutex executionMutex;
std::mutex pendingMutex;
std::shared_ptr<Request const> pending;
std::atomic_bool installed{};
std::atomic<LocalPlacementMode> activeMode{LocalPlacementMode::Off};
std::atomic_uint64_t cancellation{};

auto& state() { return detail::PlacementState::getInstance(); }

enum class Diagnostic { Unsupported, NoMaterial, IncompletePair, NoFace, Rejected, FloatingRejected, UnmatchedWrite };

void report(Diagnostic reason, BlockPos const& cell, Block const& target, std::uint64_t now) {
    constexpr std::array messages{"unsupported placement", "material missing", "vertical pair unavailable",
        "no clickable face", "vanilla use rejected", "vanilla use rejected floating target",
        "native write did not match projection"};
    static std::array<std::uint64_t, messages.size()> nextLog{};
    auto const index = static_cast<std::size_t>(reason);
    if (now < nextLog[index]) return;
    nextLog[index] = now + 3000;
    LHolo::getInstance().getSelf().getLogger().info(
        "[LocalRange] {}: {} at ({}, {}, {})", messages[index], target.getTypeName(), cell.x, cell.y, cell.z);
}

template <class UseMaterial>
auto withPlacementMaterial(Block const& target, UseMaterial useMaterial) {
    auto const& type = target.getBlockType();
    if (type.mMaterial->mLiquid) {
        auto const key = block::materialKey(target.getTypeName());
        ItemStack material;
        if (key == "minecraft:lava") material.reinit("minecraft:lava_bucket", 1, 0);
        else if (key == "minecraft:water") material.reinit("minecraft:water_bucket", 1, 0);
        return useMaterial(material);
    }
    return useMaterial(type.asItemInstance(target, nullptr));
}

int materialSlot(Inventory const& inventory, Block const& target) {
    return withPlacementMaterial(target, [&](ItemStackBase const& material) {
        if (!block::hasUsableItem(material)) return -1;
        for (int slot = 0; slot < 36; ++slot) {
            auto const& stack = inventory.getItem(slot);
            if (block::hasUsableItem(stack) && stack.getIdAux() == material.getIdAux()) return slot;
        }
        return -1;
    });
}

struct MaterialLookup {
    int slot{-1};
    bool checkedRestock{};
};

bool overlapsPlayer(Vec3 const& corner, AABB const& player) {
    return corner.x < player.max.x && corner.x + 1.0f > player.min.x
        && corner.y < player.max.y && corner.y + 1.0f > player.min.y
        && corner.z < player.max.z && corner.z + 1.0f > player.min.z;
}

bool requestCurrent(Request const& request) {
    return request.mode == activeMode.load() && request.cancellation == cancellation.load()
        && request.worldRevision == structure::detail::structureFileWorldRevision();
}

bool placementDue(Request const& request) {
    return requestCurrent(request) && (!request.manualPress
        || state().dueManualPress(GetTickCount64()) == request.manualPress);
}

void placeOne(Request const& request, Player& player, std::optional<projection::RangeCandidate>& lastCandidate) {
    if (!placementDue(request) || static_cast<int>(player.getDimensionId()) != request.dimension) return;
    auto& region = player.getDimension().getBlockSourceFromMainChunkSource();
    auto& inventory = player.getInventory();
    auto const& position = player.getPosition();
    bool const aimed = request.mode != LocalPlacementMode::Range;
    auto const eye = player.getEyePos();
    auto const& playerBox = player.getAABB();
    auto const radius = static_cast<float>(state().localRadius());
    auto const now = GetTickCount64();
    auto const shulkerRestock = state().localShulkerRestock();
    auto const creativeRestock = state().localCreativeRestock() && player.getPlayerGameType() == GameType::Creative;
    auto const overwrite = creativeRestock && state().localCreativeRestockOverwrite();
    std::unordered_map<Block const*, MaterialLookup> materials;
    bool const suppressionsActive = !request.manualPress && state().autoPlacementSuppressionsActive(now);
    auto const canPlaceAt = [&](BlockPos const& cell) {
        Vec3 const corner{static_cast<float>(cell.x), static_cast<float>(cell.y), static_cast<float>(cell.z)};
        Vec3 const delta = corner + Vec3{0.5f, 0.5f, 0.5f} - position;
        return (aimed || delta.x * delta.x + delta.y * delta.y + delta.z * delta.z <= radius * radius)
            && !overlapsPlayer(corner, playerBox)
            && (!suppressionsActive || !state().autoPlacementSuppressed(detail::packPlacementCell(cell.x, cell.y, cell.z), now))
            && cell.y >= region.getMinHeight() && cell.y < region.getMaxHeight()
            && region.areChunksFullyLoaded(cell, cell) && region.getExtraBlock(cell).isAir();
    };
    constexpr std::size_t candidatesPerTick = 128;
    int rejected = 0;
    auto const& candidates = request.candidates;
    auto start = candidates.begin();
    if (!aimed && lastCandidate) {
        // A refreshed list may have already removed the cell we just placed.
        start = std::upper_bound(start, candidates.end(), *lastCandidate, projection::RangeCandidateOrder{request.center});
        if (start == candidates.end()) start = candidates.begin();
    }
    auto const count = std::min(candidatesPerTick, static_cast<std::size_t>(candidates.end() - start));
    auto const end = start + count;
    for (auto it = start; it != end; ++it) {
        auto const& candidate = *it;
        if (!aimed) lastCandidate = candidate;
        BlockPos const cell{candidate.x, candidate.y, candidate.z};
        if (!canPlaceAt(cell)) continue;
        if (aimed) {
            auto const axisDistance = [](float eye, int cell) {
                float const low = static_cast<float>(cell);
                return std::max({low - eye, eye - (low + 1.0f), 0.0f});
            };
            float const dx = axisDistance(eye.x, cell.x);
            float const dy = axisDistance(eye.y, cell.y);
            float const dz = axisDistance(eye.z, cell.z);
            if (dx * dx + dy * dy + dz * dz > request.reach * request.reach) continue;
        }
        auto const& target = *candidate.block;
        auto const& type = target.getBlockType();
        bool const liquid = type.mMaterial->mLiquid;
        if (liquid && state().localLiquidsDisabled()) continue;
        auto const& actual = region.getBlock(cell);
        Block const* placement = &target;
        Block const* materialBlock = &target;
        bool mergingSlab = false;
        if (block::isDoubleSlab(target)) {
            auto const single = Block::tryGetFromRegistry(static_cast<SlabBlock const&>(type).mOtherSlab);
            if (!single) {
                report(Diagnostic::Unsupported, cell, target, now);
                continue;
            }
            materialBlock = &*single;
            if (actual.isAir()) {
                placement = materialBlock;
            } else {
                if (&actual.getBlockType() != &single->getBlockType()
                    || actual.getBlockType().asItemInstance(actual, nullptr).getIdAux()
                        != single->getBlockType().asItemInstance(*single, nullptr).getIdAux()) continue;
                mergingSlab = true;
            }
        } else if (!actual.isAir() && !(liquid && block::sameLiquid(target, actual) && !BlockUtils::isLiquidSource(actual))) {
            continue;
        }
        auto const verticalHalf = target.getState<bool>(VanillaStates::UpperBlockBit());
        if (verticalHalf && *verticalHalf) continue;
        if (candidate.hasLiquid || (!verticalHalf && block::placementPart(target).type)) {
            report(Diagnostic::Unsupported, cell, target, now);
            continue;
        }
        auto [foundMaterial, inserted] = materials.try_emplace(materialBlock);
        auto& material = foundMaterial->second;
        if (inserted) material.slot = materialSlot(inventory, *materialBlock);
        if (material.slot < 0 && (material.checkedRestock || (!creativeRestock && !shulkerRestock))) {
            report(Diagnostic::NoMaterial, cell, target, now);
            continue;
        }
        auto const current = projection::queryCurrentPlacementCandidate(cell, request.dimension);
        if (!current || current->block != candidate.block || current->hasLiquid) continue;
        Block const* upper = nullptr;
        if (verticalHalf) {
            auto const above = cell + BlockPos{0, 1, 0};
            auto const pair = projection::queryCurrentPlacementCandidate(above, request.dimension);
            if (!pair || pair->hasLiquid || &pair->block->getBlockType() != &type
                || !pair->block->getState<bool>(VanillaStates::UpperBlockBit()).value_or(false)
                || !canPlaceAt(above) || !region.getBlock(above).isAir()) {
                report(Diagnostic::IncompletePair, cell, target, now);
                continue;
            }
            upper = pair->block;
        }
        if (!placementDue(request)) return;
        std::optional<PlacementRestock> restock;
        if (material.slot < 0) {
            int destination = findRestockSlot(inventory);
            if (destination < 0 && overwrite) destination = player.getSelectedItemSlot();
            if (destination >= 0) {
                restock = withPlacementMaterial(*materialBlock, [&](ItemStackBase const& item) -> std::optional<PlacementRestock> {
                    if (!block::hasUsableItem(item)) return std::nullopt;
                    if (creativeRestock) return prepareCreativeRestock(player, item, destination);
                    return prepareShulkerRestock(player, item, destination);
                });
            }
            if (!restock) {
                material.checkedRestock = true;
                report(Diagnostic::NoMaterial, cell, target, now);
                continue;
            }
        }
        int const slot = restock ? restock->materialSlot : material.slot;
        if (!placementDue(request)) return;
        auto const outcome = usePlacementItem(player, region, cell, *placement, upper, slot, mergingSlab, restock ? &*restock : nullptr);
        if (request.manualPress) state().recordManualAttempt(*request.manualPress, GetTickCount64());
        switch (outcome) {
        case PlacementUseResult::NoFace:
            report(Diagnostic::NoFace, cell, target, now);
            break;
        case PlacementUseResult::Rejected:
            report(Diagnostic::Rejected, cell, target, now);
            if (++rejected >= 4) return;
            break;
        case PlacementUseResult::FloatingRejected:
            report(Diagnostic::FloatingRejected, cell, target, now);
            if (++rejected >= 4) return;
            break;
        case PlacementUseResult::UnmatchedWrite:
            report(Diagnostic::UnmatchedWrite, cell, target, now);
            return;
        case PlacementUseResult::Placed:
            return;
        }
    }
}

void onServerTick(ll::event::ServerLevelTickEvent& event) {
    std::lock_guard execution(executionMutex);
    if (!installed.load()) return;
    std::shared_ptr<Request const> request;
    {
        std::lock_guard lock(pendingMutex);
        request = pending;
        if (request && request->mode != LocalPlacementMode::Range) pending.reset();
    }
    static std::optional<projection::RangeCandidate> lastCandidate;
    static std::tuple<std::uint64_t, std::uint64_t, int> context{};
    if (!request) {
        lastCandidate.reset();
        return;
    }
    auto const currentContext = std::tuple{request->worldRevision, request->cancellation, request->dimension};
    if (currentContext != context) {
        context = currentContext;
        lastCandidate.reset();
    }
    if (auto* player = event.level().getPlayer(request->player)) placeOne(*request, *player, lastCandidate);
}

} // namespace

void install() {
    if (!installPlacementOverride()) return;
    listener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>(onServerTick);
    if (listener) installed.store(true);
    else uninstallPlacementOverride();
}

void uninstall() {
    if (!installed.exchange(false)) return;
    ll::event::EventBus::getInstance().removeListener(listener);
    listener.reset();
    std::lock_guard execution(executionMutex);
    setMode(LocalPlacementMode::Off);
    uninstallPlacementOverride();
}

bool available() {
    return installed.load() && ll::service::getServerInstance().has_value();
}

LocalPlacementMode mode() { return activeMode.load(); }

void setMode(LocalPlacementMode value) {
    auto const previous = activeMode.exchange(value);
    if (previous == value) return;
    if (previous == LocalPlacementMode::Manual || value == LocalPlacementMode::Manual) state().resetManualInput();
    cancelPending();
}

void cancelPending() {
    std::lock_guard lock(pendingMutex);
    ++cancellation;
    pending.reset();
}

void submitCandidates(LocalPlayer& player) {
    auto const currentMode = mode();
    if (currentMode == LocalPlacementMode::Off) return;
    auto const client = ll::service::getClientInstance();
    if (!available() || !client || !client->isInGameInputEnabled()) {
        if (currentMode == LocalPlacementMode::Manual) state().cancelManualPress();
        cancelPending();
        return;
    }
    auto const now = GetTickCount64();
    auto const manualPress = currentMode == LocalPlacementMode::Manual ? state().dueManualPress(now) : std::nullopt;
    if (currentMode == LocalPlacementMode::Manual && !manualPress) return;
    auto const radius = state().localRadius();
    auto const dimension = static_cast<int>(player.getDimensionId());
    if (currentMode == LocalPlacementMode::Range && radius > 4) {
        std::lock_guard lock(pendingMutex);
        if (pending && pending->dimension == dimension && requestCurrent(*pending)
            && now - pending->sampledAt < 250) return;
    }
    Request request{player.getUuid(), structure::detail::structureFileWorldRevision(), cancellation.load(),
        dimension, currentMode, player.getPickRange(), now, player.getPosition(), {}, manualPress};
    if (currentMode != LocalPlacementMode::Range) {
        if (auto const target = findAimedPlacementTarget(player, true)) {
            auto const& cell = target->cell;
            request.candidates.push_back({cell.x, cell.y, cell.z, target->block, target->hasLiquid});
        }
    } else {
        request.candidates = projection::queryMissingCellsInRange(player, request.center, static_cast<float>(radius), true);
    }
    std::lock_guard lock(pendingMutex);
    if (!requestCurrent(request)) return;
    pending = std::make_shared<Request const>(std::move(request));
}

} // namespace lholo::place::local
