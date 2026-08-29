// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/mesh/ProjectionRenderer.h"

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/core/ProjectionState.h"
#include "projection/world/ProjectionVirtualWorld.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/blockactor/BlockActorRenderDispatcher.h"
#include "mc/client/renderer/game/ItemInHandRenderer.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/deps/minecraft_renderer/framebuilder/dragon/RenderMetadata.h"
#include "mc/deps/minecraft_renderer/renderer/RenderMaterial.h"
#include "mc/deps/renderer/hal/interface/DepthStencilStateDescription.h"
#include "mc/world/level/block/actor/BlockActor.h"

namespace lholo::projection::detail {

namespace {

// Temporarily turns off depth testing on a shared render material so LHolo's
// geometry draws through world blocks (X-ray), restoring it when the scope ends.
// Safe because projection rendering runs synchronously on the present thread and
// vanilla never draws between the set and the restore.
class ScopedNoDepthTest {
public:
    ScopedNoDepthTest(mce::MaterialPtr const& material, bool enable) {
        if (!enable || !material) return;
        mMaterial = const_cast<mce::RenderMaterial*>(material.operator->());
        if (!mMaterial) return;
        auto& description = mMaterial->depthStencilStateDescription.get();
        mSaved = description.depthTestEnabled;
        description.depthTestEnabled = false;
    }
    ~ScopedNoDepthTest() {
        if (!mMaterial) return;
        mMaterial->depthStencilStateDescription.get().depthTestEnabled = mSaved;
    }
    ScopedNoDepthTest(ScopedNoDepthTest const&) = delete;
    ScopedNoDepthTest& operator=(ScopedNoDepthTest const&) = delete;

private:
    mce::RenderMaterial* mMaterial{};
    bool                 mSaved{};
};

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
        if (correctionState == CorrectionState::Correct
            || correctionState == CorrectionState::WrongType
            || correctionState == CorrectionState::WrongState
            || !projected.actor->isWithinRenderDistance(camera)) {
            continue;
        }
        dispatcher.render(
            renderContext,
            region,
            *projected.actor,
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
    bool                    structureBoundsEnabled,
    bool                    correctionSeeThrough,
    bool                    missingSeeThrough,
    bool                    projectionSeeThrough
) {
    auto& itemRenderer = renderContext.getItemInHandRenderer();
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
        if (!material) return;
        ScopedNoDepthTest seeThrough(material, projectionSeeThrough);
        for (auto const& visible : meshes) {
            auto& mesh = *state.sections[visible.section].meshes[visible.bucket];
            mesh.renderMesh(
                renderContext.getScreenContext(),
                material,
                *state.terrainTextureVariant,
                0,
                static_cast<uint>(mesh.getMeshVertexCount()),
                renderContext.mOffscreenCaptureDescription.get(),
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
            renderMeshes(opaqueMeshes, opaqueMaterial ? opaqueMaterial : blendMaterial);
            renderMeshes(alphaMeshes, alphaMaterial ? alphaMaterial : blendMaterial);
            renderMeshes(
                alphaOneSidedMeshes,
                alphaOneSidedMaterial ? alphaOneSidedMaterial
                                      : (alphaMaterial ? alphaMaterial : blendMaterial)
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
        ScopedNoDepthTest seeThrough(blendMaterial, projectionSeeThrough);
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
                renderContext.getScreenContext(),
                blendMaterial,
                *state.terrainTextureVariant,
                0,
                static_cast<uint>(mesh.getMeshVertexCount()),
                renderContext.mOffscreenCaptureDescription.get(),
                nullptr
            );
        }

        // Textured placeholder hulls for block-entity blocks.
        for (auto const& placeholder : state.blockEntityPlaceholderSectionMeshes) {
            if (!placeholder || !placeholder->isValid()) continue;
            placeholder->renderMesh(
                renderContext.getScreenContext(),
                blendMaterial,
                *state.terrainTextureVariant,
                0,
                static_cast<uint>(placeholder->getMeshVertexCount()),
                renderContext.mOffscreenCaptureDescription.get(),
                nullptr
            );
        }
    }

    if (!renderAlphaLayer) return;

    auto* levelRenderer = client.getLevelRenderer();
    auto const& outlineMaterial = levelRenderer
        ? levelRenderer->getLevelRendererPlayer().mOutlineSelectionMaterial.get()
        : itemRenderer.mMatBlendBlock.get();
    if (outlineMaterial && structureBoundsEnabled
        && state.structureBoundsMesh && state.structureBoundsMesh->isValid()) {
        state.structureBoundsMesh->renderMesh(
            renderContext.getScreenContext(),
            outlineMaterial,
            0,
            static_cast<uint>(state.structureBoundsMesh->getMeshVertexCount()),
            renderContext.mOffscreenCaptureDescription.get(),
            nullptr
        );
    }
    auto const& warningMaterial = levelRenderer
        ? levelRenderer->getLevelRendererPlayer().selectionBlockEntityOverlayColorMaterial.get()
        : itemRenderer.mMatBlendBlockNoColor.get();
    // seeThroughMeshes is passed per call so the "missing" correction meshes stay
    // depth-tested while only the "wrong" ones honor the X-ray toggle.
    auto renderOverlayMeshes = [&] (
        std::vector<std::unique_ptr<mce::Mesh>> const& meshes,
        mce::MaterialPtr const& material,
        bool seeThroughMeshes
    ) {
        if (!material) return;
        ScopedNoDepthTest seeThrough(material, seeThroughMeshes);
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
        // Vibrant Visuals: reuse the colored outline shader for the hull and
        // restore every temporary material field immediately after submission.
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
            renderOverlayMeshes(state.warningFillSectionMeshes, outlineMaterial, missingSeeThrough);
            renderOverlayMeshes(state.wrongFillSectionMeshes, outlineMaterial, correctionSeeThrough);
            renderMaterial->mPrimitiveMode = savedPrimitive;
            renderMaterial->blendStateDescription.get() = savedBlend;
            renderMaterial->mDepthBias = savedDepthBias;
            renderMaterial->mSlopeScaledDepthBias = savedSlopeBias;
        }
    } else if (warningMaterial) {
        // Vanilla selection overlay has the required depth bias. Temporarily
        // borrow SourceAlpha/OneMinusSourceAlpha from blend-block material.
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
        renderOverlayMeshes(state.warningFillSectionMeshes, warningMaterial, missingSeeThrough);
        renderOverlayMeshes(state.wrongFillSectionMeshes, warningMaterial, correctionSeeThrough);
    }
    if (outlineMaterial) {
        renderOverlayMeshes(state.correctionOutlineSectionMeshes, outlineMaterial, missingSeeThrough);
        renderOverlayMeshes(state.wrongOutlineSectionMeshes, outlineMaterial, correctionSeeThrough);
    }
}

} // namespace lholo::projection::detail
