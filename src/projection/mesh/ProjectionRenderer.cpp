// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/mesh/ProjectionRenderer.h"

#include "projection/mesh/ProjectionCorrectionRenderer.h"

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/core/ProjectionState.h"
#include "projection/world/ProjectionVirtualWorld.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "ll/api/mod/NativeMod.h"
#include "plugin/LHolo.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/blockactor/BlockActorRenderDispatcher.h"
#include "mc/world/level/block/actor/component/IVanillaRenderBlockActorComponent.h"
#include "mc/client/renderer/game/ItemInHandRenderer.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/deps/minecraft_renderer/framebuilder/dragon/RenderMetadata.h"
#include "mc/client/renderer/RenderMaterialGroup.h"
#include "mc/deps/core/renderer/RenderMaterialInfo.h"
#include "mc/deps/minecraft_renderer/renderer/RenderMaterial.h"
#include <array>
#include <cstddef>

#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/OffscreenCaptureDescription.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"
#include "mc/deps/renderer/hal/interface/DepthStencilStateDescription.h"
#include "mc/world/level/block/actor/BlockActor.h"

namespace lholo::projection::detail {

namespace {

// 26.32: MaterialPtr's operator bool / operator* / operator-> were inlined
// out; the same state stays reachable through the public member chain.
// The SDK reconstructs OffscreenCaptureDescription as an empty struct, but
// the game object is 48 bytes (RenderMetadata stores it inline). Passing a
// one-byte temporary made the game read 47 bytes of stack garbage as its
// control block. Zeroed 48 bytes is the "no active capture" state (the old
// variant member held monostate, which is all-zero as well).
auto const& emptyOffscreenCaptureDescription() {
    static std::array<std::byte, 48> const empty{};
    return reinterpret_cast<OffscreenCaptureDescription const&>(empty);
}

auto& logger() {
    return LHolo::getInstance().getSelf().getLogger();
}

bool materialExists(mce::MaterialPtr const& material) {
    return material.mRenderMaterialInfoPtr.get() != nullptr;
}

mce::RenderMaterial* tryRenderMaterial(mce::MaterialPtr const& material) {
    auto const& info = material.mRenderMaterialInfoPtr.get();
    return info ? info->mPtr.get() : nullptr;
}

} // namespace

void submitProjectedBlockActorPass(
    ProjectionState&        state,
    BaseActorRenderContext& renderContext,
    BlockSource&            region,
    Vec3 const&             camera,
    bool                    renderAlphaLayer
) {
    if (state.projectedBlockActors.empty()) return;

    alignas(mce::MaterialPtr) static const std::byte sNoForcedMaterialStorage[sizeof(mce::MaterialPtr)]{};
    auto const& noForcedMaterial = *reinterpret_cast<mce::MaterialPtr const*>(sNoForcedMaterialStorage);
    auto& dispatcher = renderContext.mBlockEntityRenderDispatcher;
    ScopedTessellationBlocks blockActorWorldScope(
        *state.expectedWorldBlocks,
        *state.expectedWorldBlockActors
    );
    for (auto const& projected : state.projectedBlockActors) {
        auto const correctionState = state.correctionStates[projected.structureIndex];
        // 26.32: BlockActor::isWithinRenderDistance() was inlined out of the
        // SDK; it kept vanilla block actors within 64 blocks of the camera.
        auto const dx = static_cast<float>(projected.position.x) - camera.x;
        auto const dy = static_cast<float>(projected.position.y) - camera.y;
        auto const dz = static_cast<float>(projected.position.z) - camera.z;
        auto const* renderComponent = projected.actor->_getRenderComponent();
        if (correctionState == CorrectionState::Correct
            || correctionState == CorrectionState::WrongType
            || correctionState == CorrectionState::WrongState
            || !renderComponent
            || (dx * dx + dy * dy + dz * dz) > 64.0f * 64.0f) {
            continue;
        }
        dispatcher.render(
            renderContext,
            region,
            *const_cast<IVanillaRenderBlockActorComponent*>(renderComponent),
            *projected.block,
            renderAlphaLayer,
            noForcedMaterial,
            nullptr,
            -1,
            std::nullopt
        );
    }
}

void submitProjectionMeshPass(
    ProjectionState&        state,
    BaseActorRenderContext& renderContext,
    IClientInstance&        client,
    BlockPos const&         renderOrigin,
    Vec3 const&             camera,
    float                   structureOpacity,
    bool                    renderAlphaLayer,
    bool                    structureBoundsEnabled
) {
    auto& itemRenderer = renderContext.mItemInHandRenderer;
    auto const& blendMaterial = itemRenderer.mMatBlendBlock.get();

    struct VisibleMesh {
        std::size_t bucket;
        std::size_t section;
    };
    auto worldCenter = [&](std::size_t section) {
        return Vec3{
            static_cast<float>(renderOrigin.x) + state.sections[section].center.x,
            static_cast<float>(renderOrigin.y) + state.sections[section].center.y,
            static_cast<float>(renderOrigin.z) + state.sections[section].center.z
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
        if (!materialExists(material)) return;
        for (auto const& visible : meshes) {
            auto& mesh = *state.sections[visible.section].meshes[visible.bucket];
            mesh.renderMesh(
                renderContext.mScreenContext,
                material,
                *state.terrainTextureVariant,
                0,
                mesh.mVertexCount.get().value_or(0u),
                emptyOffscreenCaptureDescription(),
                nullptr
            );
        }
    };
    auto collectBucket = [&](std::size_t bucket) {
        std::vector<VisibleMesh> result;
        result.reserve(state.sections.size());
        for (std::size_t section = 0; section < state.sections.size(); ++section) {
            auto const& mesh = state.sections[section].meshes[bucket];
            if (mesh && mesh->isValid()) {
                result.push_back({bucket, section});
            }
        }
        return result;
    };

    auto const opaqueBucket = static_cast<std::size_t>(RenderBucket::Opaque);
    auto const alphaBucket = static_cast<std::size_t>(RenderBucket::Alpha);
    auto const alphaOneSidedBucket = static_cast<std::size_t>(RenderBucket::AlphaOneSided);
    auto const blendBucket = static_cast<std::size_t>(RenderBucket::Blend);
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
            renderMeshes(
                opaqueMeshes,
                materialExists(opaqueMaterial) ? opaqueMaterial : blendMaterial
            );
            renderMeshes(
                alphaMeshes,
                materialExists(alphaMaterial) ? alphaMaterial : blendMaterial
            );
            renderMeshes(
                alphaOneSidedMeshes,
                materialExists(alphaOneSidedMaterial) ? alphaOneSidedMaterial
                                                      : (materialExists(alphaMaterial) ? alphaMaterial
                                                                                       : blendMaterial)
            );
        } else {
            renderMeshes(transparentMeshes, blendMaterial);
        }
    } else if (renderAlphaLayer) {
        // True projection transparency needs a blending material even for
        // normally opaque/cutout blocks. Sort every bucket together.
        std::vector<VisibleMesh> transparentMeshes;
        for (std::size_t bucket = 0;
             bucket < static_cast<std::size_t>(RenderBucket::Count);
             ++bucket) {
            auto bucketMeshes = collectBucket(bucket);
            transparentMeshes.insert(
                transparentMeshes.end(), bucketMeshes.begin(), bucketMeshes.end()
            );
        }
        sortBackToFront(transparentMeshes);
        renderMeshes(transparentMeshes, blendMaterial);
    }

