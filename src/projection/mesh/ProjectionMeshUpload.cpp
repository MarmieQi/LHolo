// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "projection/mesh/ProjectionMeshUpload.h"

#include "plugin/LHolo.h"
#include "projection/core/ProjectionInternalTypes.h"
#include "projection/mesh/ProjectionMeshWorker.h"
#include "projection/core/ProjectionState.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <string_view>
#include <utility>

#include "mc/client/renderer/Tessellator.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "ll/api/mod/NativeMod.h"

namespace lholo::projection::detail {
namespace {

auto& logger() {
    return LHolo::getInstance().getSelf().getLogger();
}

void markSectionDirty(ProjectionState& state, std::size_t section, bool incremental) {
    if (section >= state.sections.size()) return;
    auto& sectionState = state.sections[section];
    sectionState.dirty = true;
    sectionState.modelDirty = true;
    sectionState.incrementalDirty = sectionState.incrementalDirty || incremental;
    ++sectionState.requestedRevision;
}

} // namespace

void uploadCompletedProjectionMeshes(ProjectionState& root, Tessellator& tessellator) {
    if (auto bufferService = tessellator.mBufferResourceService.lock()) {
        auto const uploadStarted = std::chrono::steady_clock::now();
        for (std::size_t uploaded = 0; uploaded < 2; ++uploaded) {
            if (std::chrono::steady_clock::now() - uploadStarted >= std::chrono::milliseconds(1)) break;
            auto completed = takeCompletedSectionBuilds(1);
            if (completed.empty()) break;
            auto result = std::move(completed.front());
            // There is only one completion queue and one upload budget. Route
            // directly to the runtime instance; removed/reloaded files have no
            // matching generation and are discarded without touching live state.
            auto* owner = &root;
            if (root.structure && root.structure->independentFiles) {
                auto const found = std::find_if(root.files.begin(), root.files.end(), [&](auto const& file) {
                    return file->structureGeneration == result.structureGeneration
                        && file->meshWorkerGeneration == result.workerGeneration;
                });
                if (found == root.files.end()) continue;
                owner = found->get();
            }
            auto& state = *owner;
            if (result.workerGeneration != state.meshWorkerGeneration
                || result.structureGeneration != state.structureGeneration
                || result.section >= state.sections.size()) {
                continue;
            }
    
            auto const section = result.section;
            state.sections[section].buildInFlight = false;
            // Obsolete work cannot report a failure for the current placement
            // or contribute to disabling its asynchronous worker.
            if (result.revision != state.sections[section].requestedRevision) {
                ++state.meshWorkerDiscardedBuilds;
                state.sections[section].dirty = true;
                continue;
            }
            if (!result.success) {
                markSectionDirty(state, section, true);
                logger().warn(
                    "Projection mesh worker section {} revision {} failed: {} (expected {}, actual {}; snapshot {} us, build {} us)",
                    section,
                    result.revision,
                    result.failureReason.empty() ? "unknown failure" : result.failureReason,
                    result.expectedVertexCount,
                    result.actualVertexCount,
                    result.snapshotPrepareMicros,
                    result.workerBuildMicros
                );
                if (++state.consecutiveMeshWorkerFailures >= 3) {
                    state.asyncMeshBuildingEnabled = false;
                    disableMeshWorkerForSession();
                    stopMeshWorker();
                    logger().warn("Projection mesh worker failed three times; using synchronous fallback for this session");
                }
                continue;
            }
    
            auto const sectionUploadStarted = std::chrono::steady_clock::now();
            auto uploadCpuMesh = [&](std::unique_ptr<mce::Mesh> cpuMesh, std::string_view name) {
                if (!cpuMesh || cpuMesh->mMeshData.get().size() == 0) {
                    return std::unique_ptr<mce::Mesh>{};
                }
                auto data = std::move(cpuMesh->mMeshData.get());
                return std::make_unique<mce::Mesh>(bufferService, std::move(data), false, name);
            };
            try {
                constexpr std::array<std::string_view, static_cast<std::size_t>(RenderBucket::Count)> bucketNames{
                    "LHoloOpaque", "LHoloAlphaTest", "LHoloBlend", "LHoloBlendToOpaque"};
                std::array<std::unique_ptr<mce::Mesh>, static_cast<std::size_t>(RenderBucket::Count)> uploadedMeshes;
                if (!result.correctionOnly) {
                    for (std::size_t bucket = 0; bucket < uploadedMeshes.size(); ++bucket)
                        uploadedMeshes[bucket] =
                            uploadCpuMesh(std::move(result.sectionMeshes[bucket]), bucketNames[bucket]);
                }
                auto warningFill = uploadCpuMesh(std::move(result.warningFillMesh), "LHoloWarningFill");
                auto correctionOutline =
                    uploadCpuMesh(std::move(result.correctionOutlineMesh), "LHoloCorrectionOutline");
                auto wrongFill = uploadCpuMesh(std::move(result.wrongFillMesh), "LHoloWrongFill");
                auto wrongOutline = uploadCpuMesh(std::move(result.wrongOutlineMesh), "LHoloWrongOutline");
                auto blockEntityPlaceholder =
                    uploadCpuMesh(std::move(result.blockEntityPlaceholderMesh), "LHoloBlockEntityPlaceholder");
                if (!result.correctionOnly) {
                    for (std::size_t bucket = 0; bucket < uploadedMeshes.size(); ++bucket)
                        state.sections[section].meshes[bucket] = std::move(uploadedMeshes[bucket]);
                    state.transparentSections.resize(state.sections.size());
                    state.transparentSections[section] = std::move(result.transparentGeometry);
                    state.blockEntityPlaceholderSectionMeshes[section] = std::move(blockEntityPlaceholder);
                    state.sections[section].modelDirty = false;
                }
                state.warningFillSectionMeshes[section] = std::move(warningFill);
                state.correctionOutlineSectionMeshes[section] = std::move(correctionOutline);
                state.wrongFillSectionMeshes[section] = std::move(wrongFill);
                state.wrongOutlineSectionMeshes[section] = std::move(wrongOutline);
                state.sections[section].uploadedRevision = result.revision;
                state.sections[section].meshBounds = result.meshBounds;
                if (!result.correctionOnly) {
                    state.sections[section].biomeRefresh = result.biomeRefresh;
                    auto const delay =
                        armProjectionBiomeRefresh(state.sections[section].biomeRefresh, state.biomeRefreshFrame);
                    if (result.biomeRefresh.sampleFailed || result.biomeRefresh.blackGrassFace) {
                        auto const retries = ++state.biomeSampleRetryCount;
                        if ((retries & (retries - 1)) == 0) {
                            logger().debug("Projection tint diagnostics: {} affected builds; section {}, rejected "
                                           "sample {}, black face {}, auto refresh {}/{}, pending {}, delay {} frames",
                                           retries, section, result.biomeRefresh.sampleFailed,
                                           result.biomeRefresh.blackGrassFace, result.biomeRefresh.automaticRefreshes,
                                           kMaxProjectionBiomeAutoRefreshes, result.biomeRefresh.pending, delay);
                        }
                    }
                }
                ++state.drawMeshRevision;
                state.sections[section].incrementalDirty = false;
                state.sections[section].interactiveDirty = false;
                state.sections[section].dirty = false;
                state.consecutiveMeshWorkerFailures = 0;
                auto const uploadMicros =
                    static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                                   std::chrono::steady_clock::now() - sectionUploadStarted)
                                                   .count());
                ++state.meshWorkerUploadedSections;
                if (result.correctionOnly)
                    ++state.meshWorkerCorrectionOnlySections;
                state.meshWorkerSnapshotMicros += result.snapshotPrepareMicros;
                state.meshWorkerSnapshotDataMicros += result.snapshotDataMicros;
                state.meshWorkerChunkViewMicros += result.chunkViewMicros;
                state.meshWorkerBuildMicros += result.workerBuildMicros;
                state.meshWorkerUploadMicros += uploadMicros;
                state.meshWorkerPeakSnapshotMicros = std::max(
                    state.meshWorkerPeakSnapshotMicros, result.snapshotPrepareMicros
                );
                state.meshWorkerPeakSnapshotDataMicros = std::max(
                    state.meshWorkerPeakSnapshotDataMicros, result.snapshotDataMicros
                );
                state.meshWorkerPeakChunkViewMicros = std::max(
                    state.meshWorkerPeakChunkViewMicros, result.chunkViewMicros
                );
                state.meshWorkerPeakBuildMicros = std::max(
                    state.meshWorkerPeakBuildMicros, result.workerBuildMicros
                );
                state.meshWorkerPeakUploadMicros = std::max(
                    state.meshWorkerPeakUploadMicros, uploadMicros
                );
                if (state.meshWorkerUploadedSections % 64 == 0) {
                    auto const count = state.meshWorkerUploadedSections;
                    logger().debug(
                        "Projection mesh worker: {} sections; snapshot {}/{} us (data {}/{}, chunkView {}/{}), build {}/{} us, upload {}/{} us; visible {}/{}, discarded {}; correction snapshot {} B, COW total {} B, overlay-only {}",
                        count,
                        state.meshWorkerSnapshotMicros / count,
                        state.meshWorkerPeakSnapshotMicros,
                        state.meshWorkerSnapshotDataMicros / count,
                        state.meshWorkerPeakSnapshotDataMicros,
                        state.meshWorkerChunkViewMicros / count,
                        state.meshWorkerPeakChunkViewMicros,
                        state.meshWorkerBuildMicros / count,
                        state.meshWorkerPeakBuildMicros,
                        state.meshWorkerUploadMicros / count,
                        state.meshWorkerPeakUploadMicros,
                        state.visibleSectionCount,
                        state.sections.size(),
                        state.meshWorkerDiscardedBuilds,
                        result.correctionSnapshotBytes,
                        state.correctionStates.copiedBytes(), state.meshWorkerCorrectionOnlySections
                    );
                }
            } catch (std::exception const& exception) {
                logger().warn(
                    "Projection mesh upload for section {} revision {} failed: {}",
                    section, result.revision, exception.what()
                );
                markSectionDirty(state, section, true);
                if (++state.consecutiveMeshWorkerFailures >= 3) {
                    state.asyncMeshBuildingEnabled = false;
                    disableMeshWorkerForSession();
                    stopMeshWorker();
                    logger().warn("Projection mesh upload failed three times; using synchronous fallback for this session");
                }
            } catch (...) {
                logger().warn(
                    "Projection mesh upload for section {} revision {} failed with a non-standard exception",
                    section, result.revision
                );
                markSectionDirty(state, section, true);
                if (++state.consecutiveMeshWorkerFailures >= 3) {
                    state.asyncMeshBuildingEnabled = false;
                    disableMeshWorkerForSession();
                    stopMeshWorker();
                    logger().warn("Projection mesh upload failed three times; using synchronous fallback for this session");
                }
            }
        }
    }
}

} // namespace lholo::projection::detail
