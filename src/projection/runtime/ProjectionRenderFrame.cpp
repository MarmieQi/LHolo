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
#include "projection/mesh/ProjectionRenderer.h"
#include "projection/mesh/ProjectionSectionBuilder.h"
#include "projection/runtime/ProjectionFramePipeline.h"
#include "projection/runtime/ProjectionInvalidation.h"
#include "projection/runtime/ProjectionLifecycle.h"
#include "projection/runtime/ProjectionProgress.h"
#include "projection/runtime/ProjectionWorldEvents.h"
#include "projection/world/ProjectionPlacement.h"

#include "overlay/BoundsWireframe.h"
#include "place/PlaceHelper.h"
#include "plugin/LHolo.h"
#include "structure/capture/StructureCapture.h"
#include "structure/StructureLoader.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <memory>
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

auto& logger() {
    return LHolo::getInstance().getSelf().getLogger();
}

bool enableStructureProjection(
    ProjectionState& state,
    BaseActorRenderContext& renderContext,
    std::shared_ptr<structure::LoadedStructure const> loaded
) {
    ProjectionState next;
    if (!prepareProjectionState(next, renderContext, std::move(loaded))) return false;
    auto& client = renderContext.mClientInstance;
    auto* player = client.getLocalPlayer();
    if (auto const anchor = ProjectionSession::getInstance().consumeAnchor()) {
        next.anchor = BlockPos{anchor->x, anchor->y, anchor->z};
    } else {
        auto const& position = player->getPosition();
        // Player position is the feet/air cell. Default a newly loaded
        // structure to the supporting ground cell directly below it.
        next.anchor = BlockPos(
            std::floor(position.x),
            std::floor(position.y) - 1,
            std::floor(position.z)
        );
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
    place::resetDimensionSession();
    structure::resetDimensionSession();
    ProjectionSession::getInstance().suspendForDimension(
        state.structureGeneration,
        state.dimensionId,
        ProjectionAnchor{anchor.x, anchor.y, anchor.z}
    );
    suspendProjectionState(state);
    structure::showActionHint(
        i18n::Message{i18n::TextKey::ActionHintProjectionSuspended},
        structure::kProjectionLifecycleHintDurationMs
    );
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
        auto const mirrorMode = structure::getMirrorMode();
        auto const rotationTurns = structure::getRotationQuarterTurns();
        auto const mirror = getProjectionMirror(mirrorMode);
        auto const rotation = getProjectionRotation(rotationTurns);
        LegacyStructureSettings transformSettings{
            mirror,
            rotation,
            nullptr,
            BoundingBox{}
        };
        bool const identityTransform = mirrorMode == 0 && rotationTurns == 0;
        auto const offsetX = structure::getOffsetX();
        auto const offsetY = structure::getOffsetY();
        auto const offsetZ = structure::getOffsetZ();
        auto const layerDisplayMode = structure::getLayerDisplayMode();
        auto const layerAxis = structure::getLayerAxis();
        auto const maxLayer = layerAxis == structure::LayerAxis::Material
            ? std::max(0, static_cast<int>(state.structure->materialCount) - 1)
            : std::max(
                0,
                (layerAxis == structure::LayerAxis::X
                    ? state.structure->sizeX : state.structure->sizeY) - 1
            );
        auto const displayLayer = std::clamp(
            structure::getDisplayLayer(), 0, maxLayer
        );
        auto& session = ProjectionSession::getInstance();
        auto const structureOpacity = session.opacity();
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
                .structureOpacity         = structureOpacity,
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
            .structureOpacity         = structureOpacity,
            .correctionFillOpacity    = correctionFillOpacity,
            .correctionOutlineOpacity = correctionOutlineOpacity,
            .identityTransform        = identityTransform
        };
        processProjectionOpaqueFrame(
            state,
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
        state.anchor.x + structure::getOffsetX(),
        state.anchor.y + structure::getOffsetY(),
        state.anchor.z + structure::getOffsetZ()
    };
    auto const structureOpacity = ProjectionSession::getInstance().opacity();

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

    if (!state.meshPreflightDone) {
        auto const countValid = [](auto const& meshes) {
            return std::count_if(meshes.begin(), meshes.end(), [](auto const& mesh) {
                return mesh && mesh->isValid();
            });
        };
        std::size_t normalMeshes{};
        for (auto const& sectionState : state.sections) {
            for (auto const& mesh : sectionState.meshes) {
                if (mesh && mesh->isValid()) ++normalMeshes;
            }
        }
        auto const warningMeshes = countValid(state.warningFillSectionMeshes);
        auto const outlineMeshes = countValid(state.correctionOutlineSectionMeshes);
        auto const wrongFillMeshes = countValid(state.wrongFillSectionMeshes);
        auto const wrongOutlineMeshes = countValid(state.wrongOutlineSectionMeshes);
        auto const liquidMeshes = countValid(state.liquidProxySectionMeshes);
        auto const placeholderMeshes = countValid(state.blockEntityPlaceholderSectionMeshes);
        if (normalMeshes + warningMeshes + outlineMeshes + wrongFillMeshes
            + wrongOutlineMeshes + liquidMeshes + placeholderMeshes != 0) {
            state.meshPreflightDone = true;
        }
    }

    try {
        submitProjectionMeshPass(
            state,
            renderContext,
            client,
            renderOrigin,
            camera,
            structureOpacity,
            renderAlphaLayer,
            ProjectionSession::getInstance().structureBoundsEnabled(),
            ProjectionSession::getInstance().correctionSeeThrough(),
            ProjectionSession::getInstance().missingSeeThrough()
        );
    } catch (std::exception const& exception) {
        logger().error("Projection immediate mesh submission failed: {}", exception.what());
        resetProjectionState(state);
        return;
    } catch (...) {
        logger().error("Projection immediate mesh submission failed with an unknown exception");
        resetProjectionState(state);
        return;
    }
}

} // namespace

bool shouldSuppressProjectionHitSelect(BlockPos const& pos) {
    return ProjectionSession::getInstance().withLockedState(
        [&](ProjectionState& state, overlay::BoundsWireframe&) {
            if (state.enabled && state.structure) {
                auto const found = state.expectedWorldBlockIndices->find(
                    std::tuple{pos.x, pos.y, pos.z}
                );
                if (found != state.expectedWorldBlockIndices->end()) {
                    auto const correctionState = state.correctionStates[found->second];
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
        }
    );
}

void renderProjectionFrame(BaseActorRenderContext& renderContext, bool renderAlphaLayer) {
    bool clearStructure = false;
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

            if (auto loaded = structure::getLoaded(); loaded && loaded->generation != state.structureGeneration) {
                auto& client = renderContext.mClientInstance;
                auto* player = client.getLocalPlayer();
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
                if (!enableStructureProjection(state, renderContext, std::move(loaded))) {
                    resetProjectionState(state);
                    logger().error("Could not enable loaded structure projection");
                } else {
                    session.cancelDimensionSuspension();
                    if (activationStatus == DimensionActivationStatus::Resuming) {
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
            renderProjection(state, renderContext, renderAlphaLayer);
        }
    );
    if (clearStructure) structure::clear();
}

} // namespace lholo::projection::detail