    // Textured liquid hulls travel the proven glass path: blend-block material
    // plus the terrain atlas, sorted back to front by section.
    if (renderAlphaLayer) {
        std::vector<std::size_t> liquidSections;
        for (std::size_t liquidSection = 0;
             liquidSection < state.liquidProxySectionMeshes.size();
             ++liquidSection) {
            auto const& mesh = state.liquidProxySectionMeshes[liquidSection];
            if (mesh && mesh->isValid()) liquidSections.push_back(liquidSection);
        }
        std::sort(
            liquidSections.begin(),
            liquidSections.end(),
            [&](std::size_t lhs, std::size_t rhs) {
                return distanceSquared(worldCenter(lhs)) > distanceSquared(worldCenter(rhs));
            }
        );
        for (auto const liquidSection : liquidSections) {
            auto& mesh = *state.liquidProxySectionMeshes[liquidSection];
            mesh.renderMesh(
                renderContext.mScreenContext,
                blendMaterial,
                *state.terrainTextureVariant,
                0,
                mesh.mVertexCount.get().value_or(0u),
                emptyOffscreenCaptureDescription(),
                nullptr
            );
        }

        // Textured placeholder hulls for block-entity blocks.
        for (auto const& placeholder : state.blockEntityPlaceholderSectionMeshes) {
            if (!placeholder || !placeholder->isValid()) continue;
            placeholder->renderMesh(
                renderContext.mScreenContext,
                blendMaterial,
                *state.terrainTextureVariant,
                0,
                placeholder->mVertexCount.get().value_or(0u),
                emptyOffscreenCaptureDescription(),
                nullptr
            );
        }
    }

    if (!renderAlphaLayer) return;

    auto* levelRenderer = client.getLevelRenderer();
    auto const& outlineMaterial = levelRenderer
        ? levelRenderer->mLevelRendererPlayer->mOutlineSelectionMaterial.get()
        : itemRenderer.mMatBlendBlock.get();
    if (materialExists(outlineMaterial) && structureBoundsEnabled
        && state.structureBoundsMesh && state.structureBoundsMesh->isValid()) {
        state.structureBoundsMesh->renderMesh(
            renderContext.mScreenContext,
            outlineMaterial,
            // No texture override: the old textureless overload passed monostate.
            std::variant<::std::monostate, ::mce::TexturePtr, ::mce::ClientTexture, ::mce::ServerTexture>{},
            0,
            state.structureBoundsMesh->mVertexCount.get().value_or(0u),
            emptyOffscreenCaptureDescription(),
            nullptr
        );
    }
    submitCorrectionOverlayPass(state, renderContext, client);
}

} // namespace lholo::projection::detail
