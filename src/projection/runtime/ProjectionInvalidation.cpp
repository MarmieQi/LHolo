// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/runtime/ProjectionInvalidation.h"

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/runtime/ProjectionProgress.h"
#include "projection/core/ProjectionRules.h"
#include "projection/core/ProjectionState.h"
#include "structure/StructureLoader.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace lholo::projection::detail {
namespace {

void markSectionDirty(ProjectionState& state, std::size_t section, bool incremental, bool models = true) {
    if (section >= state.sections.size()) return;
    auto& sectionState = state.sections[section];
    if (models) sectionState.biomeRefresh = {};
    sectionState.modelDirty = sectionState.modelDirty || models;
    sectionState.dirty = true;
    sectionState.incrementalDirty = sectionState.incrementalDirty || incremental;
    ++sectionState.requestedRevision;
}

void markAllSectionsDirty(ProjectionState& state, bool incremental, bool models = true) {
    for (std::size_t section = 0; section < state.sections.size(); ++section) {
        markSectionDirty(state, section, incremental, models);
    }
}

void cacheSettings(ProjectionState &state, ProjectionInvalidationSettings const &settings) {
    state.cachedRotation = settings.rotationTurns;
    state.cachedMirror = settings.mirrorMode;
    state.cachedOffsetX = settings.offsetX;
    state.cachedOffsetY = settings.offsetY;
    state.cachedOffsetZ = settings.offsetZ;
    state.cachedLayerDisplayMode = settings.layerDisplayMode;
    state.cachedDisplayLayer = settings.displayLayer;
    state.cachedLayerAxis = settings.layerAxis;
    state.cachedCorrectionFillOpacity = settings.correctionFillOpacity;
    state.cachedCorrectionOutlineOpacity = settings.correctionOutlineOpacity;
}

} // namespace

