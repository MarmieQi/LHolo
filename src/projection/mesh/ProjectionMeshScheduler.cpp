// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/mesh/ProjectionMeshScheduler.h"

#include "plugin/LHolo.h"
#include "projection/mesh/ProjectionMeshWorker.h"
#include "projection/core/ProjectionRules.h"
#include "projection/core/ProjectionState.h"
#include "structure/StructureLoader.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/block/BlockTessellator.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/chunk/ChunkSource.h"
#include "mc/world/level/chunk/ChunkSourceViewGenerateMode.h"
#include "mc/world/level/chunk/ChunkViewSource.h"
#include "mc/world/level/chunk/LevelChunk.h"

#include "ll/api/mod/NativeMod.h"

namespace lholo::projection::detail {
namespace {

auto& logger() {
    return LHolo::getInstance().getSelf().getLogger();
}

std::optional<std::size_t> selectNextDirtySection(
    ProjectionState const& state,
    Vec3 const&            cameraPosition,
    ProjectionSectionBuildSettings const& settings
) {
    std::optional<std::size_t> selected;
    bool                       selectedIncremental{};
    float                      selectedDistance = std::numeric_limits<float>::max();
    for (std::size_t section = 0; section < state.sections.size(); ++section) {
        auto const& sectionState = state.sections[section];
        if (!sectionState.dirty || sectionState.buildInFlight) continue;
        auto const incremental = sectionState.incrementalDirty;
        auto const center = sectionState.center + Vec3{
            static_cast<float>(state.anchor.x + settings.offsetX),
            static_cast<float>(state.anchor.y + settings.offsetY),
            static_cast<float>(state.anchor.z + settings.offsetZ)
        };
        auto const delta = center - cameraPosition;
        auto const distance = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
        if (!selected || (incremental && !selectedIncremental)
            || (incremental == selectedIncremental && distance < selectedDistance)) {
            selected = section;
            selectedIncremental = incremental;
            selectedDistance = distance;
        }
    }
    return selected;
}

bool validateMeshData(
    std::unique_ptr<mce::Mesh> const& mesh,
    std::string_view                 meshName,
    AsyncSectionBuildResult&         result
) {
    if (!mesh) return true;
    auto const& data = mesh->mMeshData.get();
    auto const vertexCount = data.mPositions.get().size();
    auto fail = [&](std::string_view reason, std::uint64_t expected, std::uint64_t actual) {
        result.failureReason.assign(meshName);
        result.failureReason.append(": ");
        result.failureReason.append(reason);
        result.expectedVertexCount = expected;
        result.actualVertexCount = actual;
        return false;
    };
    if (vertexCount == 0) return fail("empty position field", 1, 0);
    if (data.size() == 0) return fail("MeshData::size is zero", 1, 0);

    // UploadMode::Never intentionally leaves the upload-side Mesh vertex count
    // unset. At this stage positions are authoritative; each enabled CPU
    // attribute must contain the same number of vertices.
    auto fieldIsConsistent = [&](auto const& field, std::string_view fieldName) {
        if (field.empty() || field.size() == vertexCount) return true;
        std::string reason{"attribute count mismatch: "};
        reason.append(fieldName);
        return fail(reason, vertexCount, field.size());
    };
    if (!fieldIsConsistent(data.mNormals.get(), "normals")
        || !fieldIsConsistent(data.mTangents.get(), "tangents")
        || !fieldIsConsistent(data.mColors.get(), "colors")
        || !fieldIsConsistent(data.mBoneId0s.get(), "boneId0")
        || !fieldIsConsistent(data.mPBRTextureIndices.get(), "pbrTextureIndices")
        || !fieldIsConsistent(data.mMERS.get(), "mers")
        || !fieldIsConsistent(data.mGeoType.get(), "geoType")) return false;
    for (std::size_t uv = 0; uv < std::size(data.mTextureUVs); ++uv) {
        std::string fieldName{"textureUV"};
        fieldName += static_cast<char>('0' + uv);
        if (!fieldIsConsistent(data.mTextureUVs[uv].get(), fieldName)) return false;
    }
    return true;
}

} // namespace

void scheduleProjectionMeshBuild(
    ProjectionState&                      state,
    Tessellator&                          tessellator,
    BlockSource&                          region,
    Vec3 const&                           cameraPosition,
    ProjectionSectionBuildSettings const& settings
) {
    // A single in-flight task gives each BlockTessellator exclusive access and
    // naturally coalesces repeated changes into the latest revision.
    if (!state.asyncMeshBuildingEnabled || meshWorkerIsBusy()) return;

    auto const selected = selectNextDirtySection(state, cameraPosition, settings);
    if (!selected) return;

    auto const snapshotStarted = std::chrono::steady_clock::now();
    auto const section = *selected;
    BlockPos minimum{INT_MAX, INT_MAX, INT_MAX};
    BlockPos maximum{INT_MIN, INT_MIN, INT_MIN};
    for (auto const index : state.sectionBlockIndices[section]) {
        auto const local = transformStructurePosition(
            state.structure->renderBlocks[index], *state.structure, settings.mirrorMode,
            settings.rotationTurns
        );
        BlockPos const world{
            state.anchor.x + settings.offsetX + local.x,
            state.anchor.y + settings.offsetY + local.y,
            state.anchor.z + settings.offsetZ + local.z
        };
        minimum.x = std::min(minimum.x, world.x);
        minimum.y = std::min(minimum.y, world.y);
        minimum.z = std::min(minimum.z, world.z);
        maximum.x = std::max(maximum.x, world.x);
        maximum.y = std::max(maximum.y, world.y);
        maximum.z = std::max(maximum.z, world.z);
    }
    minimum = BlockPos{minimum.x - 2, minimum.y - 2, minimum.z - 2};
    maximum = BlockPos{maximum.x + 2, maximum.y + 2, maximum.z + 2};

    auto snapshot = std::make_shared<ProjectionState>();
    snapshot->level = state.level;
    snapshot->dimension = state.dimension;
    snapshot->structureGeneration = state.structureGeneration;
    snapshot->anchor = state.anchor;
    // Structure data and the virtual projected world are immutable for one
    // placement generation. Only correction bytes need a task-local copy.
    snapshot->structure = state.structure;
    snapshot->correctionStates = state.correctionStates;
    snapshot->blockActorRendererAvailable = state.blockActorRendererAvailable;
    snapshot->sectionBlockIndices = {state.sectionBlockIndices[section]};
    snapshot->expectedWorldBlocks = state.expectedWorldBlocks;
    snapshot->expectedWorldBlockActors = state.expectedWorldBlockActors;
    snapshot->expectedWorldBlockIndices = state.expectedWorldBlockIndices;
    snapshot->sections.resize(1);
    snapshot->warningFillSectionMeshes.resize(1);
    snapshot->correctionOutlineSectionMeshes.resize(1);
    snapshot->wrongFillSectionMeshes.resize(1);
    snapshot->wrongOutlineSectionMeshes.resize(1);
    snapshot->liquidProxySectionMeshes.resize(1);
    snapshot->blockEntityPlaceholderSectionMeshes.resize(1);

    auto const snapshotDataFinished = std::chrono::steady_clock::now();
    auto const snapshotDataMicros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            snapshotDataFinished - snapshotStarted
        ).count()
    );
    auto chunkView = std::make_shared<ChunkViewSource>(
        region.getChunkSource(), ChunkSource::LoadMode::Deferred
    );
    chunkView->move(
        minimum,
        maximum,
        false,
        ChunkSourceViewGenerateMode::DontGenerateOnlyGet,
        [](gsl::span<std::shared_ptr<LevelChunk>>) {},
        nullptr
    );
    auto const chunkViewMicros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - snapshotDataFinished
        ).count()
    );
    auto const snapshotPrepareMicros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - snapshotStarted
        ).count()
    );

    auto const workerGeneration = state.meshWorkerGeneration;
    auto const structureGeneration = state.structureGeneration;
    auto const revision = state.sections[section].requestedRevision;
    auto const weakBufferService = tessellator.mBufferResourceService;
    auto* level = state.level;
    auto* dimension = state.dimension;
    auto const submitted = submitMeshWorkerTask(
        workerGeneration,
        [settings, snapshot, chunkView, level, dimension, weakBufferService,
         workerGeneration, structureGeneration, revision, section,
         snapshotPrepareMicros, snapshotDataMicros, chunkViewMicros]() mutable {
            AsyncSectionBuildResult result;
            result.workerGeneration = workerGeneration;
            result.structureGeneration = structureGeneration;
            result.revision = revision;
            result.section = section;
            result.snapshotPrepareMicros = snapshotPrepareMicros;
            result.snapshotDataMicros = snapshotDataMicros;
            result.chunkViewMicros = chunkViewMicros;
            auto const workerStarted = std::chrono::steady_clock::now();
            try {
                auto localRegion = std::make_unique<BlockSource>(
                    *level, *dimension, *chunkView, false, true, false
                );
                BlockTessellator localBlockTessellator(localRegion.get());
                localBlockTessellator.mCachedGetBlock.get()
                    = [snapshot, region = localRegion.get()](BlockPos const& position) -> Block const& {
                        auto const found = snapshot->expectedWorldBlocks->find(
                            std::tuple{position.x, position.y, position.z}
                        );
                        return found == snapshot->expectedWorldBlocks->end()
                            ? region->getBlock(position) : *found->second;
                    };
                Tessellator localTessellator(weakBufferService);
                buildProjectionSection(
                    *snapshot,
                    localTessellator,
                    localBlockTessellator,
                    *localRegion,
                    0,
                    Tessellator::UploadMode::Never,
                    settings
                );
                result.workerBuildMicros = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - workerStarted
                    ).count()
                );
                for (std::size_t bucket = 0; bucket < result.sectionMeshes.size(); ++bucket) {
                    result.sectionMeshes[bucket] = std::move(snapshot->sections[0].meshes[bucket]);
                }
                result.warningFillMesh = std::move(snapshot->warningFillSectionMeshes[0]);
                result.correctionOutlineMesh = std::move(snapshot->correctionOutlineSectionMeshes[0]);
                result.wrongFillMesh = std::move(snapshot->wrongFillSectionMeshes[0]);
                result.wrongOutlineMesh = std::move(snapshot->wrongOutlineSectionMeshes[0]);
                result.liquidProxyMesh = std::move(snapshot->liquidProxySectionMeshes[0]);
                result.blockEntityPlaceholderMesh
                    = std::move(snapshot->blockEntityPlaceholderSectionMeshes[0]);

                constexpr std::array<std::string_view, 4> meshNames{
                    "opaque", "alpha", "alphaOneSided", "blend"
                };
                result.success = true;
                for (std::size_t bucket = 0; bucket < result.sectionMeshes.size(); ++bucket) {
                    if (!validateMeshData(result.sectionMeshes[bucket], meshNames[bucket], result)) {
                        result.success = false;
                        break;
                    }
                }
                result.success = result.success
                    && validateMeshData(result.warningFillMesh, "warningFill", result)
                    && validateMeshData(result.correctionOutlineMesh, "correctionOutline", result)
                    && validateMeshData(result.liquidProxyMesh, "liquidProxy", result)
                    && validateMeshData(
                        result.blockEntityPlaceholderMesh, "blockEntityPlaceholder", result
                    );
            } catch (std::exception const& exception) {
                result.workerBuildMicros = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - workerStarted
                    ).count()
                );
                result.success = false;
                result.failureReason = std::string{"worker exception: "} + exception.what();
            } catch (...) {
                result.workerBuildMicros = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - workerStarted
                    ).count()
                );
                result.success = false;
                result.failureReason = "worker non-standard exception";
            }
            return result;
        }
    );
    if (submitted) {
        state.sections[section].buildInFlight = true;
        state.sections[section].dirty = false;
    } else if (++state.consecutiveMeshWorkerFailures >= 3) {
        state.asyncMeshBuildingEnabled = false;
        disableMeshWorkerForSession();
        stopMeshWorker();
        logger().warn(
            "Projection mesh task submission failed three times; using synchronous fallback for this session"
        );
    }
}

void buildNextProjectionSectionSynchronously(
    ProjectionState&                      state,
    Tessellator&                          tessellator,
    BlockTessellator&                     blockTessellator,
    BlockSource&                          region,
    ProjectionSectionBuildSettings const& settings
) {
    if (state.asyncMeshBuildingEnabled) return;

    // Compatibility path: preserve one synchronous section per frame when
    // worker creation or three consecutive worker operations fail.
    for (std::size_t attempt = 0; attempt < state.sections.size(); ++attempt) {
        auto const section = state.dirtySectionCursor++ % state.sections.size();
        if (!state.sections[section].dirty) continue;
        state.sections[section].dirty = false;
        buildProjectionSection(
            state,
            tessellator,
            blockTessellator,
            region,
            section,
            Tessellator::UploadMode::Buffered,
            settings
        );
        state.sections[section].uploadedRevision = state.sections[section].requestedRevision;
        state.sections[section].incrementalDirty = false;
        state.meshPreflightDone = false;
        break;
    }
}

} // namespace lholo::projection::detail
