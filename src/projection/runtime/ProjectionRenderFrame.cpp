// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Projection frame orchestration: structure activation, world-context check,
// opaque/transparent mesh submission, hit-select suppression and the
// post-block-entity frame entry. Session state is accessed through the
// ProjectionSession contract.

#include "projection/runtime/ProjectionRenderFrame.h"
#include "projection/runtime/ProjectionSession.h"

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/core/ProjectionRules.h"
#include "projection/core/ProjectionState.h"
#include "projection/mesh/ProjectionMeshWorker.h"
#include "projection/mesh/ProjectionMeshUpload.h"
#include "projection/mesh/ProjectionRenderer.h"
#include "projection/mesh/ProjectionSectionBuilder.h"
#include "projection/runtime/ProjectionFramePipeline.h"
#include "projection/runtime/ProjectionInvalidation.h"
#include "projection/runtime/ProjectionLifecycle.h"
#include "projection/runtime/ProjectionProgress.h"
#include "projection/runtime/ProjectionWorldEvents.h"
#include "projection/runtime/IndependentFileProjection.h"
#include "projection/runtime/IndependentFileFrame.h"
#include "structure/IndependentTaskFiles.h"
#include "projection/world/ProjectionPlacement.h"

#include "overlay/BoundsWireframe.h"
#include "place/PlaceHelper.h"
#include "plugin/LHolo.h"
#include "structure/capture/StructureCapture.h"
#include "structure/StructureLoader.h"
#include "structure/StructureSession.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <memory>
#include <limits>
#include <tuple>
#include <utility>

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/game/ItemInHandRenderer.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/levelgen/structure/LegacyStructureSettings.h"

#include "ll/api/mod/NativeMod.h"

