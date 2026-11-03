// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/mesh/ProjectionMeshScheduler.h"
#include "projection/mesh/ProjectionMeshResidency.h"

#include "plugin/LHolo.h"
#include "projection/mesh/ProjectionMeshWorker.h"
#include "projection/core/ProjectionRules.h"
#include "projection/correction/CorrectionMeshRules.h"
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

#include "mc/client/game/ClientInstance.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/block/BlockTessellator.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/chunk/ChunkSource.h"
#include "mc/world/level/chunk/ChunkSourceViewGenerateMode.h"
#include "mc/world/level/chunk/ChunkViewSource.h"
#include "mc/world/level/chunk/LevelChunk.h"

#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/Bedrock.h"

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
    return selectProjectionBuild(state.sections.size(), state.meshBuildSelections,
        state.dirtySectionCursor, [&](std::size_t index) {
            auto const& section = state.sections[index];
            auto const dx = section.center.x + static_cast<float>(state.anchor.x + settings.offsetX) - cameraPosition.x;
            auto const dy = section.center.y + static_cast<float>(state.anchor.y + settings.offsetY) - cameraPosition.y;
            auto const dz = section.center.z + static_cast<float>(state.anchor.z + settings.offsetZ) - cameraPosition.z;
            bool ready = section.dirty && !section.buildInFlight && canBuildResidentSection(state,index,cameraPosition)
                && !projectionSectionHasPendingCorrection(state, index, settings);
            return ProjectionBuildCandidate{ready, section.buildInFlight,
                section.buildVisible, section.prefetchVisible, section.incrementalDirty, dx*dx+dy*dy+dz*dz,
                section.interactiveDirty};
        });
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

bool projectionSectionHasPendingCorrection(ProjectionState const& state, std::size_t section,
                                           ProjectionSectionBuildSettings const& settings) {
    if (state.correctionEvents.empty()) return false;
    auto bounds = projectionSectionCellWorldBounds(state.localSectionKeys[section], *state.structure,
        {static_cast<long long>(state.anchor.x) + settings.offsetX,
         static_cast<long long>(state.anchor.y) + settings.offsetY,
         static_cast<long long>(state.anchor.z) + settings.offsetZ}, settings.mirrorMode, settings.rotationTurns, 1);
    return state.correctionEvents.intersects(bounds.minimum, bounds.maximum);
}

