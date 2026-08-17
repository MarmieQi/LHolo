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

#include "projection/Projection.h"

#include "plugin/LHolo.h"
#include "overlay/ImGuiOverlay.h"
#include "structure/StructureLoader.h"

#include <atomic>
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <memory>
#include <map>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/client/renderer/block/BlockTessellator.h"
#include "mc/client/renderer/block/BlockGraphics.h"
#include "mc/client/renderer/game/ItemInHandRenderer.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/client/renderer/texture/TextureUVCoordinateSet.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"
#include "mc/deps/minecraft_renderer/renderer/IsMissingTexture.h"
#include "mc/deps/minecraft_renderer/renderer/RenderMaterial.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/network/LoopbackPacketSender.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/Packet.h"
#include "mc/network/packet/TextPacket.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/deps/nbt/Tag.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/Facing.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockRenderLayer.h"
#include "mc/world/level/block/VanillaStates.h"
#include "mc/world/level/block/states/VanillaBlockStateTransformUtils.h"
#include "mc/world/level/material/Material.h"
#include "mc/util/Mirror.h"
#include "mc/util/Rotation.h"

#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"

namespace lholo::projection {
namespace {

// Litematica's default schematic overlay palette, converted from ARGB to the
// ABGR byte order expected by Tessellator::colorABGR(). Overlay edges are
// intentionally opaque while the cell faces retain their original alpha.
constexpr std::uint32_t MissingColorAbgrRgb    = 0x00E6B333U; // #33B3E6
constexpr std::uint32_t WrongBlockColorAbgrRgb = 0x003333FFU; // #FF3333
constexpr std::uint32_t WrongStateColorAbgrRgb = 0x001090FFU; // #FF9010

// Liquid proxy rendering: the hulls reuse the vanilla terrain-atlas water and
// lava tiles. Water tiles are untinted, so their vertex color carries the
// vanilla water blue #3F76E4; lava keeps its own texture colors with a white
// vertex tint. Bedrock's animated water surface lives in a terrain-water pass
// an injected mesh cannot reach; the textured proxy is the accepted static
// approximation, drawn through the exact same material path as glass.
constexpr std::uint32_t LiquidWaterTintAbgrRgb = 0x00E4763FU; // #3F76E4
constexpr std::uint32_t LiquidLavaTintAbgrRgb  = 0x00FFFFFFU; // white

std::uint32_t withAlpha(std::uint32_t colorAbgrRgb, float opacity) {
    auto const alpha = static_cast<std::uint32_t>(std::lround(std::clamp(opacity, 0.0f, 1.0f) * 255.0f));
    return colorAbgrRgb | (alpha << 24U);
}

using TextureVariant =
    std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture>;

auto& logger() {
    return LHolo::getInstance().getSelf().getLogger();
}

struct ProjectionState {
    enum class CorrectionState : uchar { Unknown, Missing, Correct, WrongType, WrongState };
    enum class RenderBucket : uchar { Opaque, Alpha, AlphaOneSided, Blend, Count };
    bool                         enabled{};
    BlockPos                     anchor{};
    IClientInstance*             client{};
    Level*                       level{};
    Dimension*                   dimension{};
    std::optional<mce::TexturePtr> terrainTexture;
    std::optional<TextureVariant> terrainTextureVariant;
    std::shared_ptr<structure::LoadedStructure const> structure;
    std::uint64_t                  structureGeneration{};
    std::unique_ptr<BlockTessellator> blockTessellator;
    std::vector<CorrectionState>   correctionStates;
    // One byte per structure block. This is updated by the existing bounded
    // correction scan, so the HUD never performs its own world-block queries.
    std::vector<uchar>             progressCorrect;
    // 0 = no placement error, 1 = wrong block type, 2 = wrong state/direction.
    // Updating one byte and two counters keeps the HUD O(1) per frame.
    std::vector<uchar>             progressErrorKind;
    std::uint64_t                  progressCorrectCount{};
    std::uint64_t                  progressWrongTypeCount{};
    std::uint64_t                  progressWrongStateCount{};
    std::size_t                    correctionScanCursor{};
    int                            cachedRotation{-1};
    int                            cachedMirror{-1};
    int                            cachedOffsetX{};
    int                            cachedOffsetY{};
    int                            cachedOffsetZ{};
    int                            cachedLayerDisplayMode{-1};
    int                            cachedDisplayLayer{-1};
    int                            cachedLayerAxis{-1};
    float                          cachedOpacity{-1.0f};
    float                          cachedCorrectionFillOpacity{-1.0f};
    float                          cachedCorrectionOutlineOpacity{-1.0f};
    std::vector<std::vector<std::size_t>> sectionBlockIndices;
    std::array<std::vector<std::unique_ptr<mce::Mesh>>, static_cast<std::size_t>(RenderBucket::Count)>
        sectionMeshes;
    std::vector<std::unique_ptr<mce::Mesh>> warningFillSectionMeshes;
    std::vector<std::unique_ptr<mce::Mesh>> correctionOutlineSectionMeshes;
    std::vector<std::unique_ptr<mce::Mesh>> liquidProxySectionMeshes;
    std::vector<std::unique_ptr<mce::Mesh>> blockEntityPlaceholderSectionMeshes;
    std::unique_ptr<mce::Mesh>              structureBoundsMesh;
    std::vector<Vec3>              sectionCenters;
    std::vector<bool>              sectionDirty;
    std::vector<std::size_t>       blockToSection;
    std::size_t                    dirtySectionCursor{};
    std::map<std::tuple<int, int, int>, Block const*> expectedWorldBlocks;
    std::map<std::tuple<int, int, int>, std::size_t>  expectedWorldBlockIndices;
};

thread_local std::map<std::tuple<int, int, int>, Block const*> const* gTessellationBlocks{};

class ScopedTessellationBlocks {
public:
    explicit ScopedTessellationBlocks(
        std::map<std::tuple<int, int, int>, Block const*> const& blocks
    ) : mPrevious(std::exchange(gTessellationBlocks, &blocks)) {}

    ~ScopedTessellationBlocks() { gTessellationBlocks = mPrevious; }