namespace lholo::projection::detail {
namespace {

Vec3 renderCameraPosition(BaseActorRenderContext const& renderContext) {
    // 1.26.40 no longer exports BaseActorRenderContext::getCameraPosition(), and
    // its backing member mCameraPosition moved into the opaque Impl together
    // with mCameraTargetPosition/mWorldClipRegion. The generated header exposes
    // no accessor, so the position is read back from Impl.
    //
    // Verified in game: float slots 10..12 mirror the eye position every frame
    // (they tracked the player exactly), while mce::Camera::mPosition reads 0
    // for both the ScreenContext camera and IClientInstance::getCamera().
    auto const* impl = reinterpret_cast<float const*>(renderContext.mImpl.get());
    if (!impl) return {};
    return {impl[10], impl[11], impl[12]};
}

void resetWorldAfterExit() {
    place::resetWorldSession();
    structure::capture::clear();
    structure::resetWorldSession();
    structure::showActionHint(
        i18n::Message{i18n::TextKey::ActionHintWorldExited},
        structure::kProjectionLifecycleHintDurationMs
    );
}

auto& logger() {
    return LHolo::getInstance().getSelf().getLogger();
}

bool enableStructureProjection(
    ProjectionState& state,
    BaseActorRenderContext& renderContext,
    std::shared_ptr<structure::LoadedStructure const> loaded,
    structure::detail::StructureLoadOrigin const& loadOrigin,
    bool resumingDimension
) {
    ProjectionState next;
    if (!prepareProjectionState(next, renderContext, std::move(loaded))) return false;
    auto& client = renderContext.mClientInstance;
    auto* player = client.getLocalPlayer();
    auto const anchor = ProjectionSession::getInstance().consumeAnchor();
    if (resumingDimension && anchor) {
        next.anchor = BlockPos{anchor->x, anchor->y, anchor->z};
    } else {
        next.anchor = BlockPos{loadOrigin.x, loadOrigin.y, loadOrigin.z};
    }
    state = std::move(next);
    state.enabled = true;
    try {
        if (meshWorkerIsDisabledForSession()) {
            state.asyncMeshBuildingEnabled = false;
        } else {
            state.meshWorkerGeneration = startMeshWorker();
        }
    } catch (std::exception const& exception) {
        state.asyncMeshBuildingEnabled = false;
        disableMeshWorkerForSession();
        logger().warn("Projection mesh worker initialization failed; using synchronous fallback: {}", exception.what());
    } catch (...) {
        state.asyncMeshBuildingEnabled = false;
        disableMeshWorkerForSession();
        logger().warn("Projection mesh worker initialization failed; using synchronous fallback");
    }
    attachProjectionWorldEvents(player->getLevel(), player->getDimensionBlockSource());
    initializePublishedBuildProgress(state.structure->renderBlocks.size());
    structure::recordProjectionAnchor(state.anchor.x, state.anchor.y, state.anchor.z);
    logger().info(
        "Structure projection enabled: {} renderable blocks at ({}, {}, {})",
        state.structure->renderBlocks.size(),
        state.anchor.x,
        state.anchor.y,
        state.anchor.z
    );
    return true;
}

void suspendProjectionDimension(ProjectionState& state) {
    auto const anchor = state.anchor;
    bool const hadVisibleFiles = !state.structure->composed || structure::hasVisibleTaskFiles(*state.structure);
    place::resetDimensionSession();
    structure::resetDimensionSession();
    ProjectionSession::getInstance().suspendForDimension(
        state.structureGeneration,
        state.dimensionId,
        ProjectionAnchor{anchor.x, anchor.y, anchor.z}
    );
    suspendProjectionState(state);
    if (hadVisibleFiles) {
        structure::showActionHint(
            i18n::Message{i18n::TextKey::ActionHintProjectionSuspended},
            structure::kProjectionLifecycleHintDurationMs
        );
    }
}

void renderProjection(
    ProjectionState&          state,
    BaseActorRenderContext&   renderContext,
    bool                      renderAlphaLayer
) {
    auto& client = renderContext.mClientInstance;
    auto* player  = client.getLocalPlayer();

    auto& tessellator = renderContext.mScreenContext.tessellator;
    Vec3 const camera = renderCameraPosition(renderContext);

    if (!state.blockTessellator) return;
    if (!renderAlphaLayer) {
        auto const transform = state.fileTransform ? *state.fileTransform
            : state.structure->composed ? structure::detail::StructureTransformSnapshot{}
            : structure::detail::StructureSession::getInstance().transform();
        auto const mirrorMode = transform.mirror;
        auto const rotationTurns = transform.rotation;
        auto const mirror = getProjectionMirror(mirrorMode);
        auto const rotation = getProjectionRotation(rotationTurns);
        LegacyStructureSettings transformSettings{
            mirror,
            rotation,
            nullptr,
            BoundingBox{}
        };
        bool const identityTransform = mirrorMode == 0 && rotationTurns == 0;
        auto const offsetX = transform.offsetX;
        auto const offsetY = transform.offsetY;
        auto const offsetZ = transform.offsetZ;
        auto const layerDisplayMode = transform.layerDisplayMode;
        auto const layerAxis = transform.layerAxis;
        auto const maxLayer = layerAxis == structure::LayerAxis::Material
            ? std::max(0, static_cast<int>(state.structure->materialCount) - 1)
            : std::max(
                0,
                (layerAxis == structure::LayerAxis::X
                    ? state.structure->sizeX : state.structure->sizeY) - 1
            );
        auto const displayLayer = std::clamp(
            transform.displayLayer, 0, maxLayer
        );
        auto& session = ProjectionSession::getInstance();
        auto const correctionFillOpacity = session.correctionFillOpacity();
        auto const correctionOutlineOpacity = session.correctionOutlineOpacity();
        auto const invalidation = reconcileProjectionInvalidation(
            state,
            ProjectionInvalidationSettings{
                .mirrorMode               = mirrorMode,
                .rotationTurns            = rotationTurns,
                .offsetX                  = offsetX,
                .offsetY                  = offsetY,
                .offsetZ                  = offsetZ,
                .layerDisplayMode         = layerDisplayMode,
                .displayLayer             = displayLayer,
                .layerAxis                = layerAxis,
                .correctionFillOpacity    = correctionFillOpacity,
                .correctionOutlineOpacity = correctionOutlineOpacity
            }
        );
        if (invalidation.placementViewChanged()) {
            rebuildProjectionPlacement(
                state,
                player->getDimensionBlockSource(),
                renderContext.mBlockEntityRenderDispatcher,
                transformSettings,
                ProjectionPlacementSettings{
                    .mirrorMode        = mirrorMode,
                    .rotationTurns     = rotationTurns,
                    .offsetX           = offsetX,
                    .offsetY           = offsetY,
                    .offsetZ           = offsetZ,
                    .layerDisplayMode  = layerDisplayMode,
                    .displayLayer      = displayLayer,
                    .layerAxis         = layerAxis,
                    .identityTransform = identityTransform
                }
            );
        }
        ProjectionSectionBuildSettings const sectionBuildSettings{
            .mirror                   = mirror,
            .rotation                 = rotation,
            .mirrorMode               = mirrorMode,
            .rotationTurns            = rotationTurns,
            .offsetX                  = offsetX,
            .offsetY                  = offsetY,
            .offsetZ                  = offsetZ,
            .correctionFillOpacity    = correctionFillOpacity,
            .correctionOutlineOpacity = correctionOutlineOpacity,
            .identityTransform        = identityTransform
        };
        processProjectionOpaqueFrame(
            state,
            renderContext,
            tessellator,
            player->getDimensionBlockSource(),
            camera,
            transformSettings,
            sectionBuildSettings,
            layerDisplayMode,
            displayLayer,
            layerAxis
        );
    }

    // Keep vanilla world queries at their real BlockPos, but do not upload large
    // absolute coordinates to the GPU. Render vertices relative to the projection
    // origin, matching the strategy used by chunk meshes.
    BlockPos const renderOrigin{
        state.anchor.x + (state.fileTransform ? state.fileTransform->offsetX : state.structure->composed ? 0 : structure::getOffsetX()),
        state.anchor.y + (state.fileTransform ? state.fileTransform->offsetY : state.structure->composed ? 0 : structure::getOffsetY()),
        state.anchor.z + (state.fileTransform ? state.fileTransform->offsetZ : state.structure->composed ? 0 : structure::getOffsetZ())
    };

    submitProjectedBlockActorPass(
        state,
        renderContext,
        player->getDimensionBlockSource(),
        camera,
        renderAlphaLayer
    );

    auto matrix = renderContext.mScreenContext.camera.worldMatrixStack.get().push(false);
    matrix.mat->translate(
        static_cast<float>(renderOrigin.x) - camera.x,
        static_cast<float>(renderOrigin.y) - camera.y,
        static_cast<float>(renderOrigin.z) - camera.z
    );

    auto& itemRenderer = renderContext.mItemInHandRenderer;
    auto const& blendMaterial = itemRenderer.mMatBlendBlock.get();
    if (blendMaterial.mRenderMaterialInfoPtr.get() == nullptr) return;

    if (!state.terrainTextureVariant) {
        logger().error("Projection terrain texture is not available");
        return;
    }

    try {
        submitProjectionMeshPass(
            state,
            renderContext,
            client,
            renderOrigin,
            camera,
            renderAlphaLayer,
            ProjectionSession::getInstance().structureBoundsEnabled(),
            ProjectionSession::getInstance().correctionSeeThrough(),
            ProjectionSession::getInstance().missingSeeThrough()
        );
    } catch (std::exception const& exception) {
        logger().error("Projection immediate mesh submission failed: {}", exception.what());
        if (state.fileTransform) throw;
        resetProjectionState(state);
        return;
    } catch (...) {
        logger().error("Projection immediate mesh submission failed with an unknown exception");
        if (state.fileTransform) throw;
        resetProjectionState(state);
        return;
    }
}

} // namespace

bool shouldSuppressProjectionHitSelect(BlockPos const& pos) {
    return ProjectionSession::getInstance().withLockedState(
        [&](ProjectionState& root, overlay::BoundsWireframe&) {
            if (!root.enabled) return false;
            auto suppresses=[&](ProjectionState const& state) {
            if (state.enabled && state.structure) {
                auto const found = state.expectedWorldBlockIndices->find(
                    std::tuple{pos.x, pos.y, pos.z}
                );
                if (found != state.expectedWorldBlockIndices->end()) {
                    auto const correctionState = (*state.correctionStates)[found->second];
                    if (correctionState == CorrectionState::WrongType
                        || correctionState == CorrectionState::WrongState) {
                        // LHolo already renders a complete red/yellow hull and
                        // outline for this cell. Vanilla's coincident hit-select
                        // overlay adds a second surface only while the crosshair
                        // targets it, producing the observed flicker.
                        return true;
                    }
                }
                BlockPos const transformed{
                    pos.x - state.anchor.x - state.cachedOffsetX,
                    pos.y - state.anchor.y - state.cachedOffsetY,
                    pos.z - state.anchor.z - state.cachedOffsetZ,
                };
                auto const local = inverseTransformStructurePosition(
                    transformed,
                    *state.structure,
                    state.cachedMirror,
                    state.cachedRotation
                );
                if (state.extraBlockPositions.contains(
                        std::tuple{local.x, local.y, local.z}
                    )) {
                    // Extra cells use the same correction material as the
                    // red/yellow markers, so the vanilla coincident selection
                    // overlay must be suppressed for the same reason.
                    return true;
                }
            }
            return false;
            };
            for (auto const& file:root.files) if (suppresses(*file)) return true;
            return suppresses(root);
        }
    );
}

void renderProjectionFrame(BaseActorRenderContext& renderContext, bool renderAlphaLayer) {
    bool clearStructure = false;
    bool worldExitPending = consumeWorldExitRequest();
    if (worldExitPending) {
        resetWorldAfterExit();
        return;
    }
    ProjectionSession::getInstance().withLockedState(
        [&](ProjectionState& state, overlay::BoundsWireframe& captureBounds) {
            if (auto const bounds = structure::capture::getBounds()) {
                captureBounds.setBounds(
                    BlockPos{bounds->min.x, bounds->min.y, bounds->min.z},
                    BlockPos{bounds->max.x, bounds->max.y, bounds->max.z},
                    0xFF0000FF
                );
            } else {
                captureBounds.clear();
            }
            captureBounds.render(renderContext, renderAlphaLayer);

            if (consumeWorldExitRequest()) {
                worldExitPending = true;
                return;
            }

            auto const activation = structure::detail::StructureSession::getInstance().activation();
            auto* activationPlayer = renderContext.mClientInstance.getLocalPlayer();
            bool const originDimensionReady = activation.origin && (!activation.origin->dimensionId
                || (activationPlayer
                    && *activation.origin->dimensionId == static_cast<int>(activationPlayer->getDimensionId())));
            bool const reuseFiles=activation.loaded && activation.loaded->independentFiles
                && state.enabled && state.structure && state.structure->independentFiles
                && originDimensionReady && state.anchor.x==activation.origin->x
                && state.anchor.y==activation.origin->y && state.anchor.z==activation.origin->z
                && classifyProjectionContext(state,renderContext.mClientInstance,activationPlayer)==ProjectionContextStatus::Current;
            if (auto loaded = activation.loaded; loaded && loaded->generation != state.structureGeneration
                && originDimensionReady && !reuseFiles) {
                auto* player = activationPlayer;
                if (!player) return;
                auto& session = ProjectionSession::getInstance();
                auto const activationStatus = session.prepareDimensionActivation(
                    loaded->generation,
                    static_cast<int>(player->getDimensionId())
                );
                if (activationStatus == DimensionActivationStatus::Deferred) {
                    return;
                }
                resetProjectionState(state);
                bool activated = false;
                {
                    // Level destruction and structure activation must not pass
                    // each other between the exit check and the first use of
                    // the new state's Level/Dimension pointers.
                    std::lock_guard lifecycleLock(projectionWorldLifecycleMutex());
                    if (consumeWorldExitRequest()) {
                        worldExitPending = true;
                    } else {
                        activated = enableStructureProjection(
                            state,
                            renderContext,
                            std::move(loaded),
                            *activation.origin,
                            activationStatus == DimensionActivationStatus::Resuming
                        );
                    }
                }
                if (worldExitPending) return;
                if (!activated) {
                    resetProjectionState(state);
                    logger().error("Could not enable loaded structure projection");
                } else {
                    session.cancelDimensionSuspension();
                    bool const hasVisibleFiles = !state.structure->composed || structure::hasVisibleTaskFiles(*state.structure);
                    if ((activationStatus == DimensionActivationStatus::Resuming
                            || activationStatus == DimensionActivationStatus::Rebuilding) && hasVisibleFiles) {
                        structure::showActionHint(
                            i18n::Message{i18n::TextKey::ActionHintProjectionRestored},
                            structure::kProjectionLifecycleHintDurationMs
                        );
                    }
                }
            }

            if (!state.enabled) return;
            auto& client = renderContext.mClientInstance;
            auto const contextStatus = classifyProjectionContext(
                state,
                client,
                client.getLocalPlayer()
            );
            if (contextStatus == ProjectionContextStatus::Unavailable) return;
            if (contextStatus == ProjectionContextStatus::WorldChanged) {
                resetProjectionState(state);
                clearStructure = true;
                return;
            }
            if (contextStatus == ProjectionContextStatus::DimensionChanged) {
                suspendProjectionDimension(state);
                return;
            }
            if (state.structure->independentFiles) {
                try {
                    if (!renderAlphaLayer) {
                        {
                            std::lock_guard lifecycleLock(projectionWorldLifecycleMutex());
                            if (consumeWorldExitRequest()) { worldExitPending=true; return; }
                            if (activation.loaded && activation.loaded->independentFiles
                                && !updateIndependentFiles(state,renderContext,activation.loaded)) return;
                        }
                        // Apply the selected file's live tuple on this frame,
                        // without waiting for the asynchronous task publication.
                        if (auto* selected=selectedFileProjection(state)) {
                            auto source=structure::detail::StructureSession::getInstance().selectedSourceFor(*state.structure);
                            auto const* definition=structure::independentTaskFile(*state.structure,source);
                            if (definition) {
                                auto live=structure::detail::StructureSession::getInstance().transform();
                                auto candidate=*definition;
                                candidate.x=live.offsetX; candidate.y=live.offsetY; candidate.z=live.offsetZ;
                                candidate.rotation=live.rotation; candidate.mirror=live.mirror;
                                std::array<long long,3> origin{static_cast<long long>(state.anchor.x)+candidate.x,
                                    static_cast<long long>(state.anchor.y)+candidate.y,
                                    static_cast<long long>(state.anchor.z)+candidate.z};
                                bool rotated=(candidate.rotation&1)!=0;
                                std::array<int,3> extent{rotated?candidate.loaded->sizeZ:candidate.loaded->sizeX,
                                    candidate.loaded->sizeY,rotated?candidate.loaded->sizeX:candidate.loaded->sizeZ};
                                bool fits=true;
                                for (int axis=0;axis<3;++axis)
                                    if (origin[axis]<std::numeric_limits<int>::min()
                                        || origin[axis]+extent[axis]-1>std::numeric_limits<int>::max()) fits=false;
                                if (fits && structure::independentTaskPlacement(candidate,state.structure->independentFiles->files))
                                    selected->fileTransform=live;
                            }
                        }
                        distributeIndependentFileEvents(state);
                        uploadCompletedProjectionMeshes(state, renderContext.mScreenContext.tessellator);
                    }
                    if (!renderAlphaLayer) {
                        forEachIndependentProjectionFile(state.files.size(), state.fileFrameCursor,
                            [&](std::size_t index) { renderProjection(*state.files[index], renderContext, false); });
                    } else {
                        for (auto& file : state.files) renderProjection(*file, renderContext, true);
                    }
                } catch (std::exception const& error) {
                    logger().error("Independent projection failed: {}",error.what());
                    resetProjectionState(state);
                } catch (...) {
                    logger().error("Independent projection failed");
                    resetProjectionState(state);
                }
            } else renderProjection(state, renderContext, renderAlphaLayer);
        }
    );
    if (worldExitPending) {
        resetWorldAfterExit();
        return;
    }
    if (clearStructure) structure::clear();
}

} // namespace lholo::projection::detail