void scheduleProjectionMeshBuild(ProjectionState& state, Tessellator& tessellator, BlockSource& region,
                                 Vec3 const& cameraPosition, ProjectionSectionBuildSettings const& buildSettings) {
    auto settings = buildSettings;
    // A single in-flight task gives each BlockTessellator exclusive access and
    // naturally coalesces repeated changes into the latest revision.
    if (!state.asyncMeshBuildingEnabled || meshWorkerIsBusy())
        return;

    auto const selected = selectNextDirtySection(state, cameraPosition, settings);
    if (!selected)
        return;

    auto const snapshotStarted = std::chrono::steady_clock::now();
    auto const section = *selected;
    settings.correctionOnly =
        canBuildCorrectionOnly(state.sections[section].modelDirty, state.sections[section].uploadedRevision);
    BlockPos minimum{INT_MAX, INT_MAX, INT_MAX};
    BlockPos maximum{INT_MIN, INT_MIN, INT_MIN};
    if (!settings.correctionOnly) {
        bool needsBiomeSamples{};
        for (auto const index : state.sectionBlockIndices[section]) {
            auto const* block = state.structure->renderBlocks[index].block;
            auto const* liquid = state.structure->renderBlocks[index].liquid;
            needsBiomeSamples = needsBiomeSamples || (block && projectionUsesBiomeTint(*block))
                || (liquid && projectionUsesBiomeTint(*liquid));
            auto const local = transformStructurePosition(state.structure->renderBlocks[index], *state.structure,
                                                          settings.mirrorMode, settings.rotationTurns);
            BlockPos const world{state.anchor.x + settings.offsetX + local.x,
                                 state.anchor.y + settings.offsetY + local.y,
                                 state.anchor.z + settings.offsetZ + local.z};
            minimum.x = std::min(minimum.x, world.x);
            minimum.y = std::min(minimum.y, world.y);
            minimum.z = std::min(minimum.z, world.z);
            maximum.x = std::max(maximum.x, world.x);
            maximum.y = std::max(maximum.y, world.y);
            maximum.z = std::max(maximum.z, world.z);
        }
        for (auto const& [x, y, z] : state.sectionExtraBlockPositions[section]) {
            auto const local = transformStructurePosition(BlockPos{x, y, z}, *state.structure, settings.mirrorMode,
                                                          settings.rotationTurns);
            BlockPos const world{state.anchor.x + settings.offsetX + local.x,
                                 state.anchor.y + settings.offsetY + local.y,
                                 state.anchor.z + settings.offsetZ + local.z};
            minimum.x = std::min(minimum.x, world.x);
            minimum.y = std::min(minimum.y, world.y);
            minimum.z = std::min(minimum.z, world.z);
            maximum.x = std::max(maximum.x, world.x);
            maximum.y = std::max(maximum.y, world.y);
            maximum.z = std::max(maximum.z, world.z);
        }
        if (minimum.x == INT_MAX) {
            auto const& center = state.sections[section].center;
            BlockPos const world{
                state.anchor.x + settings.offsetX + static_cast<int>(center.x),
                state.anchor.y + settings.offsetY + static_cast<int>(center.y),
                state.anchor.z + settings.offsetZ + static_cast<int>(center.z),
            };
            minimum = world;
            maximum = world;
        }
        auto const padding = needsBiomeSamples ? projectionBiomeSamplingPadding() : std::array<int, 3>{2, 2, 2};
        minimum = BlockPos{minimum.x - padding[0], minimum.y - padding[1], minimum.z - padding[2]};
        maximum = BlockPos{maximum.x + padding[0], maximum.y + padding[1], maximum.z + padding[2]};
    }

    auto snapshot = std::make_shared<ProjectionState>();
    snapshot->level = state.level;
    snapshot->dimension = state.dimension;
    snapshot->structureGeneration = state.structureGeneration;
    snapshot->anchor = state.anchor;
    // Structure data and the virtual projected world are immutable for one
    // placement generation. Mutable correction pages are captured below only
    // for this section and its six face neighbors.
    snapshot->structure = state.structure;
    snapshot->blockActorRendererAvailable = state.blockActorRendererAvailable;
    snapshot->sectionBlockIndices = {state.sectionBlockIndices[section]};
    snapshot->sectionExtraBlockPositions = {state.sectionExtraBlockPositions[section]};
    // Correction face culling only needs extras in this section and its six
    // direct neighbors. Avoid copying the complete sparse set for every async
    // section build on large projections.
    auto const [sectionX, sectionY, sectionZ] = state.localSectionKeys[section];
    constexpr int neighborSections[7][3] = {{0, 0, 0}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
                                            {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    std::vector<std::size_t> correctionSections;
    correctionSections.reserve(7);
    for (auto const& delta : neighborSections) {
        auto const found =
            state.localSectionIndices.find(std::tuple{sectionX + delta[0], sectionY + delta[1], sectionZ + delta[2]});
        if (found == state.localSectionIndices.end())
            continue;
        correctionSections.push_back(found->second);
        auto const& positions = state.sectionExtraBlockPositions[found->second];
        snapshot->extraBlockPositions.insert(positions.begin(), positions.end());
    }
    snapshot->correctionSnapshot.emplace(state.correctionStates.capture(correctionSections));
    snapshot->worldLookup = state.worldLookup;
    snapshot->expectedWorldBlockActors = state.expectedWorldBlockActors;
    snapshot->sections.resize(1);
    snapshot->sections[0].meshBounds = state.sections[section].meshBounds;
    snapshot->sections[0].biomeRefresh = state.sections[section].biomeRefresh;
    snapshot->warningFillSectionMeshes.resize(1);
    snapshot->correctionOutlineSectionMeshes.resize(1);
    snapshot->wrongFillSectionMeshes.resize(1);
    snapshot->wrongOutlineSectionMeshes.resize(1);
    snapshot->blockEntityPlaceholderSectionMeshes.resize(1);

    auto const snapshotDataFinished = std::chrono::steady_clock::now();
    auto const snapshotDataMicros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(snapshotDataFinished - snapshotStarted).count());
    // 26.51 removed the ChunkViewSource(ChunkSource&, LoadMode) export; the
    // player chunk-view factory is the only remaining creation path.
    std::shared_ptr<ChunkViewSource> chunkView;
    if (!settings.correctionOnly) {
        auto* localPlayer = []() -> LocalPlayer* {
            auto client = ll::service::getClientInstance();
            return client ? client->getLocalPlayer() : nullptr;
        }();
        if (!localPlayer)
            return;
        chunkView = localPlayer->$_createChunkSource(region.getChunkSource());
        if (!chunkView)
            return;
        chunkView->move(
            minimum, maximum, false, ChunkSourceViewGenerateMode::DontGenerateOnlyGet,
            [](gsl::span<std::shared_ptr<LevelChunk>>) {}, nullptr);
    }
    auto const chunkViewMicros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - snapshotDataFinished)
            .count());
    auto const snapshotPrepareMicros = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - snapshotStarted)
            .count());

    auto const workerGeneration = state.meshWorkerGeneration;
    auto const structureGeneration = state.structureGeneration;
    auto const revision = state.sections[section].requestedRevision;
    auto const weakBufferService = tessellator.mBufferResourceService;
    auto* level = state.level;
    auto* dimension = state.dimension;
    auto const submitted = submitMeshWorkerTask(workerGeneration, [settings, snapshot, chunkView, level, dimension,
                                                                   weakBufferService, workerGeneration,
                                                                   structureGeneration, revision, section,
                                                                   snapshotPrepareMicros, snapshotDataMicros,
                                                                   chunkViewMicros]() mutable {
        AsyncSectionBuildResult result;
        result.workerGeneration = workerGeneration;
        result.structureGeneration = structureGeneration;
        result.revision = revision;
        result.correctionOnly = settings.correctionOnly;
        result.section = section;
        result.snapshotPrepareMicros = snapshotPrepareMicros;
        result.correctionSnapshotBytes = snapshot->correctionSnapshot->retainedBytes();
        result.snapshotDataMicros = snapshotDataMicros;
        result.chunkViewMicros = chunkViewMicros;
        auto const workerStarted = std::chrono::steady_clock::now();
        try {
            Tessellator localTessellator(weakBufferService);
            if (settings.correctionOnly) {
                buildProjectionCorrectionSection(*snapshot, localTessellator, 0, Tessellator::UploadMode::Never,
                                                 settings);
            } else {
                auto localRegion =
                    std::make_unique<BlockSource>(*level, *dimension, *chunkView, false, true, false, false);
                BlockTessellator localBlockTessellator(localRegion.get());
                localBlockTessellator.mCachedGetBlock.get() =
                    [snapshot, region = localRegion.get()](BlockPos const& position) -> Block const& {
                    auto const found =
                        snapshot->worldLookup->blockAt(std::tuple{position.x, position.y, position.z});
                    return !found ? region->getBlock(position) : *found;
                };
                buildProjectionSection(*snapshot, localTessellator, localBlockTessellator, *localRegion, 0,
                                       Tessellator::UploadMode::Never, settings);
            }
            result.workerBuildMicros = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - workerStarted)
                    .count());
            for (std::size_t bucket = 0; bucket < result.sectionMeshes.size(); ++bucket) {
                result.sectionMeshes[bucket] = std::move(snapshot->sections[0].meshes[bucket]);
            }
            result.meshBounds = snapshot->sections[0].meshBounds;
            result.biomeRefresh = snapshot->sections[0].biomeRefresh;
            result.worldOcclusionDependent = snapshot->sections[0].worldOcclusionDependent;
            result.warningFillMesh = std::move(snapshot->warningFillSectionMeshes[0]);
            result.correctionOutlineMesh = std::move(snapshot->correctionOutlineSectionMeshes[0]);
            result.wrongFillMesh = std::move(snapshot->wrongFillSectionMeshes[0]);
            result.wrongOutlineMesh = std::move(snapshot->wrongOutlineSectionMeshes[0]);
            if (!result.correctionOnly) result.transparentGeometry = snapshot->transparentSections[0];
            result.blockEntityPlaceholderMesh = std::move(snapshot->blockEntityPlaceholderSectionMeshes[0]);

            constexpr std::array<std::string_view, 4> meshNames{"opaque", "alpha", "alphaOneSided", "blend"};
            result.success = true;
            for (std::size_t bucket = 0; bucket < result.sectionMeshes.size(); ++bucket) {
                if (!validateMeshData(result.sectionMeshes[bucket], meshNames[bucket], result)) {
                    result.success = false;
                    break;
                }
            }
            result.success = result.success && validateMeshData(result.warningFillMesh, "warningFill", result) &&
                             validateMeshData(result.correctionOutlineMesh, "correctionOutline", result) &&
                             validateMeshData(result.wrongFillMesh, "wrongFill", result) &&
                             validateMeshData(result.wrongOutlineMesh, "wrongOutline", result) &&
                             validateMeshData(result.blockEntityPlaceholderMesh, "blockEntityPlaceholder", result);
        } catch (std::exception const& exception) {
            result.workerBuildMicros = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - workerStarted)
                    .count());
            result.success = false;
            result.failureReason = std::string{"worker exception: "} + exception.what();
        } catch (...) {
            result.workerBuildMicros = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - workerStarted)
                    .count());
            result.success = false;
            result.failureReason = "worker non-standard exception";
        }
        return result;
    });
    if (submitted) {
        ++state.meshBuildSelections;
        state.dirtySectionCursor = section + 1;
        state.sections[section].buildInFlight = true;
        state.sections[section].dirty = false;
    } else if (++state.consecutiveMeshWorkerFailures >= 3) {
        state.asyncMeshBuildingEnabled = false;
        disableMeshWorkerForSession();
        stopMeshWorker();
        logger().warn(
            "Projection mesh task submission failed three times; using synchronous fallback for this session");
    }
}

