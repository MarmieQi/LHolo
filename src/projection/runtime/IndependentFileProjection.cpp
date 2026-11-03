#include "projection/runtime/IndependentFileProjection.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "projection/core/ProjectionState.h"
#include "projection/core/ProjectionWorldBounds.h"
#include "projection/runtime/ProjectionLifecycle.h"
#include "projection/world/ProjectionPlacement.h"
#include "projection/world/ProjectionTaskBlockActors.h"
#include "structure/IndependentTaskFiles.h"
#include "structure/StructureGeneration.h"

namespace lholo::projection::detail {
namespace {
ProjectionWorldBounds fileBounds(ProjectionState const& file) {
    auto const& t = *file.fileTransform;
    std::array<long long, 3> origin{static_cast<long long>(file.anchor.x) + t.offsetX,
                                    static_cast<long long>(file.anchor.y) + t.offsetY,
                                    static_cast<long long>(file.anchor.z) + t.offsetZ};
    bool rotated = (t.rotation & 1) != 0;
    std::array<int, 3> size{rotated ? file.structure->sizeZ : file.structure->sizeX, file.structure->sizeY,
                            rotated ? file.structure->sizeX : file.structure->sizeZ};
    return {origin, {origin[0] + size[0], origin[1] + size[1], origin[2] + size[2]}};
}
} // namespace
bool updateIndependentFiles(ProjectionState& root, BaseActorRenderContext& context,
                            std::shared_ptr<structure::LoadedStructure const> const& loaded) {
    if (root.structure == loaded && !root.files.empty())
        return true;
    std::vector<std::shared_ptr<ProjectionState>> files;
    for (auto const& f : loaded->independentFiles->files) {
        if (!f.visible || !f.loaded)
            continue;
        std::shared_ptr<ProjectionState> child;
        for (auto const& old : root.files)
            if (old->fileId == f.id && old->structure == f.loaded) {
                child = old;
                break;
            }
        if (!child) {
            child = std::make_shared<ProjectionState>();
            if (!prepareProjectionState(*child, context, f.loaded))
                return false;
            child->fileId = f.id;
            child->anchor = root.anchor;
            child->enabled = true;
            // Two task entries may reference the same raw file. Worker result
            // identity belongs to the runtime instance, not the raw data.
            child->structureGeneration = structure::nextStructureGeneration();
            child->meshWorkerGeneration = root.meshWorkerGeneration;
            child->asyncMeshBuildingEnabled = root.asyncMeshBuildingEnabled;
        }
        child->fileTransform = structure::detail::StructureTransformSnapshot{
            f.rotation, f.mirror, f.x, f.y, f.z, f.slice.mode, f.slice.layer, f.slice.axis};
        files.push_back(std::move(child));
    }
    root.files = std::move(files);
    root.structure = loaded;
    root.structureGeneration = loaded->generation;
    return true;
}
void publishIndependentFileViews(ProjectionState &root, BaseActorRenderContext &context) {
    auto definitions = root.structure->independentFiles->files;
    for (auto const &file : root.files) {
        auto it =
            std::find_if(definitions.begin(), definitions.end(), [&](auto const &f) { return f.id == file->fileId; });
        auto const &t = *file->fileTransform;
        it->x = root.anchor.x + t.offsetX;
        it->y = root.anchor.y + t.offsetY;
        it->z = root.anchor.z + t.offsetZ;
        it->rotation = t.rotation;
        it->mirror = t.mirror;
        it->slice = {*file->cachedLayerAxis, *file->cachedLayerDisplayMode, file->cachedDisplayLayer};
    }
    if (root.taskWorld && root.taskWorld->placements->matches(definitions))
        return;
    auto placements = std::make_shared<structure::TaskProjectionView const>(definitions);
    auto next = std::make_shared<TaskWorldLookups>();
    next->placements = placements;
    next->files.resize(definitions.size());
    next->actors.resize(definitions.size());
    std::vector<std::weak_ptr<ProjectionState>> runtimes(definitions.size());
    for (auto const &file : root.files) {
        auto index = *placements->fileIndex(file->fileId);
        next->files[index] = file->localWorldLookup;
        next->actors[index] = file->expectedWorldBlockActors;
        runtimes[index] = file;
    }
    auto createdActors = refreshTaskBlockActors(root, context, *next);
    for (auto const &file : root.files) {
        auto index = *placements->fileIndex(file->fileId);
        file->worldLookup = file->localWorldLookup->withTask(next, index);
        file->taskRuntimeFiles = runtimes;
        auto changed = placements->changedBounds(index, root.taskWorld ? root.taskWorld->placements.get() : nullptr);
        if (changed.empty())
            continue;
        ++file->progressRevision;
        auto previous = root.taskWorld ? root.taskWorld->placements->fileIndex(file->fileId) : std::nullopt;
        bool ownWorldChanged = !previous;
        if (previous) {
            auto const &old = root.taskWorld->placements->files()[*previous];
            auto const &current = definitions[index];
            ownWorldChanged = old.loaded != current.loaded || old.x != current.x || old.y != current.y ||
                              old.z != current.z || old.rotation != current.rotation || old.mirror != current.mirror;
        }
        std::vector<std::size_t> affected;
        for (auto const &world : changed) {
            auto local = placements->localIntersection(index, world);
            if (local.empty())
                continue;
            visitTaskSections(local, file->localSectionKeys, file->localSectionIndices,
                              [&](std::size_t section) { affected.push_back(section); });
            // Its own move already restarts that file's air scan. Unchanged
            // neighbors revisit only old/new interactions through the work lane.
            if (!ownWorldChanged)
                file->taskAirRefresh.request(world);
        }
        std::sort(affected.begin(), affected.end());
        affected.erase(std::unique(affected.begin(), affected.end()), affected.end());
        for (auto section : affected) {
            auto &value = file->sections[section];
            value.dirty = value.modelDirty = value.incrementalDirty = value.interactiveDirty = true;
            value.biomeRefresh = {};
            ++value.requestedRevision;
            if (!file->sectionBlockIndices[section].empty() && !ownWorldChanged)
                file->taskSectionRefresh.request(section);
        }
    }
    // Pair only fresh objects. Unchanged actors may still be read by old
    // workers and must never acquire pointers into this new version.
    if (!createdActors.empty()) {
        auto &region = context.mClientInstance.getLocalPlayer()->getDimensionBlockSource();
        for (auto const &file : root.files)
            pairProjectedChests(region, *file, &createdActors);
    }
    root.taskWorld = std::move(next);
}

void invalidateIndependentFileNeighbors(ProjectionState &root) {
    constexpr int neighbors[6][3]{{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    for (auto const &source : root.files) {
        if (source->taskNeighborChanges.empty())
            continue;
        std::sort(source->taskNeighborChanges.begin(), source->taskNeighborChanges.end());
        auto end = std::unique(source->taskNeighborChanges.begin(), source->taskNeighborChanges.end());
        auto index = source->worldLookup->fileIndex();
        for (auto foreign : root.taskWorld->placements->neighbors(index)) {
            if (foreign == index)
                continue;
            auto target = source->taskRuntimeFiles[foreign].lock();
            assert(target);
            std::vector<std::size_t> sections;
            for (auto it = source->taskNeighborChanges.begin(); it != end; ++it) {
                auto [x, y, z] = *it;
                for (auto const &d : neighbors) {
                    auto local = root.taskWorld->placements->localCell(foreign, {x + d[0], y + d[1], z + d[2]});
                    if (!local)
                        continue;
                    auto found =
                        target->localSectionIndices.find({(*local)[0] / 16, (*local)[1] / 16, (*local)[2] / 16});
                    if (found != target->localSectionIndices.end())
                        sections.push_back(found->second);
                }
            }
            std::sort(sections.begin(), sections.end());
            sections.erase(std::unique(sections.begin(), sections.end()), sections.end());
            for (auto section : sections) {
                auto &value = target->sections[section];
                value.dirty = value.modelDirty = value.incrementalDirty = true;
                ++value.requestedRevision;
            }
        }
        source->taskNeighborChanges.clear();
    }
}

void distributeIndependentFileEvents(ProjectionState& root,
    std::span<CorrectionEventBatch const> changes, std::span<SubChunkKey const> chunks) {
    for (auto& file : root.files) {
        // Route only relevant facts; don't scan unrelated world subchunks in
        // every file's original correction tracker.
        auto bounds = fileBounds(*file).withNeighborMargin();
        for (auto const& change : changes)
            file->correctionEvents.push(change.clipped(bounds.minimum, bounds.maximum));
        for (auto const& chunk : chunks) {
            auto [x, y, z] = chunk;
            std::array<long long, 3> lo{static_cast<long long>(x) * 16, static_cast<long long>(y) * 16,
                                        static_cast<long long>(z) * 16};
            std::array<long long, 3> hi{lo[0] + 15, lo[1] + 15, lo[2] + 15};
            if (bounds.intersects(lo, hi))
                file->correctionSubChunks.push({x, y, z});
        }
    }
}
ProjectionState* selectedFileProjection(ProjectionState& root) {
    if (!root.structure || !root.structure->independentFiles)
        return &root;
    auto& session = structure::detail::StructureSession::getInstance();
    auto source = session.selectedSourceFor(*root.structure);
    if (source < 0 || static_cast<std::size_t>(source) >= root.structure->sourceFiles.size())
        return nullptr;
    auto const& id = root.structure->sourceFiles[source];
    for (auto& file : root.files)
        if (file->fileId == id)
            return file.get();
    return nullptr;
}
} // namespace lholo::projection::detail
