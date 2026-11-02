// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/mesh/ProjectionRenderer.h"
#include "projection/mesh/ProjectionDrawLists.h"

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/core/ProjectionState.h"
#include "projection/world/ProjectionVirtualWorld.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/ActorShaderManager.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/blockactor/BlockActorRenderDispatcher.h"
#include "mc/client/renderer/game/ItemInHandRenderer.h"
#include "mc/common/Brightness.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/deps/core/renderer/RenderMaterialInfo.h"
#include "mc/deps/minecraft_renderer/framebuilder/dragon/RenderMetadata.h"
#include "mc/deps/minecraft_renderer/renderer/RenderMaterial.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"
#include "mc/deps/renderer/hal/interface/DepthStencilStateDescription.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/component/IVanillaRenderBlockActorComponent.h"

#include "render/OverlayMaterials.h"
#include "render/ScopedShaderColor.h"

namespace lholo::projection::detail {

namespace {

struct ProjectionDrawPassTimer {
    ProjectionState& state;
    bool alpha;
    std::uint64_t prepareMicros{};
    std::size_t draws{};
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    ~ProjectionDrawPassTimer() {
        auto const elapsed = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count());
        // Diagnostic logging must not throw while a render scope unwinds.
        try { recordProjectionDrawPass(state, alpha, elapsed, prepareMicros, draws); } catch (...) {}
    }
};

using render::materialExists;

mce::RenderMaterial* tryRenderMaterial(mce::MaterialPtr const& material) {
    auto* info = material.mRenderMaterialInfoPtr.get();
    return info ? info->mPtr.get() : nullptr;
}

OffscreenCaptureDescription const& emptyOffscreenCaptureDescription() {
    using Storage = decltype(dragon::RenderMetadata::mOffscreenCaptureDescription);
    static Storage empty{};
    return empty.get();
}

// The correction shells carry no real UVs, but their overlay materials still
// sample texture slot 0 for the alpha test: an empty variant leaves the
// missing-texture checkerboard bound there, so lookups depended on stale slot
// state. The white texture comes from render/OverlayMaterials.h; the section
// builder aims every overlay vertex at its center so the sampler reads exactly
// (1,1,1,1) and never discards.

// Temporarily turns off depth testing on a shared render material so correction
// overlay geometry draws through world blocks (X-ray), restoring it when the scope ends.
// Safe because projection rendering runs synchronously on the present thread and
// vanilla never draws between the set and the restore.
class ScopedNoDepthTest {
public:
    ScopedNoDepthTest(mce::MaterialPtr const& material, bool enable) {
        if (!enable || !materialExists(material)) return;
        mMaterial = tryRenderMaterial(material);
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
        auto const correctionState = state.renderCorrectionState(projected.structureIndex);
        auto* renderComponent = projected.actor->_getRenderComponent();
        if (correctionState == CorrectionState::Correct
            || correctionState == CorrectionState::WrongType
            || correctionState == CorrectionState::WrongState
            || !renderComponent
            || !renderComponent->isWithinRenderDistance(camera)) {
            continue;
        }
        dispatcher.render(
            renderContext,
            region,
            *renderComponent,
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
    bool                    renderAlphaLayer,
    bool                    structureBoundsEnabled,
    bool                    correctionSeeThrough,
    bool                    missingSeeThrough
) {
    ProjectionDrawPassTimer drawTimer{state, renderAlphaLayer};
    auto& itemRenderer = renderContext.mItemInHandRenderer;
    auto const& blendMaterial = itemRenderer.mMatBlendBlock.get();

    // The ItemInHand/Entity materials used for ghost blocks drive their
    // diffuse lighting from TileLightColor in constant buffer CB0. When
    // looking down at the ground with no block entities in the view frustum
    // (especially in single-layer mode), CB0 retains stale or zeroed lighting
    // from selection outlines, turning the projection black. Explicitly
    // prime actor constants with full brightness (Brightness::MAX()) so
    // projection meshes stay consistently illuminated in all view angles.
    if (auto* player = client.getLocalPlayer()) {
        ActorShaderManager::setupShaderParameters(
            renderContext.mScreenContext,
            renderContext,
            *player,
            mce::Color{1.0f, 1.0f, 1.0f, 0.0f},
            1.0f,
            Brightness::MAX(),
            std::nullopt
        );
    }

    drawTimer.prepareMicros = prepareProjectionDrawLists(state, camera, renderOrigin);
    auto const& cache = state.drawCache;
    auto list = [&](ProjectionDrawSlot slot) -> std::vector<std::size_t> const& {
        return cache.lists[static_cast<std::size_t>(slot)];
    };
    auto renderTerrainMesh = [&](std::size_t section, std::size_t bucket, mce::MaterialPtr const& material) {
        if (section >= state.sections.size() || !materialExists(material)) return;
        auto const& mesh = state.sections[section].meshes[bucket];
        if (!mesh || !mesh->isValid()) return;
        ++drawTimer.draws;
        mesh->renderMesh(renderContext.mScreenContext, material, *state.terrainTextureVariant,
            0, mesh->mVertexCount.get().value_or(0u), emptyOffscreenCaptureDescription(), nullptr);
    };
    auto renderSolidBucket = [&](ProjectionDrawSlot slot, mce::MaterialPtr const& material) {
        for (auto const section : list(slot)) renderTerrainMesh(section, static_cast<std::size_t>(slot), material);
    };

    if (!renderAlphaLayer) {
        // Colored materials preserve biome tint. Only resolve and submit the
        // three solid/cutout buckets in their actual render pass.
        auto const& opaqueMaterial = materialExists(itemRenderer.mMatOpaqueBlockColor.get())
            ? itemRenderer.mMatOpaqueBlockColor.get() : itemRenderer.mMatOpaqueBlock.get();
        auto const& alphaMaterial = materialExists(itemRenderer.mMatAlphaColoredBlock.get())
            ? itemRenderer.mMatAlphaColoredBlock.get() : itemRenderer.mMatAlphaBlock.get();
        auto const& oneSidedMaterial = materialExists(itemRenderer.mMatAlphaOneSidedColoredBlock.get())
            ? itemRenderer.mMatAlphaOneSidedColoredBlock.get() : itemRenderer.mMatAlphaOneSidedBlock.get();
        renderSolidBucket(ProjectionDrawSlot::Opaque,
            materialExists(opaqueMaterial) ? opaqueMaterial : blendMaterial);
        renderSolidBucket(ProjectionDrawSlot::Alpha,
            materialExists(alphaMaterial) ? alphaMaterial : blendMaterial);
        renderSolidBucket(ProjectionDrawSlot::AlphaOneSided, materialExists(oneSidedMaterial)
            ? oneSidedMaterial : materialExists(alphaMaterial) ? alphaMaterial : blendMaterial);
    } else {
        // Only the vanilla blend bucket uses the cached back-to-front section order.
        for (auto const& draw : cache.transparentTerrain) renderTerrainMesh(draw.section, draw.bucket, blendMaterial);
    }

    // Textured liquid hulls travel the proven glass path: blend-block material
    // plus the terrain atlas, sorted back to front by section.
    if (renderAlphaLayer) {
        for (auto const liquidSection : list(ProjectionDrawSlot::Liquid)) {
            if (liquidSection >= state.liquidProxySectionMeshes.size()) continue;
            auto const& candidate = state.liquidProxySectionMeshes[liquidSection];
            if (!candidate || !candidate->isValid()) continue;
            auto& mesh = *state.liquidProxySectionMeshes[liquidSection];
            ++drawTimer.draws;
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
        for (auto const section : list(ProjectionDrawSlot::Placeholder)) {
            if (section >= state.blockEntityPlaceholderSectionMeshes.size()) continue;
            auto const& placeholder = state.blockEntityPlaceholderSectionMeshes[section];
            if (!placeholder || !placeholder->isValid()) continue;
            ++drawTimer.draws;
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
    if (!levelRenderer) return;
    auto const& outlineMaterial = render::selectionOutlineMaterial(*levelRenderer);
    auto const overlayTexture = render::resolveWhiteTextureVariant(*levelRenderer);
    if (materialExists(outlineMaterial) && structureBoundsEnabled
        && state.structureBoundsMesh && state.structureBoundsMesh->isValid()) {
        render::ScopedShaderColor boundsColor{renderContext.mScreenContext.currentShaderColor, 0xFFFFD633U};
        ++drawTimer.draws;
        state.structureBoundsMesh->renderMesh(
            renderContext.mScreenContext,
            outlineMaterial,
            overlayTexture,
            0,
            state.structureBoundsMesh->mVertexCount.get().value_or(0u),
            emptyOffscreenCaptureDescription(),
            nullptr
        );
    }
    // The draw cache already excludes invisible/absent correction meshes.
    // Bounds above render independently; an empty correction pass needs no
    // shader-color or material-state changes.
    if (list(ProjectionDrawSlot::MissingFill).empty() && list(ProjectionDrawSlot::WrongFill).empty()
        && list(ProjectionDrawSlot::MissingOutline).empty() && list(ProjectionDrawSlot::WrongOutline).empty()) return;

    auto const& warningMaterial = levelRenderer->mLevelRendererPlayer->selectionBlockEntityOverlayColorMaterial.get();

    // Isolate correction rendering from the caller's tint, preserving the
    // existing vertex colors, opacity, and material blending.
    render::ScopedShaderColor correctionColor{renderContext.mScreenContext.currentShaderColor};

    // seeThroughMeshes is passed per call so the "missing" correction meshes stay
    // depth-tested while only the "wrong" ones honor the X-ray toggle.
    auto renderOverlayMeshes = [&] (
        std::vector<std::unique_ptr<mce::Mesh>> const& meshes,
        ProjectionDrawSlot slot,
        mce::MaterialPtr const& material,
        bool seeThroughMeshes
    ) {
        if (!materialExists(material)) return;
        ScopedNoDepthTest seeThrough(material, seeThroughMeshes);
        for (auto const section : list(slot)) {
            if (section >= meshes.size()) continue;
            auto const& overlay = meshes[section];
            if (!overlay || !overlay->isValid()) continue;
            ++drawTimer.draws;
            overlay->renderMesh(
                renderContext.mScreenContext,
                material,
                overlayTexture,
                0,
                overlay->mVertexCount.get().value_or(0u),
                emptyOffscreenCaptureDescription(),
                nullptr
            );
        }
    };

    struct MaterialStateRestore {
        mce::RenderMaterial* material{};
        std::optional<mce::BlendStateDescription> blend;
        mce::PrimitiveMode primitive{};
        float depthBias{};
        float slopeBias{};
        bool restorePrimitive{};
        ~MaterialStateRestore() {
            if (!material || !blend) return;
            material->blendStateDescription.get() = *blend;
            material->mDepthBias = depthBias;
            material->mSlopeScaledDepthBias = slopeBias;
            if (restorePrimitive) material->mPrimitiveMode = primitive;
        }
    };

    if (static_cast<bool>(itemRenderer.mIsDeferredEnabled)) {
        // Vibrant Visuals: reuse the colored outline shader for the hull and
        // restore every temporary material field immediately after submission.
        auto* renderMaterial = tryRenderMaterial(outlineMaterial);
        MaterialStateRestore restore;
        auto* blendRenderMaterial = tryRenderMaterial(blendMaterial);
        if (renderMaterial && blendRenderMaterial) {
            restore.material = renderMaterial;
            restore.blend = renderMaterial->blendStateDescription.get();
            restore.primitive = renderMaterial->mPrimitiveMode;
            restore.depthBias = renderMaterial->mDepthBias;
            restore.slopeBias = renderMaterial->mSlopeScaledDepthBias;
            restore.restorePrimitive = true;
            renderMaterial->mPrimitiveMode = mce::PrimitiveMode::QuadList;
            renderMaterial->blendStateDescription.get()
                = blendRenderMaterial->blendStateDescription.get();
            renderMaterial->mDepthBias = 100.0f;
            renderMaterial->mSlopeScaledDepthBias = 15.0f;
            renderOverlayMeshes(state.warningFillSectionMeshes, ProjectionDrawSlot::MissingFill, outlineMaterial, missingSeeThrough);
            renderOverlayMeshes(state.wrongFillSectionMeshes, ProjectionDrawSlot::WrongFill, outlineMaterial, correctionSeeThrough);
        }
    } else if (materialExists(warningMaterial)) {
        // Vanilla selection overlay has the required depth bias. Temporarily
        // borrow SourceAlpha/OneMinusSourceAlpha from blend-block material.
        auto* renderMaterial = tryRenderMaterial(warningMaterial);
        MaterialStateRestore restore;
        auto* blendRenderMaterial = tryRenderMaterial(blendMaterial);
        if (renderMaterial && blendRenderMaterial) {
            restore.material = renderMaterial;
            restore.blend = renderMaterial->blendStateDescription.get();
            restore.depthBias = renderMaterial->mDepthBias;
            restore.slopeBias = renderMaterial->mSlopeScaledDepthBias;
            renderMaterial->blendStateDescription.get()
                = blendRenderMaterial->blendStateDescription.get();
            renderMaterial->mDepthBias = 100.0f;
            renderMaterial->mSlopeScaledDepthBias = 15.0f;
        }
        renderOverlayMeshes(state.warningFillSectionMeshes, ProjectionDrawSlot::MissingFill, warningMaterial, missingSeeThrough);
        renderOverlayMeshes(state.wrongFillSectionMeshes, ProjectionDrawSlot::WrongFill, warningMaterial, correctionSeeThrough);
    }
    if (materialExists(outlineMaterial)) {
        renderOverlayMeshes(state.correctionOutlineSectionMeshes, ProjectionDrawSlot::MissingOutline, outlineMaterial, missingSeeThrough);
        renderOverlayMeshes(state.wrongOutlineSectionMeshes, ProjectionDrawSlot::WrongOutline, outlineMaterial, correctionSeeThrough);
    }
}

} // namespace lholo::projection::detail
