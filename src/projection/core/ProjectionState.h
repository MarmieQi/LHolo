// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Projection runtime state and resource ownership. Lifecycle operations remain
// in Projection.cpp; this type only makes the ownership boundary explicit.

#pragma once

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/ProjectionTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
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
    bool    dirty{};
    bool    incrementalDirty{};
    bool    buildInFlight{};
    std::uint64_t requestedRevision{};
    std::uint64_t uploadedRevision{};
    std::array<std::unique_ptr<mce::Mesh>, static_cast<std::size_t>(RenderBucket::Count)> meshes;
};

struct ProjectionState {
    bool                            enabled{};
    BlockPos                        anchor{};
    IClientInstance*                client{};
    Level*                          level{};
    Dimension*                      dimension{};
    std::optional<mce::TexturePtr>  terrainTexture;
    std::optional<TextureVariant>   terrainTextureVariant;
    std::shared_ptr<structure::LoadedStructure const> structure;
    std::uint64_t                   structureGeneration{};
    std::unique_ptr<BlockTessellator> blockTessellator;
    std::vector<CorrectionState>    correctionStates;
    // One byte per structure block. This is updated by the existing bounded
    // correction scan, so the HUD never performs its own world-block queries.
    std::vector<uchar>              progressCorrect;
    // 0 = no placement error, 1 = wrong block type, 2 = wrong state/direction.
    // Updating one byte and two counters keeps the HUD O(1) per frame.
    std::vector<uchar>              progressErrorKind;
    std::vector<uchar>              blockActorRendererAvailable;
    std::uint64_t                   progressCorrectCount{};
    std::uint64_t                   progressVisibleCorrectCount{};
    std::uint64_t                   progressWrongTypeCount{};
    std::uint64_t                   progressWrongStateCount{};
    std::size_t                     correctionScanCursor{};
    std::set<SubChunkKey>           pendingLoadedSubChunks;
    std::vector<BrokenProjectionCell> pendingBrokenCells;
    int                             cachedRotation{-1};
    int                             cachedMirror{-1};
    int                             cachedOffsetX{};
    int                             cachedOffsetY{};
    int                             cachedOffsetZ{};
    int                             cachedLayerDisplayMode{-1};
    int                             cachedDisplayLayer{-1};
    int                             cachedLayerAxis{-1};
    float                           cachedOpacity{-1.0f};
    float                           cachedCorrectionFillOpacity{-1.0f};
    float                           cachedCorrectionOutlineOpacity{-1.0f};
    std::vector<std::vector<std::size_t>> sectionBlockIndices;
    // Correction meshes are split by category so the see-through (X-ray) option
    // can apply to the wrong-type/wrong-state markers only, never to the many
    // "missing" outlines. warningFill/correctionOutline hold the MISSING cells;
    // wrongFill/wrongOutline hold WrongType + WrongState.
    std::vector<std::unique_ptr<mce::Mesh>> warningFillSectionMeshes;
    std::vector<std::unique_ptr<mce::Mesh>> correctionOutlineSectionMeshes;
    std::vector<std::unique_ptr<mce::Mesh>> wrongFillSectionMeshes;
    std::vector<std::unique_ptr<mce::Mesh>> wrongOutlineSectionMeshes;
    std::vector<std::unique_ptr<mce::Mesh>> liquidProxySectionMeshes;
    std::vector<std::unique_ptr<mce::Mesh>> blockEntityPlaceholderSectionMeshes;
    std::unique_ptr<mce::Mesh>              structureBoundsMesh;
    std::vector<SectionState>               sections;
    std::vector<std::size_t>                blockToSection;
    std::size_t                             dirtySectionCursor{};
    std::uint64_t                           meshWorkerGeneration{};
    int                                     consecutiveMeshWorkerFailures{};
    bool                                    asyncMeshBuildingEnabled{true};
    std::uint64_t                           meshWorkerUploadedSections{};
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
    std::shared_ptr<ExpectedBlockMap>        expectedWorldBlocks{std::make_shared<ExpectedBlockMap>()};
    std::shared_ptr<ExpectedBlockActorMap>   expectedWorldBlockActors{
        std::make_shared<ExpectedBlockActorMap>()
    };
    std::vector<ProjectedBlockActor>         projectedBlockActors;
    std::shared_ptr<ExpectedBlockIndexMap>   expectedWorldBlockIndices{
        std::make_shared<ExpectedBlockIndexMap>()
    };
    bool                                    meshPreflightDone{};
};

} // namespace lholo::projection::detail
