// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "projection/mesh/ProjectionSectionBuilder.h"

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/core/ProjectionRules.h"
#include "projection/core/ProjectionShellRules.h"
#include "projection/core/ProjectionState.h"
#include "projection/core/ProjectionTintRules.h"
#include "projection/world/ProjectionVirtualWorld.h"
#include "structure/StructureLoader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <vector>

#include "mc/client/renderer/SupplementaryFieldAutoGenerationMode.h"
#include "mc/client/renderer/block/BlockGraphics.h"
#include "mc/client/renderer/block/BlockTessellator.h"
#include "mc/client/renderer/texture/TextureUVCoordinateSet.h"
#include "mc/client/world/level/biome/biome_color_sampling/TessellationPolicy.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/Tag.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "mc/world/Facing.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/VanillaBlockTypeIds.h"
#include "mc/world/level/biome/biome_color_sampling/BiomeColorSampling.h"
#include "mc/world/level/material/Material.h"
#include "mc/world/level/levelgen/structure/LegacyStructureSettings.h"

namespace lholo::projection::detail {
namespace {

// Litematica's default schematic overlay palette, converted from ARGB to the
// ABGR byte order of the tessellator vertex color buffer.
constexpr std::uint32_t MissingColorAbgrRgb    = 0x00E6B333U; // #33B3E6
constexpr std::uint32_t ExtraColorAbgrRgb      = 0x00E64CFFU; // #FF4CE6
constexpr std::uint32_t WrongBlockColorAbgrRgb = 0x003333FFU; // #FF3333
constexpr std::uint32_t WrongStateColorAbgrRgb = 0x001090FFU; // #FF9010

constexpr std::uint32_t LiquidWaterTintAbgrRgb = 0x00E4763FU; // #3F76E4
constexpr std::uint32_t LiquidLavaTintAbgrRgb  = 0x001080FFU; // #FF8010

void setColorAbgr(Tessellator& tessellator, std::uint32_t colorAbgr) {
    tessellator.color(
        static_cast<float>((colorAbgr >> 0) & 0xFFU) / 255.0f,
        static_cast<float>((colorAbgr >> 8) & 0xFFU) / 255.0f,
        static_cast<float>((colorAbgr >> 16) & 0xFFU) / 255.0f,
        static_cast<float>((colorAbgr >> 24) & 0xFFU) / 255.0f
    );
}

std::uint32_t toAbgr(mce::Color const& color) {
    auto const toByte = [](float component) {
        return static_cast<std::uint32_t>(
            std::lround(255.0f * std::clamp(component, 0.0f, 1.0f))
        );
    };
    return (toByte(color.a) << 24U)
        | (toByte(color.b) << 16U)
        | (toByte(color.g) << 8U)
        | toByte(color.r);
}

std::uint32_t withAlpha(std::uint32_t colorAbgrRgb, float opacity) {
    auto const alpha = static_cast<std::uint32_t>(
        std::lround(std::clamp(opacity, 0.0f, 1.0f) * 255.0f)
    );
    return colorAbgrRgb | (alpha << 24U);
}

std::uint32_t modulateAbgr(std::uint32_t color, std::uint32_t tint) {
    auto const channel = [&](unsigned int shift) {
        auto const value = ((color >> shift) & 0xFFU) * ((tint >> shift) & 0xFFU);
        return ((value + 127U) / 255U) << shift;
    };
    return (color & 0xFF000000U) | channel(0) | channel(8) | channel(16);
}

int correctionPriority(CorrectionState state) {
    return state == CorrectionState::WrongType ? 4
        : state == CorrectionState::WrongState ? 3
        : state == CorrectionState::Missing ? 1
        : 0;
}

int getLiquidDepth(Block const& block) {
    for (auto const& [key, value] : block.mSerializationId.get()) {
        if (key != "states" || !value.hold<::CompoundTag>()) continue;
        for (auto const& [stateKey, stateValue] : value.get<::CompoundTag>()) {
            if (stateKey == "liquid_depth" && stateValue.getId() == ::Tag::Type::Int) {
                return stateValue.get<::IntTag>().data;
            }
        }
    }
    return 0;
}

constexpr float getFluidHeight(int depth) {
    if (depth <= 0) return 8.0f / 9.0f;   // source
    if (depth >= 8) return 1.0f;          // falling counts as full
    return (8.0f - static_cast<float>(depth)) / 9.0f;
}

std::uint32_t applyShade(std::uint32_t rgb, float factor, std::uint32_t alphaByte) {
    auto const r = static_cast<std::uint32_t>(std::clamp(static_cast<int>(std::lround(((rgb >> 0) & 0xFFU) * factor)), 0, 255));
    auto const g = static_cast<std::uint32_t>(std::clamp(static_cast<int>(std::lround(((rgb >> 8) & 0xFFU) * factor)), 0, 255));
    auto const b = static_cast<std::uint32_t>(std::clamp(static_cast<int>(std::lround(((rgb >> 16) & 0xFFU) * factor)), 0, 255));
    return (alphaByte << 24U) | (b << 16U) | (g << 8U) | r;
}

} // namespace

std::array<int, 3> projectionBiomeSamplingPadding() {
    static auto const padding = [] {
        std::array<int, 3> result{2, 2, 2};
        for (auto const pattern : {BiomeColorSampling::Pattern::Single,
                 BiomeColorSampling::Pattern::CornersAndMidpointsRange4,
                 BiomeColorSampling::Pattern::Grid3x3, BiomeColorSampling::Pattern::CrossRange8}) {
            for (auto const& offset : BiomeColorSampling::getPattern(pattern)) {
                result[0] = std::max(result[0], std::abs(offset.x));
                result[1] = std::max(result[1], std::abs(offset.y));
                result[2] = std::max(result[2], std::abs(offset.z));
            }
        }
        return result;
    }();
    return padding;
}

bool projectionUsesBiomeTint(Block const& block) {
    auto const method = block.getBlockType().mTintMethod;
    return method == TintMethod::Grass || method == TintMethod::DefaultFoliage
        || method == TintMethod::BirchFoliage || method == TintMethod::EvergreenFoliage
        || method == TintMethod::DryFoliage;
}

void buildCorrectionSectionMeshes(
    ProjectionState&,
    Tessellator&,
    std::size_t,
    Tessellator::UploadMode,
    ProjectionSectionBuildSettings const&
);

void buildLiquidProxySectionMesh(
    ProjectionState&,
    Tessellator&,
    std::size_t,
    Tessellator::UploadMode,
    ProjectionSectionBuildSettings const&
);

void buildBlockEntityPlaceholderSectionMesh(
    ProjectionState&,
    Tessellator&,
    std::size_t,
    Tessellator::UploadMode,
    ProjectionSectionBuildSettings const&,
    std::span<std::size_t const>
);

void buildProjectionSection(
    ProjectionState&                      state,
    Tessellator&                          tessellator,
    BlockTessellator&                     blockTessellator,
    BlockSource&                          region,
    std::size_t                           section,
    Tessellator::UploadMode               uploadMode,
    ProjectionSectionBuildSettings const& sectionBuildSettings
) {
    state.sections[section].meshBounds = {};
    auto const mirror                   = sectionBuildSettings.mirror;
    auto const rotation                 = sectionBuildSettings.rotation;
    auto const mirrorMode               = sectionBuildSettings.mirrorMode;
    auto const rotationTurns            = sectionBuildSettings.rotationTurns;
    auto const offsetX                  = sectionBuildSettings.offsetX;
    auto const offsetY                  = sectionBuildSettings.offsetY;
    auto const offsetZ                  = sectionBuildSettings.offsetZ;
    auto const identityTransform        = sectionBuildSettings.identityTransform;
    LegacyStructureSettings sectionTransformSettings{
        mirror,
        rotation,
        nullptr,
        BoundingBox{}
    };
    struct LayeredBlock {
        Block const*                  block{};
        BlockPos                      position{};
        BlockRenderLayer              layer{BlockRenderLayer::RenderlayerOpaque};
        RenderBucket bucket{RenderBucket::Opaque};
        std::size_t                   structureIndex{};
        bool                          bubbleColumn{};
    };
    // Blocks whose model produced no geometry during tessellation
    // (block-entity blocks such as chests and signs) get a placeholder.
    std::vector<std::size_t> failedTessellationIndices;
    std::vector<LayeredBlock> layeredBlocks;
    std::array<std::size_t, static_cast<std::size_t>(RenderBucket::Count)> bucketCounts{};
    layeredBlocks.reserve(state.sectionBlockIndices[section].size());
    for (auto const index : state.sectionBlockIndices[section]) {
        auto const correctionState = (*state.correctionStates)[index];
        // Never draw a projected block model on top of an existing
        // world block. Correct blocks disappear; wrong type/state use
        // only their red/yellow outline below. This removes the
        // coincident textured surfaces that caused correction flicker.
        if (correctionState == CorrectionState::Correct
            || correctionState == CorrectionState::WrongType
            || correctionState == CorrectionState::WrongState) {
            continue;
        }
        auto const& entry = state.structure->renderBlocks[index];
        auto const transformed = transformStructurePosition(entry, *state.structure, mirrorMode, rotationTurns);
        BlockPos const position{
            state.anchor.x + offsetX + transformed.x,
            state.anchor.y + offsetY + transformed.y,
            state.anchor.z + offsetZ + transformed.z
        };
        auto const appendBlock = [&](Block const* source) {
            auto const* transformedBlock = transformExpectedBlock(source, sectionTransformSettings, identityTransform);
            if (!transformedBlock) return;
            auto const& typeName = transformedBlock->getTypeName();
            if (typeName == VanillaBlockTypeIds::PistonArmCollision().getString()
                || typeName == VanillaBlockTypeIds::StickyPistonArmCollision().getString()) {
                return;
            }
            // Only opaque full blocks can use the enclosed-cell shortcut.
            // Partial blocks retain vanilla tessellation.
            if (state.expectedWorldBlocks && projectionSolidCellEnclosed(
                    transformedBlock->getBlockType().mIsOpaqueFullBlock,
                    [&](int dx, int dy, int dz) {
                        auto const found = state.expectedWorldBlocks->find(
                            std::tuple{position.x + dx, position.y + dy, position.z + dz}
                        );
                        return found != state.expectedWorldBlocks->end() && found->second
                            && found->second->getBlockType().mIsOpaqueFullBlock;
                    }
                )) return;
            auto const* graphics = BlockGraphics::getForBlock(*transformedBlock);
            auto const layer = graphics
                ? graphics->getRenderLayer(region, position)
                : (transformedBlock->getBlockType().mIsOpaqueFullBlock
                    ? BlockRenderLayer::RenderlayerOpaque
                    : BlockRenderLayer::RenderlayerAlphatest);
            layeredBlocks.push_back({transformedBlock, position, layer, renderBucketFor(layer), index,
                typeName == "minecraft:bubble_column"});
            ++bucketCounts[static_cast<std::size_t>(layeredBlocks.back().bucket)];
        };
        appendBlock(entry.block);
    }
    BlockPos const origin{
        state.anchor.x + offsetX,
        state.anchor.y + offsetY,
        state.anchor.z + offsetZ
    };
    constexpr std::array<char const*, static_cast<std::size_t>(RenderBucket::Count)>
        meshNames{
            "LHoloOpaque",
            "LHoloAlpha",
            "LHoloAlphaOneSided",
            "LHoloBlend"
        };
    ScopedTessellationBlocks tessellationBlocksScope(
        *state.expectedWorldBlocks,
        *state.expectedWorldBlockActors
    );
    auto& biomeRefresh = state.sections[section].biomeRefresh;
    auto const previousBiomeRefresh = biomeRefresh;
    biomeRefresh = {};
    bool hasBiomeTint{};
    auto const biomePadding = projectionBiomeSamplingPadding();
    for (auto const& layered : layeredBlocks) {
        if (!projectionUsesBiomeTint(*layered.block)) continue;
        std::array<int, 3> const position{layered.position.x, layered.position.y, layered.position.z};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            biomeRefresh.minimum[axis] = hasBiomeTint
                ? std::min(biomeRefresh.minimum[axis], position[axis] - biomePadding[axis])
                : position[axis] - biomePadding[axis];
            biomeRefresh.maximum[axis] = hasBiomeTint
                ? std::max(biomeRefresh.maximum[axis], position[axis] + biomePadding[axis])
                : position[axis] + biomePadding[axis];
        }
        hasBiomeTint = true;
    }
    // Record readiness before sampling. A chunk view retains only the chunks
    // available when it was created; later arrivals need a fresh mesh task.
    if (hasBiomeTint) {
        biomeRefresh.pending = !region.areChunksFullyLoaded(
            BlockPos{biomeRefresh.minimum[0], biomeRefresh.minimum[1], biomeRefresh.minimum[2]},
            BlockPos{biomeRefresh.maximum[0], biomeRefresh.maximum[1], biomeRefresh.maximum[2]});
    }
    for (std::size_t bucketIndex = 0;
         bucketIndex < static_cast<std::size_t>(RenderBucket::Count);
         ++bucketIndex) {
        // Fully enclosed sections need no terrain tessellation at all. Mixed
        // sections reserve only the vertices for this bucket, not all four.
        if (bucketCounts[bucketIndex] == 0) {
            state.sections[section].meshes[bucketIndex].reset();
            continue;
        }
        tessellator.begin(
            Tessellator::DebugContextCallback{},
            mce::PrimitiveMode::QuadList,
            std::max(128, static_cast<int>(bucketCounts[bucketIndex] * 24)),
            false
        );
        bool bucketTessellated{};
        auto const bucket = static_cast<RenderBucket>(bucketIndex);
        for (auto const& layered : layeredBlocks) {
            if (layered.bucket != bucket) continue;
            blockTessellator.mRenderingLayer = static_cast<int>(layered.layer);
            auto const tintMethod = layered.block->getBlockType().mTintMethod;
            std::optional<std::uint32_t> foliageTint;
            if (tintMethod == TintMethod::DefaultFoliage
                || tintMethod == TintMethod::BirchFoliage
                || tintMethod == TintMethod::EvergreenFoliage
                || tintMethod == TintMethod::DryFoliage) {
                // The 26.51 headers expose no buildBiomeWeights entry point,
                // and this standalone tessellator's biome cache is never
                // populated, so pass null and let the policy sample the biome
                // color fresh from the region for this position.
                foliageTint = toAbgr(BiomeColorSampling::getTessellationPolicy(tintMethod).get(
                    *layered.block,
                    region,
                    layered.position,
                    nullptr
                ));
            }
            // Normalize native wall arms/posts and flattened fence/pane arms
            // against the projected neighborhood. Java wall states may differ
            // from Bedrock, and palettes omit flattened connection states.
            // This scope supplies projected blocks to native reads; region
            // writes attempted by the update are suppressed.
            Block const& renderBlock = withNeighborConnections(
                *layered.block, region, layered.position
            );
            auto const firstPosition = tessellator.mMeshData->mPositions.get().size();
            auto const firstColor = tessellator.mMeshData->mColors.get().size();
            auto rendered = blockTessellator.tessellateInWorld(
                tessellator, renderBlock, layered.position, true
            );
            if (layered.bubbleColumn && tessellator.mMeshData->mPositions.get().size() == firstPosition) {
                // Keep the normal world/cache setup, then use the native bubble
                // builder when generic shape dispatch emits nothing. This is
                // one call per build, not a queued refresh or retry loop.
                rendered = blockTessellator.tessellateBubbleColumnInWorld(
                    tessellator, renderBlock, layered.position);
            }
            // Several legacy shape tessellators (notably doors) return
            // false after successfully appending vertices. The return
            // value describes the dispatch path, not mesh production.
            // Trust the actual mesh delta so those models are retained.
            auto const geometryAdded =
                tessellator.mMeshData->mPositions.get().size() > firstPosition;
            if (!geometryAdded && (!rendered || layered.bubbleColumn)) {
                // No terrain-atlas model: needs a placeholder hull.
                // Bubble columns are not block entities; a generic cube is not
                // a valid substitute for an intentionally empty native result.
                if (!layered.bubbleColumn) failedTessellationIndices.push_back(layered.structureIndex);
                continue;
            }
            auto& colors = tessellator.mMeshData->mColors.get();
            if (foliageTint) {
                for (std::size_t colorIndex = firstColor; colorIndex < colors.size(); ++colorIndex) {
                    colors[colorIndex] = modulateAbgr(colors[colorIndex], *foliageTint);
                }
            }
            ProjectionTintQuadObserver grassFaces;
            for (std::size_t colorIndex = firstColor; colorIndex < colors.size(); ++colorIndex) {
                // Preserve full projection alpha and observe grass faces in the same pass.
                if (tintMethod == TintMethod::Grass) grassFaces.observe(colors[colorIndex]);
                colors[colorIndex] |= 0xFF000000U;
            }
            // A black face is a symptom, not proof of missing lighting/biomes.
            // Its independent diagnostic flag permits only one auto refresh.
            biomeRefresh.blackGrassFace = biomeRefresh.blackGrassFace || grassFaces.hasBlackFace();
            bucketTessellated = true;
        }
        auto& destination = state.sections[section].meshes[bucketIndex];
        if (!bucketTessellated) {
            tessellator.end(
                Tessellator::UploadMode::Never,
                meshNames[bucketIndex],
                SupplementaryFieldAutoGenerationMode{0}
            );
            destination.reset();
            continue;
        }
        for (auto& vertex : tessellator.mMeshData->mPositions.get()) {
            vertex.x -= static_cast<float>(origin.x);
            vertex.y -= static_cast<float>(origin.y);
            vertex.z -= static_cast<float>(origin.z);
        }
        destination = std::make_unique<mce::Mesh>(tessellator.end(
            uploadMode,
            meshNames[bucketIndex],
            SupplementaryFieldAutoGenerationMode{1}
        ));
    }