void buildNextProjectionSectionSynchronously(ProjectionState& state, Tessellator& tessellator,
                                             BlockTessellator& blockTessellator, BlockSource& region,
                                             Vec3 const& cameraPosition,
                                             ProjectionSectionBuildSettings const& buildSettings) {
    auto settings = buildSettings;
    if (state.asyncMeshBuildingEnabled)
        return;

    // Keep the same visibility ordering and fairness as the async path.
    auto const selected = selectNextDirtySection(state, cameraPosition, settings);
    if (!selected)
        return;
    auto const section = *selected;
    settings.correctionOnly =
        canBuildCorrectionOnly(state.sections[section].modelDirty, state.sections[section].uploadedRevision);
    ++state.meshBuildSelections;
    state.dirtySectionCursor = section + 1;
    ++state.drawMeshRevision; // synchronous construction may replace several meshes
    buildProjectionSection(state, tessellator, blockTessellator, region, section, Tessellator::UploadMode::Buffered,
                           settings);
    if (!settings.correctionOnly)
        armProjectionBiomeRefresh(state.sections[section].biomeRefresh, state.biomeRefreshFrame);
    state.sections[section].dirty = false;
    state.sections[section].modelDirty = false;
    state.sections[section].uploadedRevision = state.sections[section].requestedRevision;
    recordProjectionResidentBytes(state, section);
    state.sections[section].incrementalDirty = false;
    state.sections[section].interactiveDirty = false;
}

} // namespace lholo::projection::detail
