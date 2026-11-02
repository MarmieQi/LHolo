// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/runtime/ProjectionFramePipeline.h"

#include "projection/correction/ProjectionCorrectionTracker.h"
#include "projection/mesh/ProjectionMeshScheduler.h"
#include "projection/mesh/ProjectionMeshUpload.h"
#include "projection/runtime/ProjectionProgress.h"
#include "projection/runtime/ProjectionVisibility.h"
#include "projection/runtime/IndependentFileProjection.h"
#include "projection/runtime/IndependentFileFrame.h"
#include "projection/correction/CorrectionFrameBudget.h"
#include "projection/world/ProjectionWorldBounds.h"
#include "plugin/LHolo.h"
#include "ll/api/mod/NativeMod.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include <chrono>
#include <limits>
#include "projection/runtime/ProjectionSession.h"
#include "projection/core/ProjectionState.h"
#include "structure/StructureLoader.h"
#include "render/BoxWireframeMesh.h"
#include <vector>

#include "mc/client/renderer/block/BlockTessellator.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/levelgen/structure/LegacyStructureSettings.h"

namespace lholo::projection::detail {
namespace {

void ensureStructureBoundsMesh(
    ProjectionState& state,
    Tessellator&     tessellator,
    int              rotationTurns
) {
    // Cache 24 line vertices per visible file in one GPU batch. Bounds are
    // independent of winner cells, slicing and the selected editor file.
    if (state.structureBoundsMesh) return;

    if (state.structure->composed) {
        std::vector<render::BoxWireframeBounds> boxes;
        boxes.reserve(state.structure->sourceBounds.size());
        for (auto const& bounds : state.structure->sourceBounds)
            boxes.push_back({
                {static_cast<float>(bounds.minimum[0]), static_cast<float>(bounds.minimum[1]),
                 static_cast<float>(bounds.minimum[2])},
                {static_cast<float>(bounds.maximum[0]), static_cast<float>(bounds.maximum[1]),
                 static_cast<float>(bounds.maximum[2])}});
        state.structureBoundsMesh = render::buildBoxWireframesMesh(tessellator, boxes, "LHoloFileBounds");
        return;
    }

    auto const rotated = rotationTurns == 1 || rotationTurns == 3;
    auto const width = static_cast<float>(
        rotated ? state.structure->sizeZ : state.structure->sizeX
    );
    auto const height = static_cast<float>(state.structure->sizeY);
    auto const depth = static_cast<float>(
        rotated ? state.structure->sizeX : state.structure->sizeZ
    );
    state.structureBoundsMesh = render::buildBoxWireframeMesh(tessellator,
        {0,0,0}, {width,height,depth}, "LHoloStructureBounds");
}

} // namespace

void processProjectionOpaqueFrames(ProjectionState& root, std::span<ProjectionFrameTarget const> targets,
                                   BaseActorRenderContext& renderContext, Vec3 const& cameraPosition) {
    if (targets.empty()) {
        discardPendingProjectionEvents();
        // Removed files can leave one completed worker result. Reap it even
        // when there is nothing to draw, so CPU meshes do not remain queued.
        uploadCompletedProjectionMeshes(root, renderContext.mScreenContext.tessellator);
        return;
    }
    auto* player = renderContext.mClientInstance.getLocalPlayer();
    if (!player)
        return;
    auto& region = player->getDimensionBlockSource();
    auto& tessellator = renderContext.mScreenContext.tessellator;
    // Placement/visibility are prepared for every file before spending the
    // shared correction budget. Budget exhaustion never skips their drawing.
    for (auto const& target : targets)
        updateProjectionVisibility(*target.state, renderContext, cameraPosition);

    CorrectionFrameBudget budget;
    auto started = CorrectionFrameBudget::Clock::now();
    // Intake is bounded too. Leave most of the shared slice for actual
    // comparisons when world notifications arrive continuously.
    for (int batch = 0; batch < 8 && budget.available(); ++batch) {
        if (batch && CorrectionFrameBudget::Clock::now() - started >= CorrectionFrameBudget::DefaultDuration / 4)
            break;
        auto events = takePendingBlockChanges(32);
        auto chunks = takePendingLoadedSubChunks(2);
        if (events.empty() && chunks.empty())
            break;
        if (root.structure->independentFiles) {
            distributeIndependentFileEvents(root, events, chunks);
        } else {
            auto const& settings = targets.front().build;
            bool rotated = (settings.rotationTurns & 1) != 0;
            std::array<long long, 3> origin{static_cast<long long>(root.anchor.x) + settings.offsetX,
                                            static_cast<long long>(root.anchor.y) + settings.offsetY,
                                            static_cast<long long>(root.anchor.z) + settings.offsetZ};
            ProjectionWorldBounds bounds{origin,
                                         {origin[0] + (rotated ? root.structure->sizeZ : root.structure->sizeX),
                                          origin[1] + root.structure->sizeY,
                                          origin[2] + (rotated ? root.structure->sizeX : root.structure->sizeZ)}};
            for (auto const& event : events) {
                std::array<long long, 3> position{event.position.x, event.position.y, event.position.z};
                if (bounds.intersects(position, position))
                    root.correctionEvents.push({event.position.x, event.position.y, event.position.z},
                                               event.destroyedAt);
            }
            for (auto const& chunk : chunks) {
                auto [x, y, z] = chunk;
                std::array<long long, 3> lo{static_cast<long long>(x) * 16, static_cast<long long>(y) * 16,
                                            static_cast<long long>(z) * 16};
                if (bounds.intersects(lo, {lo[0] + 15, lo[1] + 15, lo[2] + 15}))
                    root.correctionSubChunks.push({x, y, z});
            }
        }
    }
    auto distance = [&](CorrectionPosition const& position, float scale) {
        auto dx = position[0] * scale - cameraPosition.x;
        auto dy = position[1] * scale - cameraPosition.y;
        auto dz = position[2] * scale - cameraPosition.z;
        return dx * dx + dy * dy + dz * dz;
    };
    auto fileDistance = [&](ProjectionFrameTarget const& target) {
        auto const& state = *target.state;
        bool rotated = (target.build.rotationTurns & 1) != 0;
        float lo[3]{static_cast<float>(state.anchor.x + target.build.offsetX),
                    static_cast<float>(state.anchor.y + target.build.offsetY),
                    static_cast<float>(state.anchor.z + target.build.offsetZ)};
        float size[3]{static_cast<float>(rotated ? state.structure->sizeZ : state.structure->sizeX),
                      static_cast<float>(state.structure->sizeY),
                      static_cast<float>(rotated ? state.structure->sizeX : state.structure->sizeZ)};
        float camera[3]{cameraPosition.x, cameraPosition.y, cameraPosition.z};
        float result{};
        for (int axis = 0; axis < 3; ++axis) {
            auto delta = std::max({lo[axis] - camera[axis], camera[axis] - lo[axis] - size[axis], 0.0f});
            result += delta * delta;
        }
        return result;
    };
    std::vector<float> fileDistances;
    fileDistances.reserve(targets.size());
    for (auto const& target : targets)
        fileDistances.push_back(fileDistance(target));
    while (budget.available()) {
        auto selected =
            selectCorrectionWork(targets.size(), root.correctionWorkCursor,
                                 [&](std::size_t file, CorrectionWorkKind kind) -> std::optional<CorrectionPriority> {
                                     auto const& target = targets[file];
                                     auto const& state = *target.state;
                                     if (kind == CorrectionWorkKind::Event) {
                                         if (auto event = state.correctionEvents.front())
                                             return CorrectionPriority{0, distance(event->position, 1)};
                                     } else if (kind == CorrectionWorkKind::Loaded) {
                                         if (auto chunk = state.correctionSubChunks.front())
                                             return CorrectionPriority{0, distance(chunk->position, 16)};
                                     } else if (kind == CorrectionWorkKind::Initial) {
                                         if (!state.correctionScan.empty())
                                             return CorrectionPriority{0, fileDistances[file]};
                                      } else if (!state.extraScan.complete()) {
                                         return CorrectionPriority{0, fileDistances[file]};
                                     }
                                     return std::nullopt;
                                 });
        if (!selected)
            break;
        auto const& target = targets[selected->file];
        auto& state = *target.state;
        auto const& settings = target.build;
        auto changes =
            updateCorrectionTracker(state, region, *target.transform, settings.mirrorMode, settings.rotationTurns,
                                    settings.offsetX, settings.offsetY, settings.offsetZ, target.layerDisplayMode,
                                    target.displayLayer, target.layerAxis, cameraPosition, selected->kind, budget);
        if (changes.overall)
            publishPlacedProgress(state.progressCorrectCount);
        if (changes.visible)
            publishVisiblePlacedProgress(state.progressVisibleCorrectCount);
        if (changes.errors)
            publishErrorProgress(state.progressWrongTypeCount, state.progressWrongStateCount, state.progressExtraCount);
    }
    auto micros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(CorrectionFrameBudget::Clock::now() - started).count());
    ++root.correctionFrames;
    root.correctionMicros += micros;
    root.correctionWork += budget.work();
    root.correctionPeakMicros = std::max(root.correctionPeakMicros, micros);
    if (root.correctionFrames % 120 == 0) {
        std::uint64_t emptyCells{}, probes{};
        for (auto const& target : targets) {
            emptyCells += target.state->correctionEmptyFastCells;
            probes += target.state->correctionEmptyProbes;
            target.state->correctionEmptyFastCells = target.state->correctionEmptyProbes = 0;
        }
        LHolo::getInstance().getSelf().getLogger().debug(
            "Projection correction: {} files, average/peak {} / {} us, average {} work units, shared target {} us; empty fast-path {} cells / {} probes",
            targets.size(), root.correctionMicros / root.correctionFrames, root.correctionPeakMicros,
            root.correctionWork / root.correctionFrames, CorrectionFrameBudget::DefaultDuration.count(), emptyCells, probes);
        root.correctionFrames = root.correctionMicros = root.correctionPeakMicros = root.correctionWork = 0;
    }

