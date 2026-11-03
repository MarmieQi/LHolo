// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Projection runtime state and resource ownership. Lifecycle operations remain
// in runtime/ProjectionLifecycle; this type makes the ownership boundary explicit.

#pragma once

#include "projection/correction/BannerCorrection.h"
#include "projection/correction/CorrectionAirScan.h"
#include "projection/correction/CorrectionCellMask.h"
#include "projection/correction/CorrectionScanProgress.h"
#include "projection/correction/CorrectionScanScheduler.h"
#include "projection/correction/CorrectionStateStore.h"
#include "projection/correction/CorrectionWorkQueues.h"
#include "projection/runtime/TaskSectionRefresh.h"
#include <unordered_set>

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/world/ProjectionWorldLookup.h"
#include "projection/ProjectionTypes.h"
#include "projection/core/ProjectionVisibilityRules.h"
#include "projection/core/ProjectionDrawCache.h"
#include "projection/core/ProjectionBiomeRefresh.h"
#include "projection/core/ProjectionTransparentGeometry.h"
#include "structure/StructureSession.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <optional>
#include <set>
#include <variant>
#include <vector>

#include "mc/client/renderer/block/BlockTessellator.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"

class Dimension;
class IClientInstance;
class Level;

namespace lholo::structure {
struct LoadedStructure;
}