    detail::buildLiquidProxySectionMesh(
        state, tessellator, section, uploadMode, sectionBuildSettings
    );
    detail::buildBlockEntityPlaceholderSectionMesh(
        state,
        tessellator,
        section,
        uploadMode,
        sectionBuildSettings,
        failedTessellationIndices
    );
    detail::buildCorrectionSectionMeshes(
        state, tessellator, section, uploadMode, sectionBuildSettings
    );
    auto& bounds = state.sections[section].meshBounds;
    bool completeBounds = true;
    auto includeMesh = [&](std::unique_ptr<mce::Mesh> const& mesh) {
        if (!mesh) return;
        auto const& positions = mesh->mMeshData.get().mPositions.get();
        // Some synchronous upload paths release their CPU vertices. In that
        // case visibility must fail open, never infer bounds from a subset.
        if (positions.empty() && mesh->mVertexCount.get().value_or(0u) > 0) completeBounds = false;
        for (auto const& v : positions) bounds.include({v.x, v.y, v.z});
    };
    for (auto const& mesh : state.sections[section].meshes) includeMesh(mesh);
    includeMesh(state.warningFillSectionMeshes[section]);
    includeMesh(state.correctionOutlineSectionMeshes[section]);
    includeMesh(state.wrongFillSectionMeshes[section]);
    includeMesh(state.wrongOutlineSectionMeshes[section]);
    includeMesh(state.liquidProxySectionMeshes[section]);
    includeMesh(state.blockEntityPlaceholderSectionMeshes[section]);
    if (!completeBounds) bounds.valid = false;
    biomeRefresh.sampleFailed = tessellationBlocksScope.biomeSampleFailed();
    finishProjectionBiomeRefresh(biomeRefresh, previousBiomeRefresh);
}

