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
#include "projection/core/ProjectionState.h"
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
// ABGR byte order expected by Tessellator::colorABGR().
constexpr std::uint32_t MissingColorAbgrRgb    = 0x00E6B333U; // #33B3E6
constexpr std::uint32_t WrongBlockColorAbgrRgb = 0x003333FFU; // #FF3333
constexpr std::uint32_t WrongStateColorAbgrRgb = 0x001090FFU; // #FF9010

constexpr std::uint32_t LiquidWaterTintAbgrRgb = 0x00E4763FU; // #3F76E4
constexpr std::uint32_t LiquidLavaTintAbgrRgb  = 0x00FFFFFFU; // white

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

} // namespace

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

void buildStructureBoundsMesh(
    ProjectionState&,
    Tessellator&,
    Tessellator::UploadMode,
    ProjectionSectionBuildSettings const&
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
    auto const mirror                   = sectionBuildSettings.mirror;
    auto const rotation                 = sectionBuildSettings.rotation;
    auto const mirrorMode               = sectionBuildSettings.mirrorMode;
    auto const rotationTurns            = sectionBuildSettings.rotationTurns;
    auto const offsetX                  = sectionBuildSettings.offsetX;
    auto const offsetY                  = sectionBuildSettings.offsetY;
    auto const offsetZ                  = sectionBuildSettings.offsetZ;
    auto const structureOpacity         = sectionBuildSettings.structureOpacity;
    auto const identityTransform        = sectionBuildSettings.identityTransform;
    LegacyStructureSettings sectionTransformSettings;
    sectionTransformSettings.setMirror(mirror);
    sectionTransformSettings.setRotation(rotation);
    struct LayeredBlock {
        Block const*                  block{};
        BlockPos                      position{};
        BlockRenderLayer              layer{BlockRenderLayer::RenderlayerOpaque};
        RenderBucket bucket{RenderBucket::Opaque};
        std::size_t                   structureIndex{};
    };
    // Blocks whose model produced no geometry during tessellation
    // (block-entity blocks such as chests and signs) get a placeholder.
    std::vector<std::size_t> failedTessellationIndices;
    std::vector<LayeredBlock> layeredBlocks;
    layeredBlocks.reserve(state.sectionBlockIndices[section].size());
    for (auto const index : state.sectionBlockIndices[section]) {
        auto const correctionState = state.correctionStates[index];
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
    for (std::size_t bucketIndex = 0;
         bucketIndex < static_cast<std::size_t>(RenderBucket::Count);
         ++bucketIndex) {
        tessellator.cancel();
        tessellator.begin(
            Tessellator::DebugContextCallback{},
            std::max(128, static_cast<int>(layeredBlocks.size() * 24)),
            false
        );
        bool bucketTessellated{};
        auto const bucket = static_cast<RenderBucket>(bucketIndex);
        for (auto const& layered : layeredBlocks) {
            if (layered.bucket != bucket) continue;
            blockTessellator.setRenderLayer(static_cast<int>(layered.layer));
            auto const tintMethod = layered.block->getTintMethod();
            auto const usesBiomeTint = tintMethod == TintMethod::Grass
                || tintMethod == TintMethod::DefaultFoliage
                || tintMethod == TintMethod::BirchFoliage
                || tintMethod == TintMethod::EvergreenFoliage
                || tintMethod == TintMethod::DryFoliage;
            // A task owns a fresh BlockTessellator. Populate its biome
            // weights for every biome-tinted block so grass never reads
            // an empty or previous-position tint cache on the first build.
            if (usesBiomeTint) blockTessellator.buildBiomeWeights(layered.position);
            std::optional<std::uint32_t> foliageTint;
            if (tintMethod == TintMethod::DefaultFoliage
                || tintMethod == TintMethod::BirchFoliage
                || tintMethod == TintMethod::EvergreenFoliage
                || tintMethod == TintMethod::DryFoliage) {
                foliageTint = static_cast<std::uint32_t>(
                    BiomeColorSampling::getTessellationPolicy(tintMethod)
                        .get(
                            *layered.block,
                            region,
                            layered.position,
                            &blockTessellator.getBiomeTintCache()
                        )
                        .toABGR()
                );
            }
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
            if (foliageTint) {
                for (std::size_t colorIndex = firstColor; colorIndex < colors.size(); ++colorIndex) {
                    colors[colorIndex] = modulateAbgr(colors[colorIndex], *foliageTint);
                }
            }
            for (std::size_t colorIndex = firstColor; colorIndex < colors.size(); ++colorIndex) {
                colors[colorIndex] = (colors[colorIndex] & 0x00FFFFFFU) | (alpha << 24U);
            }
            bucketTessellated = true;
        }
        auto& destination = state.sections[section].meshes[bucketIndex];
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
            uploadMode,
            meshNames[bucketIndex],
            Tessellator::SupplementaryFieldAutoGenerationMode::NormalsAndTangents
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
    detail::buildStructureBoundsMesh(
        state, tessellator, uploadMode, sectionBuildSettings
    );
    if (tessellator.isTessellating()) tessellator.cancel();
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
    auto const structureOpacity  = settings.structureOpacity;
    auto const identityTransform = settings.identityTransform;
    LegacyStructureSettings sectionTransformSettings;
    sectionTransformSettings.setMirror(settings.mirror);
    sectionTransformSettings.setRotation(settings.rotation);
    // Textured liquid proxy hulls. LHolo never lies to the vanilla
    // world or chunk pipeline (that leaks into gameplay), so missing
    // liquids draw as translucent hulls here. The hulls reuse the
    // vanilla terrain-atlas water/lava tiles and travel the exact
    // material path used for glass, keeping them purely cosmetic.
    std::vector<std::size_t> liquidProxyIndices;
    for (auto const index : state.sectionBlockIndices[section]) {
        if (state.structure->renderBlocks[index].liquid == nullptr) continue;
        if (state.correctionStates[index] != CorrectionState::Missing) continue;
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
            auto const& entry = state.structure->renderBlocks[index];
            auto const* expectedLiquid = transformExpectedBlock(entry.liquid, sectionTransformSettings, identityTransform);
            if (!expectedLiquid) continue;
            auto const* graphics = BlockGraphics::getForBlock(*expectedLiquid);
            auto const* uvSet = graphics ? &graphics->getTexture(0, 0) : nullptr;
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
                return transformed && transformed->getTypeName() == expectedLiquid->getTypeName();
            };
            // Flow-aware surface. Source and submerged cells stay full;
            // flowing cells taper by liquid_depth, and each top corner is
            // averaged from the surrounding same-liquid columns (an air
            // column pulls a corner down toward the spill). The result is
            // a surface that slopes downhill, showing the flow direction.
            constexpr float surface = 8.0f / 9.0f;
            auto const liquidDepth = [](Block const& block) -> int {
                for (auto const& [key, value] : block.getSerializationId()) {
                    if (key != "states" || !value.hold<::CompoundTag>()) continue;
                    for (auto const& [stateKey, stateValue] : value.get<::CompoundTag>()) {
                        if (stateKey == "liquid_depth" && stateValue.getId() == ::Tag::Type::Int)
                            return stateValue.get<::IntTag>().data;
                    }
                }
                return 0;
            };
            auto const fluidHeight = [](int depth) -> float {
                if (depth <= 0) return 8.0f / 9.0f;   // source
                if (depth >= 8) return 1.0f;          // falling counts as full
                return (8.0f - static_cast<float>(depth)) / 9.0f;
            };
            // Height (0..1) of the same-liquid column at (dx,dz); -1 for a
            // solid/other block (ignored), 0 for air (spill).
            auto const columnHeight = [&](int dx, int dz) -> float {
                auto const* n = (dx == 0 && dz == 0) ? &entry : neighborEntry(dx, 0, dz);
                if (!n) return 0.0f;
                if (!n->liquid) return -1.0f;
                auto const* t = transformExpectedBlock(n->liquid, sectionTransformSettings, identityTransform);
                if (!t || t->getTypeName() != expectedLiquid->getTypeName()) return -1.0f;
                if (neighborIsSameLiquid(dx, 1, dz)) return 1.0f;  // submerged
                return fluidHeight(liquidDepth(*t));
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
            auto const tint = expectedLiquid->getMaterial().isSuperHot()
                ? (LiquidLavaTintAbgrRgb | (alpha << 24U))
                : (LiquidWaterTintAbgrRgb | (alpha << 24U));
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
                addLiquidFace({x0,yc00,z0}, {x0,yc01,z1}, {x1,yc11,z1}, {x1,yc10,z0});
            if (!neighborIsSameLiquid(0, 0, -1))
                addLiquidFace({x0,y0,z0}, {x0,yc00,z0}, {x1,yc10,z0}, {x1,y0,z0});
            if (!neighborIsSameLiquid(0, 0, 1))
                addLiquidFace({x1,y0,z1}, {x1,yc11,z1}, {x0,yc01,z1}, {x0,y0,z1});
            if (!neighborIsSameLiquid(-1, 0, 0))
                addLiquidFace({x0,y0,z1}, {x0,yc01,z1}, {x0,yc00,z0}, {x0,y0,z0});
            if (!neighborIsSameLiquid(1, 0, 0))
                addLiquidFace({x1,y0,z0}, {x1,yc10,z0}, {x1,yc11,z1}, {x1,y0,z1});
        }
        state.liquidProxySectionMeshes[section] = std::make_unique<mce::Mesh>(tessellator.end(
            uploadMode,
            "LHoloLiquidProxy",
            Tessellator::SupplementaryFieldAutoGenerationMode::None
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
    auto const structureOpacity  = settings.structureOpacity;
    auto const identityTransform = settings.identityTransform;
    LegacyStructureSettings sectionTransformSettings;
    sectionTransformSettings.setMirror(settings.mirror);
    sectionTransformSettings.setRotation(settings.rotation);
    // Blocks that tessellated to nothing (block-entity blocks such as
    // chests and signs) get a textured placeholder hull from their
    // BlockGraphics tile so the projection still shows them. Blocks
    // that render normally (hoppers, beds, ...) are left untouched.
    std::vector<std::size_t> blockEntityIndices;
    for (auto const index : failedTessellationIndices) {
        if (state.correctionStates[index] != CorrectionState::Missing) continue;
        if (state.blockActorRendererAvailable[index]) continue;
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
        state.blockEntityPlaceholderSectionMeshes[section] = std::make_unique<mce::Mesh>(tessellator.end(
            uploadMode,
            "LHoloBlockEntityPlaceholder",
            Tessellator::SupplementaryFieldAutoGenerationMode::None
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
    // Split the correction geometry into "missing" and "wrong" (WrongType +
    // WrongState) so the see-through option can X-ray only the wrong markers,
    // never the many "missing" outlines.
    auto const isWrongState = [](CorrectionState correction) {
        return correction == CorrectionState::WrongType
            || correction == CorrectionState::WrongState;
    };
    std::size_t missingCount{};
    std::size_t wrongCount{};
    for (auto const index : state.sectionBlockIndices[section]) {
        auto const correction = state.correctionStates[index];
        if (isWrongState(correction)) ++wrongCount;
        else if (correction == CorrectionState::Missing) ++missingCount;
    }
    if (missingCount == 0 && wrongCount == 0) {
        state.warningFillSectionMeshes[section].reset();
        state.correctionOutlineSectionMeshes[section].reset();
        state.wrongFillSectionMeshes[section].reset();
        state.wrongOutlineSectionMeshes[section].reset();
        return;
    }

    constexpr float outlineInset  = 0.0f;
    constexpr float outlineExtent = 1.0f;
    auto addOutlineEdge = [&](Vec3 const& first, Vec3 const& second) {
        tessellator.vertex(first);
        tessellator.vertex(second);
    };
    // Use true LineList geometry rendered with the vanilla outline material.
    auto buildOutline = [&](bool wantWrong, std::size_t count) -> std::unique_ptr<mce::Mesh> {
        if (count == 0) return nullptr;
        tessellator.begin(
            Tessellator::DebugContextCallback{},
            mce::PrimitiveMode::LineList,
            static_cast<int>(count * 24),
            false
        );
        for (auto const index : state.sectionBlockIndices[section]) {
            auto const correction = state.correctionStates[index];
            auto const priority = correctionPriority(correction);
            if (priority == 0) continue;
            if (isWrongState(correction) != wantWrong) continue;
            auto const& entry = state.structure->renderBlocks[index];
            // A missing pure-liquid cell is already communicated by its blue
            // translucent proxy hull; skip the duplicate outline.
            if (correction == CorrectionState::Missing && !entry.block && entry.liquid) continue;
            auto const p = transformStructurePosition(
                entry, *state.structure, settings.mirrorMode, settings.rotationTurns
            );
            auto const outlineColor = correction == CorrectionState::Missing
                ? withAlpha(MissingColorAbgrRgb, settings.correctionOutlineOpacity)
                : correction == CorrectionState::WrongState
                    ? withAlpha(WrongStateColorAbgrRgb, settings.correctionOutlineOpacity)
                    : withAlpha(WrongBlockColorAbgrRgb, settings.correctionOutlineOpacity);
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
        return std::make_unique<mce::Mesh>(tessellator.end(
            uploadMode,
            "LHoloCorrectionOutline",
            Tessellator::SupplementaryFieldAutoGenerationMode::None
        ));
    };
    state.correctionOutlineSectionMeshes[section] = buildOutline(false, missingCount);
    state.wrongOutlineSectionMeshes[section] = buildOutline(true, wrongCount);

    // Litematica-style correction fill: an exact untextured 1x1x1 cell overlay.
    // Rasterizer bias supplies depth separation at submission time.
    auto addFillFace = [&](Vec3 const& a, Vec3 const& b, Vec3 const& c, Vec3 const& d) {
        tessellator.vertex(a);
        tessellator.vertex(b);
        tessellator.vertex(c);
        tessellator.vertex(d);
    };
    auto buildFill = [&](bool wantWrong, std::size_t count) -> std::unique_ptr<mce::Mesh> {
        if (count == 0) return nullptr;
        tessellator.begin(
            Tessellator::DebugContextCallback{},
            mce::PrimitiveMode::QuadList,
            static_cast<int>(count * 24),
            false
        );
        for (auto const index : state.sectionBlockIndices[section]) {
            auto const correction = state.correctionStates[index];
            auto const priority = correctionPriority(correction);
            if (priority == 0) continue;
            if (isWrongState(correction) != wantWrong) continue;
            auto const& entry = state.structure->renderBlocks[index];
            if (correction == CorrectionState::Missing && !entry.block && entry.liquid) continue;
            auto const p = transformStructurePosition(
                entry, *state.structure, settings.mirrorMode, settings.rotationTurns
            );
            BlockPos const worldPosition{
                state.anchor.x + settings.offsetX + p.x,
                state.anchor.y + settings.offsetY + p.y,
                state.anchor.z + settings.offsetZ + p.z
            };
            // Face-culling stays global across categories so a wrong cell next
            // to a missing cell still hides the lower-priority shared face.
            auto const neighborPriority = [&](int dx, int dy, int dz) {
                auto const found = state.expectedWorldBlockIndices->find(std::tuple{
                    worldPosition.x + dx, worldPosition.y + dy, worldPosition.z + dz
                });
                return found == state.expectedWorldBlockIndices->end()
                    ? 0 : correctionPriority(state.correctionStates[found->second]);
            };
            float const x0 = static_cast<float>(p.x);
            float const y0 = static_cast<float>(p.y);
            float const z0 = static_cast<float>(p.z);
            float const x1 = static_cast<float>(p.x + 1);
            float const y1 = static_cast<float>(p.y + 1);
            float const z1 = static_cast<float>(p.z + 1);
            auto const fillColor = correction == CorrectionState::Missing
                ? withAlpha(MissingColorAbgrRgb, settings.correctionFillOpacity)
                : correction == CorrectionState::WrongState
                    ? withAlpha(WrongStateColorAbgrRgb, settings.correctionFillOpacity)
                    : withAlpha(WrongBlockColorAbgrRgb, settings.correctionFillOpacity);
            tessellator.colorABGR(static_cast<int>(fillColor));
            if (priority > neighborPriority(0, 0, -1)) addFillFace({x0,y0,z0}, {x0,y1,z0}, {x1,y1,z0}, {x1,y0,z0});
            if (priority > neighborPriority(0, 0, 1))  addFillFace({x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1}, {x0,y0,z1});
            if (priority > neighborPriority(-1, 0, 0)) addFillFace({x0,y0,z1}, {x0,y1,z1}, {x0,y1,z0}, {x0,y0,z0});
            if (priority > neighborPriority(1, 0, 0))  addFillFace({x1,y0,z0}, {x1,y1,z0}, {x1,y1,z1}, {x1,y0,z1});
            if (priority > neighborPriority(0, -1, 0)) addFillFace({x0,y0,z1}, {x0,y0,z0}, {x1,y0,z0}, {x1,y0,z1});
            if (priority > neighborPriority(0, 1, 0))  addFillFace({x0,y1,z0}, {x0,y1,z1}, {x1,y1,z1}, {x1,y1,z0});
        }
        return std::make_unique<mce::Mesh>(tessellator.end(
            uploadMode,
            "LHoloWarningFill",
            Tessellator::SupplementaryFieldAutoGenerationMode::None
        ));
    };
    state.warningFillSectionMeshes[section] = buildFill(false, missingCount);
    state.wrongFillSectionMeshes[section] = buildFill(true, wrongCount);
}

void buildStructureBoundsMesh(
    ProjectionState&                      state,
    Tessellator&                          tessellator,
    Tessellator::UploadMode               uploadMode,
    ProjectionSectionBuildSettings const& settings
) {
    if (uploadMode == Tessellator::UploadMode::Never || state.structureBoundsMesh) return;
    auto const rotated = settings.rotationTurns == 1 || settings.rotationTurns == 3;
    auto const width = static_cast<float>(rotated ? state.structure->sizeZ : state.structure->sizeX);
    auto const height = static_cast<float>(state.structure->sizeY);
    auto const depth = static_cast<float>(rotated ? state.structure->sizeX : state.structure->sizeZ);
    constexpr float expansion = 0.01f;
    float const x0 = -expansion, y0 = -expansion, z0 = -expansion;
    float const x1 = width + expansion;
    float const y1 = height + expansion;
    float const z1 = depth + expansion;
    tessellator.begin(
        Tessellator::DebugContextCallback{}, mce::PrimitiveMode::LineList, 24, false
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
    state.structureBoundsMesh = std::make_unique<mce::Mesh>(tessellator.end(
        uploadMode,
        "LHoloStructureBounds",
        Tessellator::SupplementaryFieldAutoGenerationMode::None
    ));
}

} // namespace lholo::projection::detail