namespace lholo::projection::detail {

using TextureVariant =
    std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture>;

struct SectionState {
    Vec3    center{};
    ProjectionBounds buildBounds;
    ProjectionBounds meshBounds;
    ProjectionBiomeRefresh biomeRefresh;
    bool renderVisible{true};
    bool buildVisible{true};
    bool prefetchVisible{true};
    bool    dirty{};
    bool    modelDirty{true};
    bool    worldOcclusionDependent{};
    bool    incrementalDirty{};
    bool    interactiveDirty{};
    bool    buildInFlight{};
    bool    residencyDeferred{};
    std::size_t residentBytes{};
    std::uint64_t requestedRevision{};
    std::uint64_t uploadedRevision{};
    std::array<std::unique_ptr<mce::Mesh>, static_cast<std::size_t>(RenderBucket::Count)> meshes;
};

struct ProjectionState {
    // Independent files use exactly the existing single-file pipeline. The
    // root only owns their lifetime and routes shared event/worker queues.
    std::string fileId;
    std::optional<structure::detail::StructureTransformSnapshot> fileTransform;
    std::vector<std::shared_ptr<ProjectionState>> files;
    std::vector<SubChunkKey> taskNeighborChanges;
    TaskSectionRefresh taskSectionRefresh;
    TaskAirRefresh taskAirRefresh;
    BuildProgress fileProgress;
    std::vector<std::weak_ptr<ProjectionState>> taskRuntimeFiles;
    struct NeighborCorrection {
        std::size_t file;
        CorrectionStateStore::Snapshot states;
        std::set<SubChunkKey> extrasWorld;
    };
    std::vector<NeighborCorrection> neighborCorrections;
    std::shared_ptr<TaskWorldLookups const> taskWorld;
    std::shared_ptr<ProjectionWorldLookup const> localWorldLookup;
    std::vector<std::size_t> blockActorIndices;
    CorrectionEventQueue correctionEvents;
    CorrectionSubChunkQueue correctionSubChunks;
    CorrectionWorkCursor correctionWorkCursor;
    std::uint64_t correctionFrames{}, correctionMicros{}, correctionPeakMicros{}, correctionWork{};
    std::uint64_t correctionEmptyFastCells{}, correctionEmptyProbes{};
    std::uint64_t correctionReceivedCells{}, correctionReceivedSections{};
    std::array<std::uint64_t, 4> correctionWorkByKind{};
    std::size_t fileFrameCursor{};
    std::uint64_t progressVisibleTotal{};
    bool                            enabled{};
    BlockPos                        anchor{};
    IClientInstance*                client{};
    Level*                          level{};
    Dimension*                      dimension{};
    int                             dimensionId{};
    std::optional<mce::TexturePtr>  terrainTexture;
    std::optional<TextureVariant>   terrainTextureVariant;
    std::shared_ptr<structure::LoadedStructure const> structure;
    std::uint64_t                   structureGeneration{};
    std::uint64_t                   activationGeneration{};
    std::unique_ptr<BlockTessellator> blockTessellator;
    CorrectionStateStore correctionStates;
    std::optional<CorrectionStateStore::Snapshot> correctionSnapshot;
    // Both builders share this read boundary. Only the active state is mutable;
    // a worker retains a bounded, immutable neighborhood instead of all cells.
    CorrectionState renderCorrectionState(std::size_t index) const {
        if (!worldLookup->owns(index))
            return CorrectionState::Correct;
        return correctionSnapshot ? correctionSnapshot->rawState(index) : correctionStates.get(index);
    }
    CorrectionState correctionAtWorld(ProjectionWorldLookup::Key const &) const;
    bool refreshTaskCellVisibility(std::size_t index);
    // One byte per structure block. This is updated by the existing bounded
    // correction scan, so the HUD never performs its own world-block queries.
    std::vector<uchar>              progressCorrect;
    // Changes whenever the per-cell correctness snapshot changes. Consumers
    // can use this to rebuild derived data (such as material HUD rows) only
    // when necessary instead of rescanning the structure every frame.
    std::uint64_t                   progressRevision{};
    // 0 = no placement error, 1 = wrong block type, 2 = wrong state/direction.
    // Updating one byte and two counters keeps the HUD O(1) per frame.
    std::vector<uchar>              progressErrorKind;
    std::shared_ptr<std::unordered_set<std::size_t>> blockActorRendererAvailable{
        std::make_shared<std::unordered_set<std::size_t>>()};
    std::uint64_t                   progressCorrectCount{};
    std::uint64_t                   progressVisibleCorrectCount{};
    std::uint64_t                   progressWrongTypeCount{};
    std::uint64_t                   progressWrongStateCount{};
    std::uint64_t                   progressExtraCount{};
    CorrectionScanScheduler         correctionScan;
    CorrectionScanProgress          initialCorrectionScan;
    CorrectionAirScan               extraScan;
    std::vector<CorrectionSectionIndex> correctionIndex;
    std::vector<BrokenProjectionCell> pendingBrokenCells;
    int                             cachedRotation{-1};
    int                             cachedMirror{-1};
    int                             cachedOffsetX{};
    int                             cachedOffsetY{};
    int                             cachedOffsetZ{};
    std::optional<structure::LayerDisplayMode> cachedLayerDisplayMode;
    int                             cachedDisplayLayer{-1};
    std::optional<structure::LayerAxis> cachedLayerAxis;
    float                           cachedCorrectionFillOpacity{-1.0f};
    float                           cachedCorrectionOutlineOpacity{-1.0f};
    std::vector<std::vector<std::size_t>> sectionBlockIndices;
    // Extra blocks occupy cells that have no render-block index. The detected
    // set covers the whole source region for HUD counting; the render set and
    // per-section sets contain only the current visible range. All stay sparse.
    std::unordered_map<SubChunkKey, std::size_t, SubChunkKeyHash> localSectionIndices;
    std::vector<SubChunkKey>              localSectionKeys;
    std::set<SubChunkKey>                 detectedExtraBlockPositions;
    std::unordered_map<SubChunkKey, std::size_t, SubChunkKeyHash> detectedExtraSectionCounts;
    std::set<SubChunkKey>                 extraBlockPositions;
    std::vector<std::set<SubChunkKey>>    sectionExtraBlockPositions;
    // Correction meshes are split by category so the see-through (X-ray) option
    // can apply to the wrong-type/wrong-state markers only, never to the many
    // "missing" outlines. warningFill/correctionOutline hold the MISSING cells;
    // wrongFill/wrongOutline hold WrongType + WrongState.
    std::vector<std::unique_ptr<mce::Mesh>> warningFillSectionMeshes;
    std::vector<std::unique_ptr<mce::Mesh>> correctionOutlineSectionMeshes;
    std::vector<std::unique_ptr<mce::Mesh>> wrongFillSectionMeshes;
    std::vector<std::unique_ptr<mce::Mesh>> wrongOutlineSectionMeshes;
    std::vector<std::shared_ptr<ProjectionTransparentSection const>> transparentSections;
    ProjectionTransparentCache transparentCache;
    std::vector<std::unique_ptr<mce::Mesh>> blockEntityPlaceholderSectionMeshes;
    std::unique_ptr<mce::Mesh>              structureBoundsMesh;
    std::vector<SectionState>               sections;
    std::size_t                             dirtySectionCursor{};
    std::size_t                             biomeRefreshCursor{};
    std::size_t                             biomeRefreshVisibleCursor{};
    std::uint64_t                           biomeRefreshFrame{};
    std::uint64_t                           biomeSampleRetryCount{};
    std::uint64_t                           meshBuildSelections{};
    std::size_t                             visibleSectionCount{};
    // Render-thread/session-owned values only. Session reset releases caches
    // after workers stop; no cached pointer can outlive a mesh or world.
    std::uint64_t                           drawMeshRevision{};
    std::uint64_t                           visibilityRevision{};
    std::uint64_t                           residencyTrimRevision{};
    Vec3                                   residencyCamera{};
    bool                                   hasResidencyCamera{};
    std::optional<float>                   residencyDistanceLimit;
    std::size_t                           residencyFileCount{};
    std::optional<ProjectionViewKey>         visibilityKey;
    ProjectionDrawCache                     drawCache;
    ProjectionDrawTimings                   drawTimings;
    std::uint64_t                           meshWorkerGeneration{};
    int                                     consecutiveMeshWorkerFailures{};
    bool                                    asyncMeshBuildingEnabled{true};
    std::uint64_t                           meshWorkerUploadedSections{};
    std::uint64_t                           meshWorkerCorrectionOnlySections{};
    std::uint64_t                           meshWorkerDiscardedBuilds{};
    std::uint64_t                           meshWorkerSnapshotMicros{};
    std::uint64_t                           meshWorkerSnapshotDataMicros{};
    std::uint64_t                           meshWorkerChunkViewMicros{};
    std::uint64_t                           meshWorkerBuildMicros{};
    std::uint64_t                           meshWorkerUploadMicros{};
    std::uint64_t                           meshWorkerPeakSnapshotMicros{};
    std::uint64_t                           meshWorkerPeakSnapshotDataMicros{};
    std::uint64_t                           meshWorkerPeakChunkViewMicros{};
    std::uint64_t                           meshWorkerPeakBuildMicros{};
    std::uint64_t                           meshWorkerPeakUploadMicros{};
    std::shared_ptr<ProjectionWorldLookup const> worldLookup{std::make_shared<ProjectionWorldLookup>()};
    std::shared_ptr<ExpectedBlockActorMap>   expectedWorldBlockActors{
        std::make_shared<ExpectedBlockActorMap>()
    };
    std::vector<ProjectedBlockActor>         projectedBlockActors;
    // The structure owns the immutable NBT keys. Appearance does not depend
    // on placement or visible layers and is released with this state.
    std::unordered_map<CompoundTag const*, BannerAppearance> bannerAppearances;
};

} // namespace lholo::projection::detail