void buildLiquidProxySectionMesh(
    ProjectionState&                      state,
    Tessellator&                          tessellator,
    std::size_t                           section,
    Tessellator::UploadMode               uploadMode,
    ProjectionSectionBuildSettings const& settings
) {
    auto const mirrorMode        = settings.mirrorMode;
    auto const rotationTurns     = settings.rotationTurns;
    auto const offsetX           = settings.offsetX;
    auto const offsetY           = settings.offsetY;
    auto const offsetZ           = settings.offsetZ;
    auto const identityTransform = settings.identityTransform;
    LegacyStructureSettings sectionTransformSettings{
        settings.mirror,
        settings.rotation,
        nullptr,
        BoundingBox{}
    };
    // Textured liquid proxy hulls. LHolo never lies to the vanilla
    // world or chunk pipeline (that leaks into gameplay), so missing
    // liquids draw as translucent hulls here. The hulls reuse the
    // vanilla terrain-atlas water/lava tiles and travel the exact
    // material path used for glass, keeping them purely cosmetic.
    std::vector<std::size_t> liquidProxyIndices;
    for (auto const index : state.sectionBlockIndices[section]) {
        if (state.structure->renderBlocks[index].liquid == nullptr) continue;
        if ((*state.correctionStates)[index] != CorrectionState::Missing) continue;
        liquidProxyIndices.push_back(index);
    }
    if (!liquidProxyIndices.empty()) {
        tessellator.begin(
            Tessellator::DebugContextCallback{},
            mce::PrimitiveMode::QuadList,
            static_cast<int>(liquidProxyIndices.size() * 24),
            false
        );
        for (auto const index : liquidProxyIndices) {
            auto const& entry = state.structure->renderBlocks[index];
            auto const* expectedLiquid = transformExpectedBlock(entry.liquid, sectionTransformSettings, identityTransform);
            if (!expectedLiquid) continue;

            bool const isLava = expectedLiquid->getTypeName().find("lava") != std::string::npos
                || expectedLiquid->getBlockType().mMaterial.mSuperHot;

            // Route B: Water uses standard blue (#3F76E4) to tint grayscale water tiles.
            // Lava uses fiery molten orange (#FF8010), ensuring grayscale flowing lava tiles
            // and fallback tiles display rich, warm molten color rather than rendering white.
            auto const baseRgb = isLava ? LiquidLavaTintAbgrRgb : LiquidWaterTintAbgrRgb;
            // Preserve the liquid appearance used at full projection opacity.
            float const effectiveAlpha = isLava ? 0.85f : 0.65f;
            auto const alphaByte = static_cast<std::uint32_t>(std::lround(effectiveAlpha * 255.0f));

            // Directional shading preserves 3D volume perception in transparent mode.
            auto const topColor    = applyShade(baseRgb, 1.00f, alphaByte);
            auto const bottomColor = applyShade(baseRgb, 0.60f, alphaByte);
            auto const nsColor     = applyShade(baseRgb, 0.85f, alphaByte);
            auto const ewColor     = applyShade(baseRgb, 0.75f, alphaByte);

            auto const* graphics = BlockGraphics::getForBlock(*expectedLiquid);
            auto const* uvSetStill = graphics ? &graphics->getTexture(0, 0) : nullptr;
            auto const* uvSetFlow = graphics ? &graphics->getTexture(2, 0) : nullptr;
            if (!uvSetFlow) uvSetFlow = uvSetStill;

            float const u0_s = uvSetStill ? uvSetStill->_u0 : 0.0f;
            float const v0_s = uvSetStill ? uvSetStill->_v0 : 0.0f;
            float const u1_s = uvSetStill ? uvSetStill->_u1 : 0.0f;
            float const v1_s = uvSetStill ? uvSetStill->_v1 : 0.0f;

            float const u0_f = uvSetFlow ? uvSetFlow->_u0 : u0_s;
            float const v0_f = uvSetFlow ? uvSetFlow->_v0 : v0_s;
            float const u1_f = uvSetFlow ? uvSetFlow->_u1 : u1_s;
            float const v1_f = uvSetFlow ? uvSetFlow->_v1 : v1_s;
            float const dv_f = v1_f - v0_f;

            auto const p = transformStructurePosition(entry, *state.structure, mirrorMode, rotationTurns);
            BlockPos const worldPosition{
                state.anchor.x + offsetX + p.x,
                state.anchor.y + offsetY + p.y,
                state.anchor.z + offsetZ + p.z
            };
            auto const neighborEntry = [&](int dx, int dy, int dz)
                -> structure::LoadedStructure::RenderBlock const* {
                auto const found = state.expectedWorldBlockIndices->find(std::tuple{
                    worldPosition.x + dx, worldPosition.y + dy, worldPosition.z + dz
                });
                return found == state.expectedWorldBlockIndices->end()
                    ? nullptr : &state.structure->renderBlocks[found->second];
            };
            auto const neighborIsSameLiquid = [&](int dx, int dy, int dz) {
                auto const* neighbor = neighborEntry(dx, dy, dz);
                if (!neighbor || !neighbor->liquid) return false;
                auto const* transformed = transformExpectedBlock(neighbor->liquid, sectionTransformSettings, identityTransform);
                if (!transformed) return false;
                auto const isLavaNeighbor = transformed->getTypeName().find("lava") != std::string::npos
                    || transformed->getBlockType().mMaterial.mSuperHot;
                return isLavaNeighbor == isLava;
            };

            // Flow-aware surface. Source and submerged cells stay full;
            // flowing cells taper by liquid_depth, and each top corner is
            // averaged from the surrounding same-liquid columns (an air
            // column pulls a corner down toward the spill). The result is
            // a surface that slopes downhill, showing the flow direction.
            constexpr float surface = 8.0f / 9.0f;
            auto const columnHeight = [&](int dx, int dz) -> float {
                auto const* n = (dx == 0 && dz == 0) ? &entry : neighborEntry(dx, 0, dz);
                if (!n) return 0.0f;
                if (!n->liquid) return -1.0f;
                auto const* t = transformExpectedBlock(n->liquid, sectionTransformSettings, identityTransform);
                if (!t) return -1.0f;
                auto const isLavaNeighbor = t->getTypeName().find("lava") != std::string::npos
                    || t->getBlockType().mMaterial.mSuperHot;
                if (isLavaNeighbor != isLava) return -1.0f;
                if (neighborIsSameLiquid(dx, 1, dz)) return 1.0f;  // submerged
                return getFluidHeight(getLiquidDepth(*t));
            };
            auto const cornerHeight = [&](int dx, int dz) -> float {
                float best = -1.0f, sum = 0.0f;
                int   count = 0;
                int const offsets[4][2] = {{0, 0}, {dx, 0}, {0, dz}, {dx, dz}};
                for (auto const& o : offsets) {
                    float const h = columnHeight(o[0], o[1]);
                    if (h < 0.0f) continue;  // solid: does not affect the surface
                    best = std::max(best, h);
                    sum += h;
                    ++count;
                }
                if (best >= surface) return best;  // a source/full column keeps it high
                return count > 0 ? sum / static_cast<float>(count) : surface;
            };

            float const x0 = static_cast<float>(p.x);
            float const y0 = static_cast<float>(p.y);
            float const z0 = static_cast<float>(p.z);
            float const x1 = static_cast<float>(p.x + 1);
            float const z1 = static_cast<float>(p.z + 1);

            // Per-corner top heights (world Y). c<x><z>: x0/x1, z0/z1.
            float const yc00 = y0 + cornerHeight(-1, -1);
            float const yc10 = y0 + cornerHeight( 1, -1);
            float const yc01 = y0 + cornerHeight(-1,  1);
            float const yc11 = y0 + cornerHeight( 1,  1);

            auto addQuad = [&](
                Vec3 const& p0, float uA, float vA,
                Vec3 const& p1, float uB, float vB,
                Vec3 const& p2, float uC, float vC,
                Vec3 const& p3, float uD, float vD,
                std::uint32_t color
            ) {
                setColorAbgr(tessellator, color);
                tessellator.tex2({uA, vA}); tessellator.vertex(p0.x, p0.y, p0.z);
                tessellator.tex2({uB, vB}); tessellator.vertex(p1.x, p1.y, p1.z);
                tessellator.tex2({uC, vC}); tessellator.vertex(p2.x, p2.y, p2.z);
                tessellator.tex2({uD, vD}); tessellator.vertex(p3.x, p3.y, p3.z);
            };

            auto const* bottom = neighborEntry(0, -1, 0);
            bool const bottomIsSolid = bottom && bottom->block && !bottom->block->isAir();
            bool const bottomIsSameLiquid = neighborIsSameLiquid(0, -1, 0);
            if (!bottomIsSolid && !bottomIsSameLiquid) {
                addQuad(
                    {x0, y0, z1}, u0_s, v1_s,
                    {x0, y0, z0}, u0_s, v0_s,
                    {x1, y0, z0}, u1_s, v0_s,
                    {x1, y0, z1}, u1_s, v1_s,
                    bottomColor
                );
            }
            if (!neighborIsSameLiquid(0, 1, 0)) {
                addQuad(
                    {x0, yc00, z0}, u0_s, v0_s,
                    {x0, yc01, z1}, u0_s, v1_s,
                    {x1, yc11, z1}, u1_s, v1_s,
                    {x1, yc10, z0}, u1_s, v0_s,
                    topColor
                );
            }

            auto addSideFace = [&](
                float xA, float zA, float ycA,
                float xB, float zB, float ycB,
                std::uint32_t color
            ) {
                float const hA = std::clamp(ycA - y0, 0.0f, 1.0f);
                float const hB = std::clamp(ycB - y0, 0.0f, 1.0f);
                addQuad(
                    {xA, y0,  zA}, u0_f, v1_f,
                    {xA, ycA, zA}, u0_f, v1_f - dv_f * hA,
                    {xB, ycB, zB}, u1_f, v1_f - dv_f * hB,
                    {xB, y0,  zB}, u1_f, v1_f,
                    color
                );
            };

            if (!neighborIsSameLiquid( 0, 0, -1)) addSideFace(x0, z0, yc00, x1, z0, yc10, nsColor);
            if (!neighborIsSameLiquid( 0, 0,  1)) addSideFace(x1, z1, yc11, x0, z1, yc01, nsColor);
            if (!neighborIsSameLiquid(-1, 0,  0)) addSideFace(x0, z1, yc01, x0, z0, yc00, ewColor);
            if (!neighborIsSameLiquid( 1, 0,  0)) addSideFace(x1, z0, yc10, x1, z1, yc11, ewColor);
        }
        state.liquidProxySectionMeshes[section] = std::make_unique<mce::Mesh>(tessellator.end(
            uploadMode,
            "LHoloLiquidProxy",
            SupplementaryFieldAutoGenerationMode{0}
        ));
    } else {
        state.liquidProxySectionMeshes[section].reset();
    }

}