    uploadCompletedProjectionMeshes(root, tessellator);
    // Maintain biome/bounds state for every file, then dispatch at most one
    // build globally (also when the worker has fallen back to synchronous).
    for (auto const& target : targets) {
        auto& state = *target.state;
        if (meshWorkerIsDisabledForSession() && state.asyncMeshBuildingEnabled) {
            state.asyncMeshBuildingEnabled = false;
            for (auto& section : state.sections)
                if (section.buildInFlight) {
                    section.buildInFlight = false;
                    section.dirty = true;
                }
        }
        state.blockTessellator->setRegion(region);
        refreshProjectionBiomes(
            state.sections, state.biomeRefreshVisibleCursor, state.biomeRefreshCursor,
            [&](ProjectionBiomeRefresh const& refresh) {
                return region.areChunksFullyLoaded(
                    BlockPos{refresh.minimum[0], refresh.minimum[1], refresh.minimum[2]},
                    BlockPos{refresh.maximum[0], refresh.maximum[1], refresh.maximum[2]});
            },
            [&](std::size_t section) {
                auto& dirty = state.sections[section];
                dirty.dirty = dirty.incrementalDirty = true;
                dirty.modelDirty = true;
                ++dirty.requestedRevision;
            },
            ++state.biomeRefreshFrame);
        if (ProjectionSession::getInstance().structureBoundsEnabled())
            ensureStructureBoundsMesh(state, tessellator, target.build.rotationTurns);
    }
    if (!meshWorkerIsBusy()) {
        std::optional<std::size_t> selectedFile;
        int bestRank = std::numeric_limits<int>::max();
        float bestDistance = std::numeric_limits<float>::max();
        bool fair = targets.size() > 1 && (root.meshBuildSelections + 1) % 8 == 0;
        forEachIndependentProjectionFile(targets.size(), root.fileFrameCursor, [&](std::size_t file) {
            if (fair && selectedFile)
                return;
            auto const& target = targets[file];
            for (auto const& section : target.state->sections) {
                if (!section.dirty || section.buildInFlight)
                    continue;
                int rank = section.interactiveDirty ? -1
                                                    : projectionBuildRank(section.buildVisible, section.prefetchVisible,
                                                                          section.incrementalDirty);
                float d = fileDistances[file];
                if (!selectedFile || rank < bestRank || (rank == bestRank && d < bestDistance)) {
                    selectedFile = file;
                    bestRank = rank;
                    bestDistance = d;
                }
                if (fair)
                    break;
            }
        });
        if (selectedFile) {
            auto const& target = targets[*selectedFile];
            auto& state = *target.state;
            auto selectionsBefore = state.meshBuildSelections;
            scheduleProjectionMeshBuild(state, tessellator, region, cameraPosition, target.build);
            buildNextProjectionSectionSynchronously(state, tessellator, *state.blockTessellator, region, cameraPosition,
                                                    target.build);
            if (&state != &root && state.meshBuildSelections != selectionsBefore)
                ++root.meshBuildSelections;
        }
    }
    for (auto const& target : targets)
        if (!target.state->asyncMeshBuildingEnabled)
            updateProjectionVisibility(*target.state, renderContext, cameraPosition);
}

} // namespace lholo::projection::detail