    ScopedTessellationBlocks(ScopedTessellationBlocks const&) = delete;
    ScopedTessellationBlocks& operator=(ScopedTessellationBlocks const&) = delete;

private:
    std::map<std::tuple<int, int, int>, Block const*> const* mPrevious{};
};

ProjectionState::RenderBucket renderBucketFor(BlockRenderLayer layer) {
    using Bucket = ProjectionState::RenderBucket;
    switch (layer) {
    case BlockRenderLayer::RenderlayerBlend:
    case BlockRenderLayer::RenderlayerBlendToOpaque:
        return Bucket::Blend;
    case BlockRenderLayer::RenderlayerOpaque:
    case BlockRenderLayer::RenderlayerSeasonsOpaque:
    case BlockRenderLayer::RenderlayerShiftOpaqueInternalOnly:
        return Bucket::Opaque;
    case BlockRenderLayer::RenderlayerAlphatestSingleSide:
    case BlockRenderLayer::RenderlayerAlphatestSingleSideToOpaque:
    case BlockRenderLayer::RenderlayerShiftAlphatestSingleSideInternalOnly:
    case BlockRenderLayer::RenderlayerShiftAlphatestSingleSideToOpaqueInternalOnly:
        return Bucket::AlphaOneSided;
    default:
        return Bucket::Alpha;
    }
}

std::atomic<float> gOpacity{1.0f};
std::atomic<float> gCorrectionFillOpacity{0.15f};
std::atomic<float> gCorrectionOutlineOpacity{1.0f};
std::atomic_bool   gStructureBoundsEnabled{true};
std::atomic_bool   gPendingStructureAnchor{false};
std::atomic_int    gPendingStructureAnchorX{0};
std::atomic_int    gPendingStructureAnchorY{0};
std::atomic_int    gPendingStructureAnchorZ{0};
std::atomic_uint64_t gBuildProgressPlaced{0};
std::atomic_uint64_t gBuildProgressTotal{0};
std::atomic_uint64_t gBuildProgressWrongType{0};
std::atomic_uint64_t gBuildProgressWrongState{0};
std::mutex       gStateMutex;
ProjectionState  gState;

void clearProjectionStateLocked() {
    gBuildProgressPlaced.store(0, std::memory_order_relaxed);
    gBuildProgressWrongType.store(0, std::memory_order_relaxed);
    gBuildProgressWrongState.store(0, std::memory_order_relaxed);
    gBuildProgressTotal.store(0, std::memory_order_release);
    // Withdraw nothing: liquids are drawn by LHolo's own meshes and never
    // touch the vanilla chunk pipeline.
    gState.terrainTexture.reset();
    gState.terrainTextureVariant.reset();
    gState.dimension     = nullptr;
    gState.level         = nullptr;
    gState.client        = nullptr;
    gState.anchor        = {};
    gState.enabled       = false;
    gState.structure.reset();
    gState.blockTessellator.reset();
    gState.structureGeneration = 0;
}

void clearProjectionState() {
    std::lock_guard lock(gStateMutex);
    clearProjectionStateLocked();
}

bool isMenuCommand(std::string_view message) {
    constexpr std::string_view command{"lholo"};
    if (message.size() != command.size()) return false;
    for (std::size_t index = 0; index < command.size(); ++index) {
        auto character = message[index];
        if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
        if (character != command[index]) return false;
    }
    return true;
}

bool filterProjectionPacket(Packet& packet) {
    if (packet.getId() != MinecraftPacketIds::Text) return false;
    auto& textPacket = static_cast<TextPacket&>(packet);
    if (textPacket.getType() != TextPacketType::Chat || !isMenuCommand(textPacket.getMessage())) return false;

    if (overlay::ensureInstalled()) {
        structure::requestOpenGui();
    } else {
        logger().error("Could not initialize the injected ImGui overlay");
    }
    return true;
}

bool resolveTerrainTexture(IClientInstance& client, ProjectionState& state) {
    auto* levelRenderer = client.getLevelRenderer();
    if (!levelRenderer) return false;
    auto const& atlasTexture = levelRenderer->mAtlasTexture.get();
    if (!atlasTexture || atlasTexture.isMissingTexture() == IsMissingTexture::Yes) return false;
    state.terrainTexture.emplace(atlasTexture);
    state.terrainTextureVariant.emplace(*state.terrainTexture);
    return true;
}

bool enableStructureProjection(
    BaseActorRenderContext& renderContext,
    std::shared_ptr<structure::LoadedStructure const> loaded
) {
    auto& client = renderContext.getClient();
    auto* player = client.getLocalPlayer();
    if (!player || !loaded || loaded->renderBlocks.empty()) return false;

    ProjectionState next;
    next.client = &client;
    next.level = &player->getLevel();
    next.dimension = &player->getDimension();
    next.structure = std::move(loaded);
    next.structureGeneration = next.structure->generation;
    next.blockTessellator = std::make_unique<BlockTessellator>(&player->getDimensionBlockSource());
    next.correctionStates.resize(
        next.structure->renderBlocks.size(), ProjectionState::CorrectionState::Unknown
    );
    next.progressCorrect.resize(next.structure->renderBlocks.size(), 0);
    next.progressErrorKind.resize(next.structure->renderBlocks.size(), 0);
    // Force the first render pass to build the transformed virtual-world lookup.
    next.cachedRotation = -1;
    next.cachedMirror = -1;
    std::map<std::tuple<int, int, int>, std::size_t> sectionLookup;
    next.blockToSection.resize(next.structure->renderBlocks.size());
    for (std::size_t index = 0; index < next.structure->renderBlocks.size(); ++index) {
        auto const& entry = next.structure->renderBlocks[index];
        auto const key = std::tuple{entry.x / 16, entry.y / 16, entry.z / 16};
        auto [found, inserted] = sectionLookup.try_emplace(key, next.sectionBlockIndices.size());
        if (inserted) {
            next.sectionBlockIndices.emplace_back();
            auto const [sx, sy, sz] = key;
            next.sectionCenters.emplace_back(
                static_cast<float>(sx * 16 + 8),
                static_cast<float>(sy * 16 + 8),
                static_cast<float>(sz * 16 + 8)
            );
        }
        next.blockToSection[index] = found->second;
        next.sectionBlockIndices[found->second].push_back(index);
    }
    for (auto& meshes : next.sectionMeshes) meshes.resize(next.sectionBlockIndices.size());
    next.warningFillSectionMeshes.resize(next.sectionBlockIndices.size());
    next.correctionOutlineSectionMeshes.resize(next.sectionBlockIndices.size());
    next.liquidProxySectionMeshes.resize(next.sectionBlockIndices.size());
    next.blockEntityPlaceholderSectionMeshes.resize(next.sectionBlockIndices.size());
    next.sectionDirty.assign(next.sectionBlockIndices.size(), true);
    if (!resolveTerrainTexture(client, next)) return false;
    if (gPendingStructureAnchor.exchange(false, std::memory_order_acq_rel)) {
        next.anchor = BlockPos{
            gPendingStructureAnchorX.load(std::memory_order_relaxed),
            gPendingStructureAnchorY.load(std::memory_order_relaxed),
            gPendingStructureAnchorZ.load(std::memory_order_relaxed)
        };
    } else {
        auto const& position = player->getPosition();
        next.anchor = BlockPos(
            std::floor(position.x),
            std::floor(position.y),
            std::floor(position.z)
        );
    }
    gState = std::move(next);
    gState.enabled = true;
    gBuildProgressPlaced.store(0, std::memory_order_relaxed);
    gBuildProgressWrongType.store(0, std::memory_order_relaxed);
    gBuildProgressWrongState.store(0, std::memory_order_relaxed);
    gBuildProgressTotal.store(gState.structure->renderBlocks.size(), std::memory_order_release);
    structure::recordProjectionAnchor(gState.anchor.x, gState.anchor.y, gState.anchor.z);
    logger().info(
        "Structure projection enabled: {} renderable blocks at ({}, {}, {})",
        gState.structure->renderBlocks.size(), gState.anchor.x, gState.anchor.y, gState.anchor.z
    );
    return true;
}

bool contextIsValid(IClientInstance& client, Actor* player) {
    if (!player) return false;
    return gState.client == &client && gState.level == &player->getLevel()
        && gState.dimension == &player->getDimension();
}

Mirror getProjectionMirror() {
    switch (structure::getMirrorMode()) {
    case 1: return Mirror::X;
    case 2: return Mirror::Z;
    case 3: return Mirror::Xz;
    default: return Mirror::None;
    }
}

Rotation getProjectionRotation() {
    switch (structure::getRotationQuarterTurns()) {
    case 1: return Rotation::Clockwise90;
    case 2: return Rotation::Clockwise180;
    case 3: return Rotation::CounterClockwise90;
    default: return Rotation::None;
    }
}

Block const* transformExpectedBlock(Block const* block, Rotation rotation, Mirror mirror) {
    if (!block) return nullptr;
    auto const* transformed = VanillaBlockStateTransformUtils::transformBlock(*block, rotation, mirror);
    return transformed ? transformed : block;
}

bool projectionStatesMatch(Block const& expected, Block const& actual) {
    if (expected == actual) return true;
    if (expected.getTypeName() != actual.getTypeName()) return false;

    auto stateMatches = [&](auto const& state) {
        using StateValue = typename std::remove_cvref_t<decltype(state)>::Type;
        auto const expectedValue = expected.getState<StateValue>(state);
        auto const actualValue = actual.getState<StateValue>(state);
        return expectedValue && actualValue && *expectedValue == *actualValue;
    };

    // A real door stores its placement state across two blocks: the lower half
    // owns direction/open, the upper half owns hinge. Bedrock may normalize the
    // duplicated fields differently after a structure load, so the complete
    // serialization hash can differ even for a correctly placed door. Only real
    // doors carry upper_block_bit; trapdoor names also end with "door", so the
    // presence of that state is the reliable discriminator.
    auto const expectedUpper = expected.getState<bool>(VanillaStates::UpperBlockBit());
    if (expectedUpper) {
        auto const actualUpper = actual.getState<bool>(VanillaStates::UpperBlockBit());
        if (!actualUpper || *actualUpper != *expectedUpper) return false;
        return *expectedUpper
            ? stateMatches(VanillaStates::DoorHingeBit())
            : stateMatches(VanillaStates::Direction())
                && stateMatches(VanillaStates::OpenBit());
    }

    // Trapdoors are single blocks: compare their own placement states instead
    // of treating them like a two-block door.
    auto const expectedOpen = expected.getState<bool>(VanillaStates::OpenBit());
    if (expectedOpen) {
        auto const actualOpen = actual.getState<bool>(VanillaStates::OpenBit());
        return actualOpen && *actualOpen == *expectedOpen
            && stateMatches(VanillaStates::Direction())
            && stateMatches(VanillaStates::UpsideDownBit());
    }

    return false;
}

// Front face (Facing 0-5) for a block-entity placeholder, read from whichever
// facing state the block actually carries. Chests and similar block entities
// moved from the integer facing_direction to the string
// minecraft:cardinal_direction, so both are handled. Returns -1 when the block
// has no horizontal facing.
int blockFrontFace(Block const& block) {
    for (auto const& [key, value] : block.getSerializationId()) {
        if (key != "states") continue;
        if (!value.hold<CompoundTag>()) break;
        for (auto const& [stateKey, stateValue] : value.get<CompoundTag>()) {
            if (stateKey == "facing_direction" && stateValue.getId() == Tag::Type::Int) {
                return stateValue.get<IntTag>().data;
            }
            if (stateKey == "minecraft:cardinal_direction" && stateValue.getId() == Tag::Type::String) {
                std::string const& facing = static_cast<std::string const&>(stateValue.get<StringTag>());
                if (facing == "north") return static_cast<int>(Facing::Name::North);
                if (facing == "south") return static_cast<int>(Facing::Name::South);
                if (facing == "west")  return static_cast<int>(Facing::Name::West);
                if (facing == "east")  return static_cast<int>(Facing::Name::East);
            }
        }
        break;
    }
    return -1;
}

BlockPos transformStructurePosition(
    structure::LoadedStructure::RenderBlock const& entry,
    structure::LoadedStructure const& loaded,
    int mirrorMode,
    int rotation
) {
    int x = entry.x;
    int z = entry.z;
    if (mirrorMode == 1 || mirrorMode == 3) x = loaded.sizeX - 1 - x;
    if (mirrorMode == 2 || mirrorMode == 3) z = loaded.sizeZ - 1 - z;
    switch (rotation) {
    case 1: return BlockPos{loaded.sizeZ - 1 - z, entry.y, x};
    case 2: return BlockPos{loaded.sizeX - 1 - x, entry.y, loaded.sizeZ - 1 - z};
    case 3: return BlockPos{z, entry.y, loaded.sizeX - 1 - x};
    default: return BlockPos{x, entry.y, z};
    }
}

void renderProjection(BaseActorRenderContext& renderContext, bool renderAlphaLayer) {
    auto& client = renderContext.getClient();
    auto* player  = client.getLocalPlayer();
    if (!contextIsValid(client, player)) {
        clearProjectionStateLocked();
        structure::clear();        return;
    }

    auto& tessellator = renderContext.getTessellator();
    tessellator.begin(Tessellator::DebugContextCallback{}, 128, false);

    if (!gState.blockTessellator) {
        tessellator.cancel();
        return;
    }
    if (!renderAlphaLayer) {
        auto const mirrorMode = structure::getMirrorMode();
        auto const rotationTurns = structure::getRotationQuarterTurns();
        auto const mirror = getProjectionMirror();
        auto const rotation = getProjectionRotation();
        auto const offsetX = structure::getOffsetX();
        auto const offsetY = structure::getOffsetY();
        auto const offsetZ = structure::getOffsetZ();
        auto const layerDisplayMode = structure::getLayerDisplayMode();
        auto const layerAxis = structure::getLayerAxis();
        auto const maxLayer = std::max(
            0,
            (layerAxis == 1 ? gState.structure->sizeX : gState.structure->sizeY) - 1
        );
        auto const displayLayer = std::clamp(
            structure::getDisplayLayer(), 0, maxLayer
        );
        auto const layerIsVisible = [&](int layer) {
            switch (layerDisplayMode) {
            case 1: return layer == displayLayer;
            case 2: return layer <= displayLayer;
            case 3: return layer >= displayLayer;
            default: return true;
            }
        };
        auto const structureOpacity = gOpacity.load(std::memory_order_relaxed);
        auto const correctionFillOpacity = gCorrectionFillOpacity.load(std::memory_order_relaxed);
        auto const correctionOutlineOpacity = gCorrectionOutlineOpacity.load(std::memory_order_relaxed);
        bool const geometryTransformChanged = gState.cachedRotation != rotationTurns
            || gState.cachedMirror != mirrorMode;
        bool const placementMoved = gState.cachedOffsetX != offsetX
            || gState.cachedOffsetY != offsetY || gState.cachedOffsetZ != offsetZ;
        bool const layerChanged = gState.cachedLayerDisplayMode != layerDisplayMode
            || gState.cachedDisplayLayer != displayLayer || gState.cachedLayerAxis != layerAxis;
        bool const opacityChanged = std::abs(gState.cachedOpacity - structureOpacity) > 0.0001f;
        bool const correctionStyleChanged
            = std::abs(gState.cachedCorrectionFillOpacity - correctionFillOpacity) > 0.0001f
            || std::abs(gState.cachedCorrectionOutlineOpacity - correctionOutlineOpacity) > 0.0001f;
        if (geometryTransformChanged || placementMoved || layerChanged || opacityChanged || correctionStyleChanged) {
            if (geometryTransformChanged || opacityChanged || correctionStyleChanged) {
                std::fill(gState.sectionDirty.begin(), gState.sectionDirty.end(), true);
            }

            // LayerRange changes only invalidate sections containing blocks
            // whose visibility crossed the old/new boundary. This mirrors
            // Litematica's section/range intersection instead of rebuilding
            // the whole structure for a one-layer step.
            if (layerChanged && !geometryTransformChanged) {
                auto const oldLayerVisible = [&](structure::LoadedStructure::RenderBlock const& entry) {
                    if (gState.cachedLayerDisplayMode < 0 || gState.cachedLayerAxis < 0) return false;
                    auto const layer = gState.cachedLayerAxis == 1 ? entry.x : entry.y;
                    switch (gState.cachedLayerDisplayMode) {
                    case 1: return layer == gState.cachedDisplayLayer;
                    case 2: return layer <= gState.cachedDisplayLayer;
                    case 3: return layer >= gState.cachedDisplayLayer;
                    default: return true;
                    }
                };
                for (std::size_t index = 0; index < gState.structure->renderBlocks.size(); ++index) {
                    auto const& entry = gState.structure->renderBlocks[index];
                    auto const visible = layerIsVisible(layerAxis == 1 ? entry.x : entry.y);
                    if (oldLayerVisible(entry) == visible) continue;
                    gState.sectionDirty[gState.blockToSection[index]] = true;
                    gState.correctionStates[index] = visible
                        ? ProjectionState::CorrectionState::Unknown
                        : ProjectionState::CorrectionState::Correct;
                }
            }

            gState.cachedRotation = rotationTurns;
            gState.cachedMirror = mirrorMode;
            gState.cachedOffsetX = offsetX;
            gState.cachedOffsetY = offsetY;
            gState.cachedOffsetZ = offsetZ;
            gState.cachedLayerDisplayMode = layerDisplayMode;
            gState.cachedDisplayLayer = displayLayer;
            gState.cachedLayerAxis = layerAxis;
            gState.cachedOpacity = structureOpacity;
            gState.cachedCorrectionFillOpacity = correctionFillOpacity;
            gState.cachedCorrectionOutlineOpacity = correctionOutlineOpacity;
            // Rotation/mirror alter local block models, so only those require
            // throwing away every GPU mesh. XYZ movement is represented by the
            // world matrix and keeps the existing section meshes alive.
            if (geometryTransformChanged) {
                std::fill(
                    gState.correctionStates.begin(),
                    gState.correctionStates.end(),
                    ProjectionState::CorrectionState::Unknown
                );
                for (auto& meshes : gState.sectionMeshes) {
                    for (auto& mesh : meshes) mesh.reset();
                }
                for (auto& mesh : gState.warningFillSectionMeshes) mesh.reset();
                for (auto& mesh : gState.correctionOutlineSectionMeshes) mesh.reset();
                for (auto& mesh : gState.liquidProxySectionMeshes) mesh.reset();
                gState.structureBoundsMesh.reset();
                gState.correctionScanCursor = 0;
                std::fill(gState.progressCorrect.begin(), gState.progressCorrect.end(), 0);
                std::fill(gState.progressErrorKind.begin(), gState.progressErrorKind.end(), 0);
                gState.progressCorrectCount = 0;
                gState.progressWrongTypeCount = 0;
                gState.progressWrongStateCount = 0;
                gBuildProgressPlaced.store(0, std::memory_order_release);
                gBuildProgressWrongType.store(0, std::memory_order_release);
                gBuildProgressWrongState.store(0, std::memory_order_release);
            }

            if (geometryTransformChanged || placementMoved || layerChanged) {
                // A moved placement keeps its local GPU geometry, but the virtual
                // world and correction lookup must follow the new world origin.
                // Recreate the CPU tessellator cache without touching those meshes.
                gState.blockTessellator = std::make_unique<BlockTessellator>(
                    &player->getDimensionBlockSource()
                );
                gState.expectedWorldBlocks.clear();
                gState.expectedWorldBlockIndices.clear();
                std::vector<Vec3> centerSums(gState.sectionCenters.size(), Vec3{});
                std::vector<std::size_t> centerCounts(gState.sectionCenters.size(), 0);
                for (std::size_t index = 0; index < gState.structure->renderBlocks.size(); ++index) {
                auto const& entry = gState.structure->renderBlocks[index];
                if (!layerIsVisible(layerAxis == 1 ? entry.x : entry.y)) {
                    // Hidden layers behave like completed cells for mesh
                    // generation, but are excluded from the world lookup below.
                    gState.correctionStates[index] = ProjectionState::CorrectionState::Correct;
                    continue;
                }
                auto const transformed = transformStructurePosition(
                    entry, *gState.structure, mirrorMode, rotationTurns
                );
                auto const* transformedBlock = transformExpectedBlock(entry.block, rotation, mirror);
                auto const worldKey = std::tuple{
                    gState.anchor.x + offsetX + transformed.x,
                    gState.anchor.y + offsetY + transformed.y,
                    gState.anchor.z + offsetZ + transformed.z
                };
                if (transformedBlock) {
                    gState.expectedWorldBlocks.emplace(worldKey, transformedBlock);
                } else {
                    // Liquids join the virtual world so vanilla liquid-height
                    // queries see stacked virtual water (full-cell columns).
                    auto const* transformedLiquid = transformExpectedBlock(entry.liquid, rotation, mirror);
                    if (transformedLiquid) gState.expectedWorldBlocks.emplace(worldKey, transformedLiquid);
                }
                gState.expectedWorldBlockIndices.emplace(worldKey, index);
                auto const section = gState.blockToSection[index];
                centerSums[section] += Vec3{
                    static_cast<float>(transformed.x) + 0.5f,
                    static_cast<float>(transformed.y) + 0.5f,
                    static_cast<float>(transformed.z) + 0.5f
                };
                ++centerCounts[section];
            }
            for (std::size_t section = 0; section < gState.sectionCenters.size(); ++section) {
                if (centerCounts[section] != 0) {
                    gState.sectionCenters[section] = centerSums[section] / static_cast<float>(centerCounts[section]);
                }
            }
            auto* region = &player->getDimensionBlockSource();
            gState.blockTessellator->mCachedGetBlock.get() = [region](BlockPos const& position) -> Block const& {
                auto const found = gState.expectedWorldBlocks.find(
                    std::tuple{position.x, position.y, position.z}
                );
                return found == gState.expectedWorldBlocks.end() ? region->getBlock(position) : *found->second;
            };
            gState.correctionScanCursor = 0;
            }
        }

        auto& blockTessellator = *gState.blockTessellator;
        blockTessellator.setRegion(player->getDimensionBlockSource());

        auto const totalBlocks = gState.structure->renderBlocks.size();
        // Scan a bounded round-robin batch instead of the entire structure every
        // render frame. Only a real state transition dirties its 16^3 section.
        // The per-frame cap keeps frame cost bounded regardless of structure
        // size. Tune this constant per game version only after profiling.
        constexpr std::size_t kCorrectionChecksPerFrame = 4096;
        auto& region = player->getDimensionBlockSource();
        auto const checks = std::min(totalBlocks, kCorrectionChecksPerFrame);
        for (std::size_t checked = 0; checked < checks; ++checked) {
            auto const index = gState.correctionScanCursor++ % totalBlocks;
            auto const& entry = gState.structure->renderBlocks[index];
            auto const visible = layerIsVisible(layerAxis == 1 ? entry.x : entry.y);
            auto const transformed = transformStructurePosition(
                entry, *gState.structure, mirrorMode, rotationTurns
            );
            BlockPos const position{
                gState.anchor.x + offsetX + transformed.x,
                gState.anchor.y + offsetY + transformed.y,
                gState.anchor.z + offsetZ + transformed.z
            };
            auto const* expected = transformExpectedBlock(entry.block, rotation, mirror);
            auto const* expectedLiquid = transformExpectedBlock(entry.liquid, rotation, mirror);
            auto const& actual = region.getBlock(position);
            auto const& actualLiquid = region.getLiquidBlock(position);
            auto const bodyMissing = expected && actual.isAir();
            auto const liquidMissing = expectedLiquid && actualLiquid.isAir();
            auto const bodyTypeWrong = expected
                && !actual.isAir() && actual.getTypeName() != expected->getTypeName();
            auto const liquidTypeWrong = expectedLiquid
                && !actualLiquid.isAir() && actualLiquid.getTypeName() != expectedLiquid->getTypeName();
            auto const liquidCellOccupiedBySolid = !expected && expectedLiquid && !actual.isAir()
                && actual.getTypeName() != expectedLiquid->getTypeName();
            auto nextState = ProjectionState::CorrectionState::Correct;
            if (bodyMissing || liquidMissing) {
                nextState = ProjectionState::CorrectionState::Missing;
            } else if (bodyTypeWrong || liquidTypeWrong || liquidCellOccupiedBySolid) {
                nextState = ProjectionState::CorrectionState::WrongType;
            } else if ((expected && !projectionStatesMatch(*expected, actual))
                || (expectedLiquid && actualLiquid != *expectedLiquid)) {
                nextState = ProjectionState::CorrectionState::WrongState;
            }
            auto const nowCorrect = nextState == ProjectionState::CorrectionState::Correct;
            auto const wasCorrect = gState.progressCorrect[index] != 0;
            if (nowCorrect != wasCorrect) {
                gState.progressCorrect[index] = nowCorrect ? 1 : 0;
                if (nowCorrect) ++gState.progressCorrectCount;
                else --gState.progressCorrectCount;
                gBuildProgressPlaced.store(gState.progressCorrectCount, std::memory_order_release);
            }
            auto const nextErrorKind = nextState == ProjectionState::CorrectionState::WrongType ? uchar{1}
                : nextState == ProjectionState::CorrectionState::WrongState ? uchar{2}
                : uchar{0};
            auto const previousErrorKind = gState.progressErrorKind[index];
            if (nextErrorKind != previousErrorKind) {
                if (previousErrorKind == 1) --gState.progressWrongTypeCount;
                else if (previousErrorKind == 2) --gState.progressWrongStateCount;
                if (nextErrorKind == 1) ++gState.progressWrongTypeCount;
                else if (nextErrorKind == 2) ++gState.progressWrongStateCount;
                gState.progressErrorKind[index] = nextErrorKind;
                gBuildProgressWrongType.store(gState.progressWrongTypeCount, std::memory_order_release);
                gBuildProgressWrongState.store(gState.progressWrongStateCount, std::memory_order_release);
            }
            // Progress always describes the whole structure. Hidden layers are
            // still checked above, but their correction/model meshes remain
            // suppressed by the layer renderer.
            if (!visible) continue;
            if (gState.correctionStates[index] != nextState) {
                gState.correctionStates[index] = nextState;
                gState.sectionDirty[gState.blockToSection[index]] = true;
                // A missing-cell shell omits faces shared with adjacent missing
                // cells. If either side changes, both section meshes may need an
                // exposed face added or removed (including across 16^3 borders).
                constexpr int neighbors[6][3] = {
                    {-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
                    {0, 1, 0}, {0, 0, -1}, {0, 0, 1}
                };
                for (auto const& delta : neighbors) {
                    auto const neighbor = gState.expectedWorldBlockIndices.find(std::tuple{
                        position.x + delta[0], position.y + delta[1], position.z + delta[2]
                    });
                    if (neighbor != gState.expectedWorldBlockIndices.end()) {
                        gState.sectionDirty[gState.blockToSection[neighbor->second]] = true;
                    }
                }
            }
        }

        // Rebuild at most one dirty 16x16x16 section per frame. Stable frames do
        // no block tessellation and only submit persistent GPU meshes.
        for (std::size_t attempt = 0; attempt < gState.sectionDirty.size(); ++attempt) {
            auto const section = gState.dirtySectionCursor++ % gState.sectionDirty.size();
            if (!gState.sectionDirty[section]) continue;
            gState.sectionDirty[section] = false;
            struct LayeredBlock {
                Block const*                  block{};
                BlockPos                      position{};
                BlockRenderLayer              layer{BlockRenderLayer::RenderlayerOpaque};
                ProjectionState::RenderBucket bucket{ProjectionState::RenderBucket::Opaque};
                std::size_t                   structureIndex{};
            };
            // Blocks whose model produced no geometry during tessellation
            // (block-entity blocks such as chests and signs) get a placeholder.
            std::vector<std::size_t> failedTessellationIndices;
            std::vector<LayeredBlock> layeredBlocks;
            layeredBlocks.reserve(gState.sectionBlockIndices[section].size());
            for (auto const index : gState.sectionBlockIndices[section]) {
                auto const state = gState.correctionStates[index];
                // Never draw a projected block model on top of an existing
                // world block. Correct blocks disappear; wrong type/state use
                // only their red/yellow outline below. This removes the
                // coincident textured surfaces that caused correction flicker.
                if (state == ProjectionState::CorrectionState::Correct
                    || state == ProjectionState::CorrectionState::WrongType
                    || state == ProjectionState::CorrectionState::WrongState) {
                    continue;
                }
                auto const& entry = gState.structure->renderBlocks[index];
                auto const transformed = transformStructurePosition(entry, *gState.structure, mirrorMode, rotationTurns);
                BlockPos const position{
                    gState.anchor.x + offsetX + transformed.x,
                    gState.anchor.y + offsetY + transformed.y,
                    gState.anchor.z + offsetZ + transformed.z
                };
                auto const appendBlock = [&](Block const* source) {
                    auto const* transformedBlock = transformExpectedBlock(source, rotation, mirror);
                    if (!transformedBlock) return;
                    auto const* graphics = BlockGraphics::getForBlock(*transformedBlock);
                    auto const layer = graphics
                        ? graphics->getRenderLayer(region, position)
                        : (transformedBlock->isOpaqueFullBlock()
                            ? BlockRenderLayer::RenderlayerOpaque
                            : BlockRenderLayer::RenderlayerAlphatest);
                    layeredBlocks.push_back({transformedBlock, position, layer, renderBucketFor(layer), index});
                };
                appendBlock(entry.block);
            }
            BlockPos const origin{
                gState.anchor.x + offsetX,
                gState.anchor.y + offsetY,
                gState.anchor.z + offsetZ
            };
            constexpr std::array<char const*, static_cast<std::size_t>(ProjectionState::RenderBucket::Count)>
                meshNames{
                    "LHoloOpaque",
                    "LHoloAlpha",
                    "LHoloAlphaOneSided",
                    "LHoloBlend"
                };
            ScopedTessellationBlocks tessellationBlocksScope(gState.expectedWorldBlocks);
            for (std::size_t bucketIndex = 0; bucketIndex < gState.sectionMeshes.size(); ++bucketIndex) {
                tessellator.cancel();
                tessellator.begin(
                    Tessellator::DebugContextCallback{},
                    std::max(128, static_cast<int>(layeredBlocks.size() * 24)),
                    false
                );
                bool bucketTessellated{};
                auto const bucket = static_cast<ProjectionState::RenderBucket>(bucketIndex);
                for (auto const& layered : layeredBlocks) {
                    if (layered.bucket != bucket) continue;
                    blockTessellator.setRenderLayer(static_cast<int>(layered.layer));
                    auto const firstPosition = tessellator.mMeshData->mPositions.get().size();
                    auto const firstColor = tessellator.mMeshData->mColors.get().size();
                    auto const rendered = blockTessellator.tessellateInWorld(
                        tessellator, *layered.block, layered.position, true
                    );
                    // Several legacy shape tessellators (notably doors) return
                    // false after successfully appending vertices. The return
                    // value describes the dispatch path, not mesh production.
                    // Trust the actual mesh delta so those models are retained.
                    auto const geometryAdded =
                        tessellator.mMeshData->mPositions.get().size() > firstPosition;
                    if (!rendered && !geometryAdded) {
                        // No terrain-atlas model: needs a placeholder hull.
                        failedTessellationIndices.push_back(layered.structureIndex);
                        continue;
                    }
                    auto& colors = tessellator.mMeshData->mColors.get();
                    auto const alpha = static_cast<uint>(std::lround(
                        std::clamp(structureOpacity, 0.05f, 1.0f) * 255.0f
                    ));
                    for (std::size_t colorIndex = firstColor; colorIndex < colors.size(); ++colorIndex) {
                        colors[colorIndex] = (colors[colorIndex] & 0x00FFFFFFU) | (alpha << 24U);
                    }
                    bucketTessellated = true;
                }
                auto& destination = gState.sectionMeshes[bucketIndex][section];
                if (!bucketTessellated) {
                    tessellator.cancel();
                    destination.reset();
                    continue;
                }
                for (auto& vertex : tessellator.mMeshData->mPositions.get()) {
                    vertex.x -= static_cast<float>(origin.x);
                    vertex.y -= static_cast<float>(origin.y);
                    vertex.z -= static_cast<float>(origin.z);
                }
                destination = std::make_unique<mce::Mesh>(tessellator.end(
                    Tessellator::UploadMode::Buffered,
                    meshNames[bucketIndex],
                    Tessellator::SupplementaryFieldAutoGenerationMode::NormalsAndTangents
                ));
            }

            // Textured liquid proxy hulls. LHolo never lies to the vanilla
            // world or chunk pipeline (that leaks into gameplay), so missing
            // liquids draw as translucent hulls here. The hulls reuse the
            // vanilla terrain-atlas water/lava tiles and travel the exact
            // material path used for glass, keeping them purely cosmetic.
            std::vector<std::size_t> liquidProxyIndices;
            for (auto const index : gState.sectionBlockIndices[section]) {
                if (gState.structure->renderBlocks[index].liquid == nullptr) continue;
                if (gState.correctionStates[index] != ProjectionState::CorrectionState::Missing) continue;
                liquidProxyIndices.push_back(index);
            }
            if (!liquidProxyIndices.empty()) {
                tessellator.begin(
                    Tessellator::DebugContextCallback{},
                    mce::PrimitiveMode::QuadList,
                    static_cast<int>(liquidProxyIndices.size() * 24),
                    false
                );
                auto const alpha = static_cast<uint>(std::lround(
                    std::clamp(structureOpacity, 0.05f, 1.0f) * 255.0f
                ));
                for (auto const index : liquidProxyIndices) {
                    auto const& entry = gState.structure->renderBlocks[index];
                    auto const* expectedLiquid = transformExpectedBlock(entry.liquid, rotation, mirror);
                    if (!expectedLiquid) continue;
                    auto const* graphics = BlockGraphics::getForBlock(*expectedLiquid);
                    auto const* uvSet = graphics ? &graphics->getTexture(0, 0) : nullptr;
                    auto const p = transformStructurePosition(entry, *gState.structure, mirrorMode, rotationTurns);
                    BlockPos const worldPosition{
                        gState.anchor.x + offsetX + p.x,
                        gState.anchor.y + offsetY + p.y,
                        gState.anchor.z + offsetZ + p.z
                    };
                    auto const neighborEntry = [&](int dx, int dy, int dz)
                        -> structure::LoadedStructure::RenderBlock const* {
                        auto const found = gState.expectedWorldBlockIndices.find(std::tuple{
                            worldPosition.x + dx, worldPosition.y + dy, worldPosition.z + dz
                        });
                        return found == gState.expectedWorldBlockIndices.end()
                            ? nullptr : &gState.structure->renderBlocks[found->second];
                    };
                    auto const neighborIsSameLiquid = [&](int dx, int dy, int dz) {
                        auto const* neighbor = neighborEntry(dx, dy, dz);
                        if (!neighbor || !neighbor->liquid) return false;
                        auto const* transformed = transformExpectedBlock(neighbor->liquid, rotation, mirror);
                        return transformed && transformed->getTypeName() == expectedLiquid->getTypeName();
                    };
                    // Vanilla source liquids render at 8/9 of a block.
                    // getHeightFromDepth() proved unreliable here on 1.26
                    // (source cells came out as thin slivers), so the proxy
                    // always uses the source height for the topmost cell and
                    // full height for submerged cells. Per-cell flowing depth
                    // is intentionally not depicted; it still participates in
                    // the correction comparison.
                    constexpr float surface = 8.0f / 9.0f;
                    auto const tint = expectedLiquid->getMaterial().isSuperHot()
                        ? (LiquidLavaTintAbgrRgb | (alpha << 24U))
                        : (LiquidWaterTintAbgrRgb | (alpha << 24U));
                    float const x0 = static_cast<float>(p.x);
                    float const y0 = static_cast<float>(p.y);
                    float const z0 = static_cast<float>(p.z);
                    float const x1 = static_cast<float>(p.x + 1);
                    // A same-liquid cell above means this cell is fully
                    // submerged and its sides run to the full cube height.
                    float const y1 = static_cast<float>(p.y)
                        + (neighborIsSameLiquid(0, 1, 0) ? 1.0f : surface);
                    float const z1 = static_cast<float>(p.z + 1);
                    // Full-tile UVs when the atlas tile is available; a tiny
                    // degenerate UV otherwise still renders as flat tint.
                    float const u0 = uvSet ? uvSet->_u0 : 0.0f;
                    float const v0 = uvSet ? uvSet->_v0 : 0.0f;
                    float const u1 = uvSet ? uvSet->_u1 : 0.0f;
                    float const v1 = uvSet ? uvSet->_v1 : 0.0f;
                    auto addLiquidFace = [&](
                        Vec3 const& a, Vec3 const& b, Vec3 const& c, Vec3 const& d
                    ) {
                        tessellator.colorABGR(static_cast<int>(tint));
                        tessellator.vertexUV(a.x, a.y, a.z, u0, v0);
                        tessellator.vertexUV(b.x, b.y, b.z, u0, v1);
                        tessellator.vertexUV(c.x, c.y, c.z, u1, v1);
                        tessellator.vertexUV(d.x, d.y, d.z, u1, v0);
                    };
                    if (!neighborEntry(0, -1, 0))
                        addLiquidFace({x0,y0,z1}, {x0,y0,z0}, {x1,y0,z0}, {x1,y0,z1});
                    if (!neighborIsSameLiquid(0, 1, 0))
                        addLiquidFace({x0,y1,z0}, {x0,y1,z1}, {x1,y1,z1}, {x1,y1,z0});
                    if (!neighborIsSameLiquid(0, 0, -1))
                        addLiquidFace({x0,y0,z0}, {x0,y1,z0}, {x1,y1,z0}, {x1,y0,z0});
                    if (!neighborIsSameLiquid(0, 0, 1))
                        addLiquidFace({x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1}, {x0,y0,z1});
                    if (!neighborIsSameLiquid(-1, 0, 0))
                        addLiquidFace({x0,y0,z1}, {x0,y1,z1}, {x0,y1,z0}, {x0,y0,z0});
                    if (!neighborIsSameLiquid(1, 0, 0))
                        addLiquidFace({x1,y0,z0}, {x1,y1,z0}, {x1,y1,z1}, {x1,y0,z1});
                }
                gState.liquidProxySectionMeshes[section] = std::make_unique<mce::Mesh>(tessellator.end(
                    Tessellator::UploadMode::Buffered,
                    "LHoloLiquidProxy",
                    Tessellator::SupplementaryFieldAutoGenerationMode::None
                ));
            } else {
                gState.liquidProxySectionMeshes[section].reset();
            }

            // Blocks that tessellated to nothing (block-entity blocks such as
            // chests and signs) get a textured placeholder hull from their
            // BlockGraphics tile so the projection still shows them. Blocks
            // that render normally (hoppers, beds, ...) are left untouched.
            std::vector<std::size_t> blockEntityIndices;
            for (auto const index : failedTessellationIndices) {
                if (gState.correctionStates[index] != ProjectionState::CorrectionState::Missing) continue;
                blockEntityIndices.push_back(index);
            }
            if (!blockEntityIndices.empty()) {
                tessellator.begin(
                    Tessellator::DebugContextCallback{},
                    mce::PrimitiveMode::QuadList,
                    static_cast<int>(blockEntityIndices.size() * 24),
                    false
                );
                auto const alpha = static_cast<uint>(std::lround(
                    std::clamp(structureOpacity, 0.05f, 1.0f) * 255.0f
                ));
                auto const tint = 0x00FFFFFFU | (alpha << 24U);
                for (auto const index : blockEntityIndices) {
                    auto const& entry = gState.structure->renderBlocks[index];
                    auto const* expectedBlock = transformExpectedBlock(entry.block, rotation, mirror);
                    if (!expectedBlock) continue;
                    auto const* graphics = BlockGraphics::getForBlock(*expectedBlock);
                    auto const* uvSet = graphics ? &graphics->getTexture(0, 0) : nullptr;
                    auto const p = transformStructurePosition(entry, *gState.structure, mirrorMode, rotationTurns);
                    // Signs are thin planks, not full cubes.
                    bool const isSign = expectedBlock->getTypeName().find("sign") != std::string::npos;
                    // Facing (FacingDirection state: North=2 South=3 West=4 East=5)
                    // decides which cube face is the block's front.
                    int const frontFace = blockFrontFace(*expectedBlock);
                    float const px = static_cast<float>(p.x);
                    float const py = static_cast<float>(p.y);
                    float const pz = static_cast<float>(p.z);
                    float x0 = px, y0 = py, z0 = pz;
                    float x1 = px + 1.0f, y1 = py + 1.0f, z1 = pz + 1.0f;
                    if (isSign) {
                        // Thin plank, 0.125 thick along the facing axis.
                        float const mid = (frontFace == static_cast<int>(Facing::Name::West)
                            || frontFace == static_cast<int>(Facing::Name::East))
                            ? px + 0.5f : pz + 0.5f;
                        if (frontFace == static_cast<int>(Facing::Name::West)
                            || frontFace == static_cast<int>(Facing::Name::East)) {
                            x0 = mid - 0.0625f;
                            x1 = mid + 0.0625f;
                        } else {
                            z0 = mid - 0.0625f;
                            z1 = mid + 0.0625f;
                        }
                    }
                    float const u0 = uvSet ? uvSet->_u0 : 0.0f;
                    float const v0 = uvSet ? uvSet->_v0 : 0.0f;
                    float const u1 = uvSet ? uvSet->_u1 : 0.0f;
                    float const v1 = uvSet ? uvSet->_v1 : 0.0f;
                    auto const frontColor = tint;
                    auto const dimColor = (0x00888888U & 0x00FFFFFFU) | (alpha << 24U);
                    auto addFace = [&](
                        Vec3 const& a, Vec3 const& b, Vec3 const& c, Vec3 const& d, bool isFront
                    ) {
                        auto const color = isFront ? frontColor : dimColor;
                        tessellator.colorABGR(static_cast<int>(color));
                        tessellator.vertexUV(a.x, a.y, a.z, u0, v0);
                        tessellator.vertexUV(b.x, b.y, b.z, u0, v1);
                        tessellator.vertexUV(c.x, c.y, c.z, u1, v1);
                        tessellator.vertexUV(d.x, d.y, d.z, u1, v0);
                    };
                    bool const northFront = frontFace == static_cast<int>(Facing::Name::North);
                    bool const southFront = frontFace == static_cast<int>(Facing::Name::South);
                    bool const westFront = frontFace == static_cast<int>(Facing::Name::West);
                    bool const eastFront = frontFace == static_cast<int>(Facing::Name::East);
                    addFace({x0,y0,z1}, {x0,y0,z0}, {x1,y0,z0}, {x1,y0,z1}, false); // bottom
                    addFace({x0,y1,z0}, {x0,y1,z1}, {x1,y1,z1}, {x1,y1,z0}, false); // top
                    addFace({x0,y0,z0}, {x0,y1,z0}, {x1,y1,z0}, {x1,y0,z0}, northFront); // north
                    addFace({x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1}, {x0,y0,z1}, southFront); // south
                    addFace({x0,y0,z1}, {x0,y1,z1}, {x0,y1,z0}, {x0,y0,z0}, westFront); // west
                    addFace({x1,y0,z0}, {x1,y1,z0}, {x1,y1,z1}, {x1,y0,z1}, eastFront); // east
                }
                gState.blockEntityPlaceholderSectionMeshes[section] = std::make_unique<mce::Mesh>(tessellator.end(
                    Tessellator::UploadMode::Buffered,
                    "LHoloBlockEntityPlaceholder",
                    Tessellator::SupplementaryFieldAutoGenerationMode::None
                ));
            } else {
                gState.blockEntityPlaceholderSectionMeshes[section].reset();
            }

            // Missing and error markers now both use complete-cell shells.
            std::size_t warningCount{};
            for (auto const index : gState.sectionBlockIndices[section]) {
                auto const state = gState.correctionStates[index];
                warningCount += state == ProjectionState::CorrectionState::Missing
                    || state == ProjectionState::CorrectionState::WrongType
                    || state == ProjectionState::CorrectionState::WrongState;
            }
            if (warningCount != 0) {
                auto correctionPriority = [](ProjectionState::CorrectionState state) {
                    return state == ProjectionState::CorrectionState::WrongType ? 4
                        : state == ProjectionState::CorrectionState::WrongState ? 3
                        : state == ProjectionState::CorrectionState::Missing ? 1
                        : 0;
                };

                // Use true LineList geometry rendered with the vanilla outline material.
                constexpr float outlineInset  = 0.0f;
                constexpr float outlineExtent = 1.0f;
                tessellator.begin(
                    Tessellator::DebugContextCallback{},
                    mce::PrimitiveMode::LineList,
                    static_cast<int>(warningCount * 24),
                    false
                );
                auto addOutlineEdge = [&](Vec3 const& first, Vec3 const& second) {
                    tessellator.vertex(first);
                    tessellator.vertex(second);
                };
                for (auto const index : gState.sectionBlockIndices[section]) {
                    auto const state = gState.correctionStates[index];
                    auto const priority = correctionPriority(state);
                    if (priority == 0) continue;
                    auto const& entry = gState.structure->renderBlocks[index];
                    // A missing pure-liquid cell is already communicated by its
                    // blue translucent proxy hull; skip the duplicate outline.
                    if (state == ProjectionState::CorrectionState::Missing
                        && !entry.block && entry.liquid) continue;
                    auto const p = transformStructurePosition(entry, *gState.structure, mirrorMode, rotationTurns);
                    auto const outlineColor = state == ProjectionState::CorrectionState::Missing
                        ? withAlpha(MissingColorAbgrRgb, correctionOutlineOpacity)
                        : state == ProjectionState::CorrectionState::WrongState
                            ? withAlpha(WrongStateColorAbgrRgb, correctionOutlineOpacity)
                            : withAlpha(WrongBlockColorAbgrRgb, correctionOutlineOpacity);
                    float const x0 = static_cast<float>(p.x) + outlineInset;
                    float const y0 = static_cast<float>(p.y) + outlineInset;
                    float const z0 = static_cast<float>(p.z) + outlineInset;
                    float const x1 = static_cast<float>(p.x) + outlineExtent;
                    float const y1 = static_cast<float>(p.y) + outlineExtent;
                    float const z1 = static_cast<float>(p.z) + outlineExtent;
                    tessellator.colorABGR(static_cast<int>(outlineColor));
                    addOutlineEdge({x0,y0,z0},{x1,y0,z0}); addOutlineEdge({x1,y0,z0},{x1,y1,z0});
                    addOutlineEdge({x1,y1,z0},{x0,y1,z0}); addOutlineEdge({x0,y1,z0},{x0,y0,z0});
                    addOutlineEdge({x0,y0,z1},{x1,y0,z1}); addOutlineEdge({x1,y0,z1},{x1,y1,z1});
                    addOutlineEdge({x1,y1,z1},{x0,y1,z1}); addOutlineEdge({x0,y1,z1},{x0,y0,z1});
                    addOutlineEdge({x0,y0,z0},{x0,y0,z1}); addOutlineEdge({x1,y0,z0},{x1,y0,z1});
                    addOutlineEdge({x1,y1,z0},{x1,y1,z1}); addOutlineEdge({x0,y1,z0},{x0,y1,z1});
                }
                gState.correctionOutlineSectionMeshes[section] = std::make_unique<mce::Mesh>(tessellator.end(
                    Tessellator::UploadMode::Buffered,
                    "LHoloCorrectionOutline",
                    Tessellator::SupplementaryFieldAutoGenerationMode::None
                ));

                // Litematica-style correction fill: an exact untextured 1x1x1
                // cell overlay. Depth separation is provided by the material's
                // rasterizer bias at submission time, not by changing geometry.
                tessellator.begin(
                    Tessellator::DebugContextCallback{},
                    mce::PrimitiveMode::QuadList,
                    static_cast<int>(warningCount * 24),
                    false
                );
                auto addFillFace = [&](Vec3 const& a, Vec3 const& b, Vec3 const& c, Vec3 const& d) {
                    tessellator.vertex(a);
                    tessellator.vertex(b);
                    tessellator.vertex(c);
                    tessellator.vertex(d);
                };
                for (auto const index : gState.sectionBlockIndices[section]) {
                    auto const state = gState.correctionStates[index];
                    auto const priority = correctionPriority(state);
                    if (priority == 0) continue;
                    auto const& entry = gState.structure->renderBlocks[index];
                    // Same as the outline above: the blue proxy hull already
                    // marks a missing pure-liquid cell.
                    if (state == ProjectionState::CorrectionState::Missing
                        && !entry.block && entry.liquid) continue;
                    auto const p = transformStructurePosition(entry, *gState.structure, mirrorMode, rotationTurns);
                    BlockPos const worldPosition{
                        gState.anchor.x + offsetX + p.x,
                        gState.anchor.y + offsetY + p.y,
                        gState.anchor.z + offsetZ + p.z
                    };
                    auto const neighborPriority = [&](int dx, int dy, int dz) {
                        auto const found = gState.expectedWorldBlockIndices.find(std::tuple{
                            worldPosition.x + dx, worldPosition.y + dy, worldPosition.z + dz
                        });
                        return found == gState.expectedWorldBlockIndices.end()
                            ? 0 : correctionPriority(gState.correctionStates[found->second]);
                    };
                    float const x0 = static_cast<float>(p.x);
                    float const y0 = static_cast<float>(p.y);
                    float const z0 = static_cast<float>(p.z);
                    float const x1 = static_cast<float>(p.x + 1);
                    float const y1 = static_cast<float>(p.y + 1);
                    float const z1 = static_cast<float>(p.z + 1);
                    auto const fillColor = state == ProjectionState::CorrectionState::Missing
                        ? withAlpha(MissingColorAbgrRgb, correctionFillOpacity)
                        : state == ProjectionState::CorrectionState::WrongState
                            ? withAlpha(WrongStateColorAbgrRgb, correctionFillOpacity)
                            : withAlpha(WrongBlockColorAbgrRgb, correctionFillOpacity);
                    tessellator.colorABGR(static_cast<int>(fillColor));
                    if (priority > neighborPriority(0, 0, -1)) addFillFace({x0,y0,z0}, {x0,y1,z0}, {x1,y1,z0}, {x1,y0,z0});
                    if (priority > neighborPriority(0, 0, 1))  addFillFace({x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1}, {x0,y0,z1});
                    if (priority > neighborPriority(-1, 0, 0)) addFillFace({x0,y0,z1}, {x0,y1,z1}, {x0,y1,z0}, {x0,y0,z0});
                    if (priority > neighborPriority(1, 0, 0))  addFillFace({x1,y0,z0}, {x1,y1,z0}, {x1,y1,z1}, {x1,y0,z1});
                    if (priority > neighborPriority(0, -1, 0)) addFillFace({x0,y0,z1}, {x0,y0,z0}, {x1,y0,z0}, {x1,y0,z1});
                    if (priority > neighborPriority(0, 1, 0))  addFillFace({x0,y1,z0}, {x0,y1,z1}, {x1,y1,z1}, {x1,y1,z0});
                }
                gState.warningFillSectionMeshes[section] = std::make_unique<mce::Mesh>(tessellator.end(
                    Tessellator::UploadMode::Buffered,
                    "LHoloWarningFill",
                    Tessellator::SupplementaryFieldAutoGenerationMode::None
                ));

            } else {
                gState.warningFillSectionMeshes[section].reset();
                gState.correctionOutlineSectionMeshes[section].reset();
            }

            if (!gState.structureBoundsMesh) {
                auto const rotated = rotationTurns == 1 || rotationTurns == 3;
                auto const width = static_cast<float>(
                    rotated ? gState.structure->sizeZ : gState.structure->sizeX
                );
                auto const height = static_cast<float>(gState.structure->sizeY);
                auto const depth = static_cast<float>(
                    rotated ? gState.structure->sizeX : gState.structure->sizeZ
                );
                constexpr float expansion = 0.01f;
                float const x0 = -expansion, y0 = -expansion, z0 = -expansion;
                float const x1 = width + expansion;
                float const y1 = height + expansion;
                float const z1 = depth + expansion;
                tessellator.begin(
                    Tessellator::DebugContextCallback{},
                    mce::PrimitiveMode::LineList,
                    24,
                    false
                );
                tessellator.colorABGR(static_cast<int>(0xFFFFD633U));
                auto addBoundsEdge = [&](Vec3 const& a, Vec3 const& b) {
                    tessellator.vertex(a);
                    tessellator.vertex(b);
                };
                addBoundsEdge({x0,y0,z0},{x1,y0,z0}); addBoundsEdge({x1,y0,z0},{x1,y1,z0});
                addBoundsEdge({x1,y1,z0},{x0,y1,z0}); addBoundsEdge({x0,y1,z0},{x0,y0,z0});
                addBoundsEdge({x0,y0,z1},{x1,y0,z1}); addBoundsEdge({x1,y0,z1},{x1,y1,z1});
                addBoundsEdge({x1,y1,z1},{x0,y1,z1}); addBoundsEdge({x0,y1,z1},{x0,y0,z1});
                addBoundsEdge({x0,y0,z0},{x0,y0,z1}); addBoundsEdge({x1,y0,z0},{x1,y0,z1});
                addBoundsEdge({x1,y1,z0},{x1,y1,z1}); addBoundsEdge({x0,y1,z0},{x0,y1,z1});
                gState.structureBoundsMesh = std::make_unique<mce::Mesh>(tessellator.end(
                    Tessellator::UploadMode::Buffered,
                    "LHoloStructureBounds",
                    Tessellator::SupplementaryFieldAutoGenerationMode::None
                ));
            }
            break;
        }
        if (tessellator.isTessellating()) tessellator.cancel();
    }

    // Keep vanilla world queries at their real BlockPos, but do not upload large
    // absolute coordinates to the GPU. Render vertices relative to the projection
    // origin, matching the strategy used by chunk meshes.
    BlockPos const renderOrigin{
        gState.anchor.x + structure::getOffsetX(),
        gState.anchor.y + structure::getOffsetY(),
        gState.anchor.z + structure::getOffsetZ()
    };
    auto const structureOpacity = gOpacity.load(std::memory_order_relaxed);
    auto const& camera = renderContext.getCameraPosition();
    if (renderAlphaLayer) {
        // The transparent pass only submits meshes built during the preceding
        // opaque pass. Do not leave the shared immediate tessellator active.
        tessellator.cancel();
    }

    auto matrix = renderContext.getWorldMatrix().push(false);
    matrix->translate(
        static_cast<float>(renderOrigin.x) - camera.x,
        static_cast<float>(renderOrigin.y) - camera.y,
        static_cast<float>(renderOrigin.z) - camera.z
    );

    auto& itemRenderer = renderContext.getItemInHandRenderer();
    auto const& blendMaterial = itemRenderer.mMatBlendBlock.get();
    if (!blendMaterial) {
        tessellator.cancel();
        return;
    }

    if (!gState.terrainTextureVariant) {
        tessellator.cancel();
        logger().error("Projection terrain texture is not available");
        return;
    }

    try {
        {
            struct VisibleMesh { std::size_t bucket; std::size_t section; };
            auto worldCenter = [&](std::size_t section) {
                return Vec3{
                    static_cast<float>(renderOrigin.x) + gState.sectionCenters[section].x,
                    static_cast<float>(renderOrigin.y) + gState.sectionCenters[section].y,
                    static_cast<float>(renderOrigin.z) + gState.sectionCenters[section].z
                };
            };
            auto distanceSquared = [&](Vec3 const& point) {
                auto const dx = point.x - camera.x;
                auto const dy = point.y - camera.y;
                auto const dz = point.z - camera.z;
                return dx * dx + dy * dy + dz * dz;
            };
            auto sortBackToFront = [&](std::vector<VisibleMesh>& meshes) {
                std::sort(meshes.begin(), meshes.end(), [&](VisibleMesh const& lhs, VisibleMesh const& rhs) {
                    return distanceSquared(worldCenter(lhs.section))
                        > distanceSquared(worldCenter(rhs.section));
                });
            };
            auto renderMeshes = [&](std::vector<VisibleMesh> const& meshes, mce::MaterialPtr const& material) {
                if (!material) return;
                for (auto const& visible : meshes) {
                    auto& mesh = *gState.sectionMeshes[visible.bucket][visible.section];
                    mesh.renderMesh(
                        renderContext.getScreenContext(),
                        material,
                        *gState.terrainTextureVariant,
                        0,
                        static_cast<uint>(mesh.getMeshVertexCount()),
                        renderContext.mOffscreenCaptureDescription.get(),
                        nullptr
                    );
                }
            };
            auto collectBucket = [&](std::size_t bucket) {
                std::vector<VisibleMesh> result;
                auto const& meshes = gState.sectionMeshes[bucket];
                result.reserve(meshes.size());
                for (std::size_t section = 0; section < meshes.size(); ++section) {
                    if (meshes[section] && meshes[section]->isValid()) {
                        result.push_back({bucket, section});
                    }
                }
                return result;
            };

            auto const opaqueBucket = static_cast<std::size_t>(ProjectionState::RenderBucket::Opaque);
            auto const alphaBucket = static_cast<std::size_t>(ProjectionState::RenderBucket::Alpha);
            auto const alphaOneSidedBucket = static_cast<std::size_t>(ProjectionState::RenderBucket::AlphaOneSided);
            auto const blendBucket = static_cast<std::size_t>(ProjectionState::RenderBucket::Blend);
            if (structureOpacity >= 0.999f) {
                auto opaqueMeshes = collectBucket(opaqueBucket);
                auto alphaMeshes = collectBucket(alphaBucket);
                auto alphaOneSidedMeshes = collectBucket(alphaOneSidedBucket);
                auto transparentMeshes = collectBucket(blendBucket);
                sortBackToFront(transparentMeshes);

                auto const& opaqueMaterial = itemRenderer.mMatOpaqueBlock.get();
                auto const& alphaMaterial = itemRenderer.mMatAlphaBlock.get();
                auto const& alphaOneSidedMaterial = itemRenderer.mMatAlphaOneSidedBlock.get();
                if (!renderAlphaLayer) {
                    renderMeshes(opaqueMeshes, opaqueMaterial ? opaqueMaterial : blendMaterial);
                    renderMeshes(alphaMeshes, alphaMaterial ? alphaMaterial : blendMaterial);
                    renderMeshes(
                        alphaOneSidedMeshes,
                        alphaOneSidedMaterial ? alphaOneSidedMaterial : (alphaMaterial ? alphaMaterial : blendMaterial)
                    );
                } else {
                    renderMeshes(transparentMeshes, blendMaterial);
                }
            } else if (renderAlphaLayer) {
                // True projection transparency needs a blending material even
                // for normally opaque/cutout blocks. Sort all buckets together
                // so changing opacity never reintroduces inter-section flicker.
                std::vector<VisibleMesh> transparentMeshes;
                for (std::size_t bucket = 0; bucket < gState.sectionMeshes.size(); ++bucket) {
                    auto bucketMeshes = collectBucket(bucket);
                    transparentMeshes.insert(
                        transparentMeshes.end(), bucketMeshes.begin(), bucketMeshes.end()
                    );
                }
                sortBackToFront(transparentMeshes);
                renderMeshes(transparentMeshes, blendMaterial);
            }

            // Textured liquid hulls travel the proven glass path: blend-block
            // material plus the terrain atlas, sorted back to front by
            // section like the other transparent meshes.
            if (renderAlphaLayer) {
                std::vector<std::size_t> liquidSections;
                for (std::size_t liquidSection = 0;
                     liquidSection < gState.liquidProxySectionMeshes.size();
                     ++liquidSection) {
                    auto const& mesh = gState.liquidProxySectionMeshes[liquidSection];
                    if (mesh && mesh->isValid()) liquidSections.push_back(liquidSection);
                }
                std::sort(
                    liquidSections.begin(),
                    liquidSections.end(),
                    [&](std::size_t lhs, std::size_t rhs) {
                        return distanceSquared(worldCenter(lhs))
                            > distanceSquared(worldCenter(rhs));
                    }
                );
                for (auto const liquidSection : liquidSections) {
                    auto& mesh = *gState.liquidProxySectionMeshes[liquidSection];
                    mesh.renderMesh(
                        renderContext.getScreenContext(),
                        blendMaterial,
                        *gState.terrainTextureVariant,
                        0,
                        static_cast<uint>(mesh.getMeshVertexCount()),
                        renderContext.mOffscreenCaptureDescription.get(),
                        nullptr
                    );
                }

                // Textured placeholder hulls for block-entity blocks.
                for (auto const& placeholder : gState.blockEntityPlaceholderSectionMeshes) {
                    if (!placeholder || !placeholder->isValid()) continue;
                    placeholder->renderMesh(
                        renderContext.getScreenContext(),
                        blendMaterial,
                        *gState.terrainTextureVariant,
                        0,
                        static_cast<uint>(placeholder->getMeshVertexCount()),
                        renderContext.mOffscreenCaptureDescription.get(),
                        nullptr
                    );
                }
            }
        }

        if (renderAlphaLayer) {
            auto* levelRenderer = client.getLevelRenderer();
            auto const& outlineMaterial = levelRenderer
                ? levelRenderer->getLevelRendererPlayer().mOutlineSelectionMaterial.get()
                : renderContext.getItemInHandRenderer().mMatBlendBlock.get();
            if (outlineMaterial && gStructureBoundsEnabled.load(std::memory_order_relaxed)
                && gState.structureBoundsMesh && gState.structureBoundsMesh->isValid()) {
                gState.structureBoundsMesh->renderMesh(
                    renderContext.getScreenContext(),
                    outlineMaterial,
                    0,
                    static_cast<uint>(gState.structureBoundsMesh->getMeshVertexCount()),
                    renderContext.mOffscreenCaptureDescription.get(),
                    nullptr
                );
            }
            auto const& warningMaterial = levelRenderer
                ? levelRenderer->getLevelRendererPlayer().selectionBlockEntityOverlayColorMaterial.get()
                : renderContext.getItemInHandRenderer().mMatBlendBlockNoColor.get();
            auto renderOverlayMeshes = [&](
                std::vector<std::unique_ptr<mce::Mesh>> const& meshes, mce::MaterialPtr const& material
            ) {
                if (!material) return;
                for (auto const& overlay : meshes) {
                    if (!overlay || !overlay->isValid()) continue;
                    overlay->renderMesh(
                        renderContext.getScreenContext(),
                        material,
                        0,
                        static_cast<uint>(overlay->getMeshVertexCount()),
                        renderContext.mOffscreenCaptureDescription.get(),
                        nullptr
                    );
                }
            };

            if (static_cast<bool>(itemRenderer.mIsDeferredEnabled)) {
                // The colored outline material is already proven stable and
                // correctly exposed by Vibrant Visuals. Reuse the same shader
                // for the hull, changing only its primitive and blend state
                // for this submission. Restoring immediately keeps the normal
                // correction and structure outlines untouched.
                auto* renderMaterial = outlineMaterial
                    ? const_cast<mce::RenderMaterial*>(outlineMaterial.operator->())
                    : nullptr;
                if (renderMaterial) {
                    auto const savedPrimitive = renderMaterial->mPrimitiveMode;
                    auto const savedBlend = renderMaterial->blendStateDescription.get();
                    auto const savedDepthBias = renderMaterial->mDepthBias;
                    auto const savedSlopeBias = renderMaterial->mSlopeScaledDepthBias;
                    renderMaterial->mPrimitiveMode = mce::PrimitiveMode::QuadList;
                    renderMaterial->blendStateDescription.get()
                        = blendMaterial->blendStateDescription.get();
                    renderMaterial->mDepthBias = 100.0f;
                    renderMaterial->mSlopeScaledDepthBias = 15.0f;
                    renderOverlayMeshes(gState.warningFillSectionMeshes, outlineMaterial);
                    renderMaterial->mPrimitiveMode = savedPrimitive;
                    renderMaterial->blendStateDescription.get() = savedBlend;
                    renderMaterial->mDepthBias = savedDepthBias;
                    renderMaterial->mSlopeScaledDepthBias = savedSlopeBias;
                }
            } else if (warningMaterial) {
                // Bedrock's selection overlay already supplies the D3D depth
                // bias equivalent of Java's polygon offset, but its default
                // blend equation is multiplicative. Temporarily borrow the
                // standard SourceAlpha/OneMinusSourceAlpha state from the
                // vanilla blend-block material so the 0x4C overlay preserves
                // the real block texture beneath it.
                auto* renderMaterial = levelRenderer
                    ? const_cast<mce::RenderMaterial*>(warningMaterial.operator->())
                    : nullptr;
                struct MaterialStateRestore {
                    mce::RenderMaterial* material{};
                    std::optional<mce::BlendStateDescription> blend;
                    float depthBias{};
                    float slopeBias{};
                    ~MaterialStateRestore() {
                        if (!material || !blend) return;
                        material->blendStateDescription.get() = *blend;
                        material->mDepthBias = depthBias;
                        material->mSlopeScaledDepthBias = slopeBias;
                    }
                } restore;
                if (renderMaterial && blendMaterial) {
                    restore.material = renderMaterial;
                    restore.blend = renderMaterial->blendStateDescription.get();
                    restore.depthBias = renderMaterial->mDepthBias;
                    restore.slopeBias = renderMaterial->mSlopeScaledDepthBias;
                    renderMaterial->blendStateDescription.get()
                        = blendMaterial->blendStateDescription.get();
                    renderMaterial->mDepthBias = 100.0f;
                    renderMaterial->mSlopeScaledDepthBias = 15.0f;
                }
                renderOverlayMeshes(gState.warningFillSectionMeshes, warningMaterial);
            }
            if (outlineMaterial) {
                for (auto const& correctionOutline : gState.correctionOutlineSectionMeshes) {
                    if (!correctionOutline || !correctionOutline->isValid()) continue;
                    correctionOutline->renderMesh(
                        renderContext.getScreenContext(),
                        outlineMaterial,
                        0,
                        static_cast<uint>(correctionOutline->getMeshVertexCount()),
                        renderContext.mOffscreenCaptureDescription.get(),
                        nullptr
                    );
                }
            }
        }
    } catch (std::exception const& exception) {
        logger().error("Projection immediate mesh submission failed: {}", exception.what());
        tessellator.cancel();
        clearProjectionStateLocked();
        return;
    } catch (...) {
        logger().error("Projection immediate mesh submission failed with an unknown exception");
        tessellator.cancel();
        clearProjectionStateLocked();
        return;
    }

}

LL_TYPE_INSTANCE_HOOK(
    BlockSourceGetBlockHook,
    ll::memory::HookPriority::Normal,
    BlockSource,
    static_cast<Block const& (BlockSource::*)(BlockPos const&) const>(&BlockSource::$getBlock),
    Block const&,
    BlockPos const& position
) {
    if (gTessellationBlocks) {
        auto const found = gTessellationBlocks->find(
            std::tuple{position.x, position.y, position.z}
        );
        if (found != gTessellationBlocks->end()) return *found->second;
    }
    return origin(position);
}

LL_TYPE_INSTANCE_HOOK(
    BlockSourceGetBlockLayerHook,
    ll::memory::HookPriority::Normal,
    BlockSource,
    static_cast<Block const& (BlockSource::*)(BlockPos const&, uint) const>(&BlockSource::$getBlock),
    Block const&,
    BlockPos const& position,
    uint layer
) {
    if (layer == 0 && gTessellationBlocks) {
        auto const found = gTessellationBlocks->find(std::tuple{position.x, position.y, position.z});
        if (found != gTessellationBlocks->end()) return *found->second;
    }
    return origin(position, layer);
}

LL_TYPE_INSTANCE_HOOK(
    LoopbackPacketSenderSendToServerHook,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    &LoopbackPacketSender::$sendToServer,
    void,
    Packet& packet
) {
    if (filterProjectionPacket(packet)) return;
    origin(packet);
}

LL_TYPE_INSTANCE_HOOK(
    LoopbackPacketSenderSendHook,
    ll::memory::HookPriority::Normal,
    LoopbackPacketSender,
    &LoopbackPacketSender::$send,
    void,
    Packet& packet
) {
    if (filterProjectionPacket(packet)) return;
    origin(packet);
}

LL_TYPE_INSTANCE_HOOK(
    LevelRendererPlayerRenderHitSelectHook,
    ll::memory::HookPriority::Normal,
    LevelRendererPlayer,
    &LevelRendererPlayer::renderHitSelect,
    void,
    BaseActorRenderContext& renderContext,
    BlockSource&            region,
    BlockPos const&         pos,
    bool                    fancyGraphics
) {
    {
        std::lock_guard lock(gStateMutex);
        if (gState.enabled && gState.structure) {
            auto const found = gState.expectedWorldBlockIndices.find(
                std::tuple{pos.x, pos.y, pos.z}
            );
            if (found != gState.expectedWorldBlockIndices.end()) {
                auto const state = gState.correctionStates[found->second];
                if (state == ProjectionState::CorrectionState::WrongType
                    || state == ProjectionState::CorrectionState::WrongState) {
                    // LHolo already renders a complete red/yellow hull and
                    // outline for this cell. Vanilla's coincident hit-select
                    // overlay adds a second surface only while the crosshair
                    // targets it, producing the observed flicker.
                    return;
                }
            }
        }
    }
    origin(renderContext, region, pos, fancyGraphics);
}

LL_TYPE_INSTANCE_HOOK(
    LevelRendererPlayerRenderBlockEntitiesHook,
    ll::memory::HookPriority::Normal,
    LevelRendererPlayer,
    &LevelRendererPlayer::$renderBlockEntities,
    void,
    BaseActorRenderContext& renderContext,
    bool                      renderAlphaLayer
) {
    origin(renderContext, renderAlphaLayer);

    std::lock_guard lock(gStateMutex);

    if (auto loaded = structure::getLoaded(); loaded && loaded->generation != gState.structureGeneration) {
        clearProjectionStateLocked();
        if (!enableStructureProjection(renderContext, std::move(loaded))) {
            clearProjectionStateLocked();
            logger().error("Could not enable loaded structure projection");
        }
    }

    if (!gState.enabled) return;
    renderProjection(renderContext, renderAlphaLayer);
}

} // namespace

bool installHook() {
    if (BlockSourceGetBlockHook::hook() < 0) return false;
    if (BlockSourceGetBlockLayerHook::hook() < 0) {
        BlockSourceGetBlockHook::unhook();
        return false;
    }
    if (LoopbackPacketSenderSendToServerHook::hook() < 0) {
        BlockSourceGetBlockLayerHook::unhook();
        BlockSourceGetBlockHook::unhook();
        return false;
    }
    if (LoopbackPacketSenderSendHook::hook() < 0) {
        LoopbackPacketSenderSendToServerHook::unhook();
        BlockSourceGetBlockLayerHook::unhook();
        BlockSourceGetBlockHook::unhook();
        return false;
    }
    if (LevelRendererPlayerRenderHitSelectHook::hook() < 0) {
        LoopbackPacketSenderSendHook::unhook();
        LoopbackPacketSenderSendToServerHook::unhook();
        BlockSourceGetBlockLayerHook::unhook();
        BlockSourceGetBlockHook::unhook();
        return false;
    }
    if (LevelRendererPlayerRenderBlockEntitiesHook::hook() < 0) {
        LevelRendererPlayerRenderHitSelectHook::unhook();
        LoopbackPacketSenderSendHook::unhook();
        LoopbackPacketSenderSendToServerHook::unhook();
        BlockSourceGetBlockLayerHook::unhook();
        BlockSourceGetBlockHook::unhook();
        return false;
    }
    return true;
}

void uninstallHook() {
    LevelRendererPlayerRenderBlockEntitiesHook::unhook();
    LevelRendererPlayerRenderHitSelectHook::unhook();
    LoopbackPacketSenderSendHook::unhook();
    LoopbackPacketSenderSendToServerHook::unhook();
    BlockSourceGetBlockLayerHook::unhook();
    BlockSourceGetBlockHook::unhook();
}

void disable() {
    clearProjectionState();
}

float getOpacity() {
    return gOpacity.load(std::memory_order_relaxed);
}

void setOpacity(float opacity) {
    gOpacity.store(std::clamp(opacity, 0.0f, 1.0f), std::memory_order_relaxed);
}

float getCorrectionFillOpacity() {
    return gCorrectionFillOpacity.load(std::memory_order_relaxed);
}

void setCorrectionFillOpacity(float opacity) {
    gCorrectionFillOpacity.store(std::clamp(opacity, 0.0f, 1.0f), std::memory_order_relaxed);
}

float getCorrectionOutlineOpacity() {
    return gCorrectionOutlineOpacity.load(std::memory_order_relaxed);
}

void setCorrectionOutlineOpacity(float opacity) {
    gCorrectionOutlineOpacity.store(std::clamp(opacity, 0.0f, 1.0f), std::memory_order_relaxed);
}

bool getStructureBoundsEnabled() {
    return gStructureBoundsEnabled.load(std::memory_order_relaxed);
}

void setStructureBoundsEnabled(bool enabled) {
    gStructureBoundsEnabled.store(enabled, std::memory_order_relaxed);
}

void requestNextStructureAnchor(int x, int y, int z) {
    gPendingStructureAnchorX.store(x, std::memory_order_relaxed);
    gPendingStructureAnchorY.store(y, std::memory_order_relaxed);
    gPendingStructureAnchorZ.store(z, std::memory_order_relaxed);
    gPendingStructureAnchor.store(true, std::memory_order_release);
}

BuildProgress getBuildProgress() {
    BuildProgress result;
    result.total = gBuildProgressTotal.load(std::memory_order_acquire);
    result.placed = gBuildProgressPlaced.load(std::memory_order_acquire);
    result.wrongType = gBuildProgressWrongType.load(std::memory_order_acquire);
    result.wrongState = gBuildProgressWrongState.load(std::memory_order_acquire);
    if (result.placed > result.total) result.placed = result.total;
    if (result.wrongType > result.total) result.wrongType = result.total;
    if (result.wrongState > result.total) result.wrongState = result.total;
    return result;
}

ProjectionQuery queryProjection(BlockPos const& worldPos) {
    std::lock_guard lock(gStateMutex);
    if (!gState.enabled || !gState.structure) return {nullptr, false};
    auto const key = std::tuple{worldPos.x, worldPos.y, worldPos.z};
    auto const foundIndex = gState.expectedWorldBlockIndices.find(key);
    if (foundIndex == gState.expectedWorldBlockIndices.end()) return {nullptr, false};
    auto const foundBlock = gState.expectedWorldBlocks.find(key);
    Block const* block = foundBlock == gState.expectedWorldBlocks.end() ? nullptr : foundBlock->second;
    // Liquids have no normal block item, so they are never a valid place target.
    if (block && block->getMaterial().isLiquid()) block = nullptr;
    bool const missing = gState.correctionStates[foundIndex->second]
        == ProjectionState::CorrectionState::Missing;
    return {block, missing};
}

std::vector<RangeCandidate> queryMissingCellsInRange(Vec3 const& center, float radius) {
    std::vector<RangeCandidate> result;
    std::lock_guard lock(gStateMutex);
    if (!gState.enabled || !gState.structure) return result;
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
                auto const foundIndex = gState.expectedWorldBlockIndices.find(key);
                if (foundIndex == gState.expectedWorldBlockIndices.end()) continue;
                if (gState.correctionStates[foundIndex->second] != ProjectionState::CorrectionState::Missing) continue;
                float const dx = static_cast<float>(x) + 0.5f - center.x;
                float const dy = static_cast<float>(y) + 0.5f - center.y;
                float const dz = static_cast<float>(z) + 0.5f - center.z;
                if (dx * dx + dy * dy + dz * dz > r2) continue;
                auto const foundBlock = gState.expectedWorldBlocks.find(key);
                Block const* block = foundBlock == gState.expectedWorldBlocks.end() ? nullptr : foundBlock->second;
                // Liquids have no normal block item, so they are never a place target.
                if (block && block->getMaterial().isLiquid()) block = nullptr;
                if (!block) continue;
                result.push_back({x, y, z, block});
            }
        }
    }
    std::sort(result.begin(), result.end(), [&center](RangeCandidate const& a, RangeCandidate const& b) {
        auto const distSq = [&center](RangeCandidate const& c) {
            float const dx = static_cast<float>(c.x) + 0.5f - center.x;
            float const dy = static_cast<float>(c.y) + 0.5f - center.y;
            float const dz = static_cast<float>(c.z) + 0.5f - center.z;
            return dx * dx + dy * dy + dz * dz;
        };
        return distSq(a) < distSq(b);
    });
    return result;
}

} // namespace lholo::projection