ProjectionInvalidationResult reconcileProjectionInvalidation(
    ProjectionState&                      state,
    ProjectionInvalidationSettings const& settings
) {
    ProjectionInvalidationResult result;
    result.geometryTransformChanged = state.cachedRotation != settings.rotationTurns
        || state.cachedMirror != settings.mirrorMode;
    result.placementMoved = state.cachedOffsetX != settings.offsetX
        || state.cachedOffsetY != settings.offsetY
        || state.cachedOffsetZ != settings.offsetZ;
    result.layerChanged = state.cachedLayerDisplayMode != settings.layerDisplayMode
        || state.cachedDisplayLayer != settings.displayLayer
        || state.cachedLayerAxis != settings.layerAxis;
    bool const correctionStyleChanged
        = std::abs(state.cachedCorrectionFillOpacity - settings.correctionFillOpacity) > 0.0001f
        || std::abs(state.cachedCorrectionOutlineOpacity - settings.correctionOutlineOpacity) > 0.0001f;
    if (!result.geometryTransformChanged && !result.placementMoved && !result.layerChanged
        && !correctionStyleChanged) {
        return result;
    }

    if (state.fileTransform) {
        // Task files keep their source/section layout. Per-cell visibility and
        // ownership refresh through resumable section jobs after publication.
        bool const resetWorld = result.geometryTransformChanged || result.placementMoved;
        if (resetWorld || correctionStyleChanged) {
            for (std::size_t i = 0; i < state.sections.size(); ++i) {
                bool models = result.geometryTransformChanged ||
                              (result.placementMoved && (state.sections[i].worldOcclusionDependent ||
                                                         state.correctionStates.sectionHasResolvedCells(i)));
                markSectionDirty(state, i, false, models);
                if (!resetWorld)
                    continue;
                state.warningFillSectionMeshes[i].reset();
                state.correctionOutlineSectionMeshes[i].reset();
                state.wrongFillSectionMeshes[i].reset();
                state.wrongOutlineSectionMeshes[i].reset();
                state.sectionExtraBlockPositions[i].clear();
                if (result.geometryTransformChanged) {
                    for (auto &mesh : state.sections[i].meshes)
                        mesh.reset();
                    state.sections[i].residentBytes = 0;
                    state.blockEntityPlaceholderSectionMeshes[i].reset();
                }
            }
        }
        if (result.geometryTransformChanged || result.placementMoved) {
            state.correctionStates.fill(CorrectionState::Missing);
            state.fileProgress.placed = state.fileProgress.visiblePlaced = 0;
            state.fileProgress.wrongType = state.fileProgress.wrongState = state.fileProgress.extra = 0;
            state.progressExtraCount = 0;
            state.detectedExtraBlockPositions.clear();
            state.detectedExtraSectionCounts.clear();
            state.extraBlockPositions.clear();
            state.taskAirRefresh.clear();
            state.extraScan.reset();
            if (result.geometryTransformChanged) {
                state.transparentSections.clear();
                state.transparentCache = {};
                state.structureBoundsMesh.reset();
            }
            ++state.drawMeshRevision;
        }
        if (result.placementViewChanged()) {
            state.hasResidencyCamera = false;
            ++state.progressRevision;
        }
        cacheSettings(state, settings);
        return result;
    }

    if (result.geometryTransformChanged || correctionStyleChanged) {
        markAllSectionsDirty(state, false, result.geometryTransformChanged);
    }
    if (result.placementMoved && !result.geometryTransformChanged) {
        // World-dependent face visibility must be sampled at the new origin.
        // Other local geometry survives the move; in-flight old snapshots do not.
        for (std::size_t i = 0; i < state.sections.size(); ++i) {
            auto& sectionState = state.sections[i];
            if (sectionState.worldOcclusionDependent) {
                markSectionDirty(state, i, false);
                continue;
            }
            ++sectionState.requestedRevision;
            if (sectionState.buildInFlight) sectionState.dirty = true;
        }
    }

    auto const layerIsVisible = [&](structure::LoadedStructure::RenderBlock const& entry) {
        return isLayerVisible(
            settings.layerAxis == structure::LayerAxis::X ? entry.x : entry.y,
            settings.layerDisplayMode,
            settings.displayLayer,
            entry.materialIndex,
            entry.liquidMaterialIndex,
            settings.layerAxis
        );
    };
    // LayerRange changes only invalidate sections containing blocks whose
    // visibility crossed the old/new boundary.
    if (result.layerChanged && !result.geometryTransformChanged) {
        std::vector<bool> changedSections(state.sections.size());
        auto const oldLayerVisible = [&](structure::LoadedStructure::RenderBlock const& entry) {
            if (!state.cachedLayerDisplayMode || !state.cachedLayerAxis) return false;
            auto const layer = *state.cachedLayerAxis == structure::LayerAxis::X
                ? entry.x : entry.y;
            return isLayerVisible(
                layer, *state.cachedLayerDisplayMode, state.cachedDisplayLayer,
                entry.materialIndex, entry.liquidMaterialIndex, *state.cachedLayerAxis
            );
        };
        for (std::size_t index = 0; index < state.structure->renderBlocks.size(); ++index) {
            auto const& entry = state.structure->renderBlocks[index];
            auto const visible = layerIsVisible(entry);
            if (oldLayerVisible(entry) == visible) continue;
            auto const section = state.correctionStates.section(index);
            changedSections[section] = true;
            markSectionDirty(state, section, false);
            state.correctionStates.setVisible(index, visible);
        }
        // A visible support can disappear across a section boundary when the
        // display slice changes. Update its neighbors once per changed section.
        constexpr int neighbors[6][3]{{-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}};
        for (std::size_t i = 0; i < changedSections.size(); ++i) {
            if (!changedSections[i]) continue;
            auto const [x,y,z] = state.localSectionKeys[i];
            for (auto const& d : neighbors) {
                auto const found = state.localSectionIndices.find(std::tuple{x+d[0],y+d[1],z+d[2]});
                if (found != state.localSectionIndices.end() && !changedSections[found->second])
                    markSectionDirty(state, found->second, false);
            }
        }
    }

    if (result.placementViewChanged()) {
        state.hasResidencyCamera = false;
    }
    cacheSettings(state, settings);

    if (result.placementViewChanged()) {
        state.detectedExtraBlockPositions.clear();
        state.detectedExtraSectionCounts.clear();
        state.extraBlockPositions.clear();
        for (auto& positions : state.sectionExtraBlockPositions) positions.clear();
        state.progressExtraCount = 0;
        state.extraScan.reset();
        markAllSectionsDirty(state, false);
        publishErrorProgress(
            state.progressWrongTypeCount,
            state.progressWrongStateCount,
            0
        );
    }

    // Rotation/mirror alter local block models, so only those require throwing
    // away every GPU mesh. XYZ movement stays represented by the world matrix.
    if (result.geometryTransformChanged) {
        ++state.drawMeshRevision;
        state.transparentSections.clear();
        state.transparentCache = {};
        state.correctionStates.fill(CorrectionState::Missing);
        for (auto& sectionState : state.sections) {
            sectionState.residentBytes = 0;
            for (auto& mesh : sectionState.meshes) mesh.reset();
        }
        for (auto& mesh : state.warningFillSectionMeshes) mesh.reset();
        for (auto& mesh : state.correctionOutlineSectionMeshes) mesh.reset();
        for (auto& mesh : state.wrongFillSectionMeshes) mesh.reset();
        for (auto& mesh : state.wrongOutlineSectionMeshes) mesh.reset();
        for (auto& mesh : state.blockEntityPlaceholderSectionMeshes) mesh.reset();
        state.structureBoundsMesh.reset();
        std::fill(state.progressCorrect.begin(), state.progressCorrect.end(), 0);
        ++state.progressRevision;
        std::fill(state.progressErrorKind.begin(), state.progressErrorKind.end(), 0);
        state.progressCorrectCount = 0;
        state.progressVisibleCorrectCount = 0;
        state.progressWrongTypeCount = 0;
        state.progressWrongStateCount = 0;
        state.progressExtraCount = 0;
        resetPublishedBuildProgressCounts();
    }

    if (result.geometryTransformChanged || result.layerChanged) {
        state.progressVisibleCorrectCount = 0;
        std::uint64_t visibleTotal{};
        for (std::size_t index = 0; index < state.structure->renderBlocks.size(); ++index) {
            auto const& entry = state.structure->renderBlocks[index];
            if (!layerIsVisible(entry)) continue;
            ++visibleTotal;
            if (state.progressCorrect[index] != 0) {
                ++state.progressVisibleCorrectCount;
            }
        }
        state.progressVisibleTotal=visibleTotal;
        publishVisibleProgress(state.progressVisibleCorrectCount, visibleTotal);
    }

    return result;
}

} // namespace lholo::projection::detail