void buildBlockEntityPlaceholderSectionMesh(
    ProjectionState&                      state,
    Tessellator&                          tessellator,
    std::size_t                           section,
    Tessellator::UploadMode               uploadMode,
    ProjectionSectionBuildSettings const& settings,
    std::span<std::size_t const>           failedTessellationIndices
) {
    auto const mirrorMode        = settings.mirrorMode;
    auto const rotationTurns     = settings.rotationTurns;
    auto const identityTransform = settings.identityTransform;
    LegacyStructureSettings sectionTransformSettings{
        settings.mirror,
        settings.rotation,
        nullptr,
        BoundingBox{}
    };
    // Blocks that tessellated to nothing (block-entity blocks such as
    // chests and signs) get a textured placeholder hull from their
    // BlockGraphics tile so the projection still shows them. Blocks
    // that render normally (hoppers, beds, ...) are left untouched.
    std::vector<std::size_t> blockEntityIndices;
    for (auto const index : failedTessellationIndices) {
        if ((*state.correctionStates)[index] != CorrectionState::Missing) continue;
        if ((*state.blockActorRendererAvailable)[index]) continue;
        blockEntityIndices.push_back(index);
    }
    if (!blockEntityIndices.empty()) {
        tessellator.begin(
            Tessellator::DebugContextCallback{},
            mce::PrimitiveMode::QuadList,
            static_cast<int>(blockEntityIndices.size() * 24),
            false
        );
        constexpr auto tint = 0xFFFFFFFFU;
        for (auto const index : blockEntityIndices) {
            auto const& entry = state.structure->renderBlocks[index];
            auto const* expectedBlock = transformExpectedBlock(entry.block, sectionTransformSettings, identityTransform);
            if (!expectedBlock) continue;
            auto const* graphics = BlockGraphics::getForBlock(*expectedBlock);
            auto const* uvSet = graphics ? &graphics->getTexture(0, 0) : nullptr;
            auto const p = transformStructurePosition(entry, *state.structure, mirrorMode, rotationTurns);
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
            constexpr auto dimColor = 0xFF888888U;
            auto addFace = [&](
                Vec3 const& a, Vec3 const& b, Vec3 const& c, Vec3 const& d, bool isFront
            ) {
                auto const color = isFront ? frontColor : dimColor;
                setColorAbgr(tessellator, color);
                tessellator.tex2({u0, v0}); tessellator.vertex(a.x, a.y, a.z);
                tessellator.tex2({u0, v1}); tessellator.vertex(b.x, b.y, b.z);
                tessellator.tex2({u1, v1}); tessellator.vertex(c.x, c.y, c.z);
                tessellator.tex2({u1, v0}); tessellator.vertex(d.x, d.y, d.z);
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
        state.blockEntityPlaceholderSectionMeshes[section] = std::make_unique<mce::Mesh>(tessellator.end(
            uploadMode,
            "LHoloBlockEntityPlaceholder",
            SupplementaryFieldAutoGenerationMode{0}
        ));
    } else {
        state.blockEntityPlaceholderSectionMeshes[section].reset();
    }

}

void buildCorrectionSectionMeshes(
    ProjectionState&                      state,
    Tessellator&                          tessellator,
    std::size_t                           section,
    Tessellator::UploadMode               uploadMode,
    ProjectionSectionBuildSettings const& settings
) {
    // Resolve each cell's six neighbors once. Fill and outline share the same
    // surface mask, so interior cells never enter either tessellation loop.
    struct ShellCell {
        BlockPos position;
        std::uint32_t color;
        unsigned char faces;
    };
    std::array<std::vector<ShellCell>, 2> shells; // missing, wrong/extra
    auto const neighborPriority = [&](BlockPos const& p, int dx, int dy, int dz) {
        BlockPos const world{
            state.anchor.x + settings.offsetX + p.x + dx,
            state.anchor.y + settings.offsetY + p.y + dy,
            state.anchor.z + settings.offsetZ + p.z + dz
        };
        auto const expected = state.expectedWorldBlockIndices->find(std::tuple{world.x, world.y, world.z});
        auto priority = expected == state.expectedWorldBlockIndices->end()
            ? 0 : correctionPriority((*state.correctionStates)[expected->second]);
        auto const local = inverseTransformStructurePosition(
            BlockPos{p.x + dx, p.y + dy, p.z + dz}, *state.structure, settings.mirrorMode, settings.rotationTurns
        );
        if (state.extraBlockPositions.contains(std::tuple{local.x, local.y, local.z})) {
            priority = std::max(priority, 2);
        }
        return priority;
    };
    auto appendShell = [&](BlockPos const& local, int priority, std::uint32_t color, std::size_t bucket) {
        auto const p = transformStructurePosition(local, *state.structure, settings.mirrorMode, settings.rotationTurns);
        auto const faces = projectionShellFaces(priority, [&](int dx, int dy, int dz) {
            return neighborPriority(p, dx, dy, dz);
        });
        if (faces) shells[bucket].push_back({p, color, faces});
    };
    for (auto const index : state.sectionBlockIndices[section]) {
        auto const correction = (*state.correctionStates)[index];
        auto const priority = correctionPriority(correction);
        if (!priority) continue;
        auto const& entry = state.structure->renderBlocks[index];
        // Pure liquids already have a textured proxy hull.
        if (correction == CorrectionState::Missing && !entry.block && entry.liquid) continue;
        auto const color = correction == CorrectionState::Missing ? MissingColorAbgrRgb
            : correction == CorrectionState::WrongState ? WrongStateColorAbgrRgb : WrongBlockColorAbgrRgb;
        appendShell(BlockPos{entry.x, entry.y, entry.z}, priority, color,
            correction == CorrectionState::Missing ? 0 : 1);
    }
    for (auto const& [x, y, z] : state.sectionExtraBlockPositions[section]) {
        appendShell(BlockPos{x, y, z}, 2, ExtraColorAbgrRgb, 1);
    }
    auto addVertex = [&](Vec3 const& v) {
        tessellator.tex2({0.5f, 0.5f});
        tessellator.vertex(v.x, v.y, v.z);
    };
    auto edge = [&](Vec3 const& a, Vec3 const& b) { addVertex(a); addVertex(b); };
    auto face = [&](Vec3 const& a, Vec3 const& b, Vec3 const& c, Vec3 const& d) {
        addVertex(a); addVertex(b); addVertex(c); addVertex(d);
    };
    auto buildShell = [&](std::vector<ShellCell> const& cells, bool outline) -> std::unique_ptr<mce::Mesh> {
        if (cells.empty()) return nullptr;
        auto const opacity = outline ? settings.correctionOutlineOpacity : settings.correctionFillOpacity;
        if (opacity <= 0.0f) return nullptr;
        tessellator.begin(Tessellator::DebugContextCallback{},
            outline ? mce::PrimitiveMode::LineList : mce::PrimitiveMode::QuadList,
            static_cast<int>(cells.size() * 24), false);
        for (auto const& cell : cells) {
            auto const& p = cell.position;
            float const x0 = static_cast<float>(p.x), y0 = static_cast<float>(p.y), z0 = static_cast<float>(p.z);
            float const x1 = x0 + 1, y1 = y0 + 1, z1 = z0 + 1;
            bool const nz = cell.faces & 1, pz = cell.faces & 2;
            bool const nx = cell.faces & 4, px = cell.faces & 8;
            bool const ny = cell.faces & 16, py = cell.faces & 32;
            setColorAbgr(tessellator, withAlpha(cell.color, opacity));
            if (outline) {
                if (ny || nz) edge({x0,y0,z0},{x1,y0,z0});
                if (px || nz) edge({x1,y0,z0},{x1,y1,z0});
                if (py || nz) edge({x1,y1,z0},{x0,y1,z0});
                if (nx || nz) edge({x0,y1,z0},{x0,y0,z0});
                if (ny || pz) edge({x0,y0,z1},{x1,y0,z1});
                if (px || pz) edge({x1,y0,z1},{x1,y1,z1});
                if (py || pz) edge({x1,y1,z1},{x0,y1,z1});
                if (nx || pz) edge({x0,y1,z1},{x0,y0,z1});
                if (nx || ny) edge({x0,y0,z0},{x0,y0,z1});
                if (px || ny) edge({x1,y0,z0},{x1,y0,z1});
                if (px || py) edge({x1,y1,z0},{x1,y1,z1});
                if (nx || py) edge({x0,y1,z0},{x0,y1,z1});
            } else {
                if (nz) face({x0,y0,z0}, {x0,y1,z0}, {x1,y1,z0}, {x1,y0,z0});
                if (pz) face({x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1}, {x0,y0,z1});
                if (nx) face({x0,y0,z1}, {x0,y1,z1}, {x0,y1,z0}, {x0,y0,z0});
                if (px) face({x1,y0,z0}, {x1,y1,z0}, {x1,y1,z1}, {x1,y0,z1});
                if (ny) face({x0,y0,z1}, {x0,y0,z0}, {x1,y0,z0}, {x1,y0,z1});
                if (py) face({x0,y1,z0}, {x0,y1,z1}, {x1,y1,z1}, {x1,y1,z0});
            }
        }
        return std::make_unique<mce::Mesh>(tessellator.end(uploadMode,
            outline ? "LHoloCorrectionOutline" : "LHoloWarningFill", SupplementaryFieldAutoGenerationMode{0}));
    };
    state.correctionOutlineSectionMeshes[section] = buildShell(shells[0], true);
    state.wrongOutlineSectionMeshes[section] = buildShell(shells[1], true);
    state.warningFillSectionMeshes[section] = buildShell(shells[0], false);
    state.wrongFillSectionMeshes[section] = buildShell(shells[1], false);
}

} // namespace lholo::projection::detail
