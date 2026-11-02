#include "projection/runtime/IndependentFileProjection.h"
#include "projection/core/ProjectionState.h"
#include "projection/runtime/ProjectionLifecycle.h"
#include "structure/IndependentTaskFiles.h"
#include "structure/StructureGeneration.h"
#include "projection/core/ProjectionWorldBounds.h"

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
void distributeIndependentFileEvents(ProjectionState& root,
    std::span<CorrectionEventBatch const> changes, std::span<SubChunkKey const> chunks) {
    for (auto& file : root.files) {
        // Route only relevant facts; don't scan unrelated world subchunks in
        // every file's original correction tracker.
        auto bounds = fileBounds(*file);
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
