#include "projection/mesh/ProjectionMeshResidency.h"
#include "projection/core/ProjectionState.h"
#include "projection/core/ProjectionResidencyBudget.h"

#include <algorithm>
#include <vector>

namespace lholo::projection::detail {
namespace {
float distanceSquared(ProjectionState const& file, std::size_t index, Vec3 const& camera) {
    auto const& center = file.sections[index].center;
    auto x = center.x + static_cast<float>(file.anchor.x + file.cachedOffsetX) - camera.x;
    auto y = center.y + static_cast<float>(file.anchor.y + file.cachedOffsetY) - camera.y;
    auto z = center.z + static_cast<float>(file.anchor.z + file.cachedOffsetZ) - camera.z;
    return x*x + y*y + z*z;
}
}
bool canBuildResidentSection(ProjectionState const& file, std::size_t section, Vec3 const& camera) {
    return !file.sections[section].residencyDeferred && (!file.residencyDistanceLimit ||
        distanceSquared(file, section, camera) <= *file.residencyDistanceLimit);
}
void recordProjectionResidentBytes(ProjectionState& state, std::size_t index) {
    auto& section = state.sections[index];
    std::size_t bytes{};
    auto count = [&](auto const& mesh) {
        if (!mesh) return;
        // Include retained CPU attributes/raw data and a conservative GPU
        // allowance even when upload has released the source vertex arrays.
        auto vertices = mesh->mVertexCount.get().value_or(0);
        auto cpu = mesh->mMeshData.get().size() + mesh->mRawData.get().capacity();
        bytes += std::max<std::size_t>(cpu, static_cast<std::size_t>(vertices) * 64) * 2;
    };
    for (auto const& mesh : section.meshes) count(mesh);
    count(state.warningFillSectionMeshes[index]);
    count(state.correctionOutlineSectionMeshes[index]);
    count(state.wrongFillSectionMeshes[index]);
    count(state.wrongOutlineSectionMeshes[index]);
    count(state.blockEntityPlaceholderSectionMeshes[index]);
    if (index < state.transparentSections.size()) if (auto const& source = state.transparentSections[index])
        bytes += (source->terrain.quads.capacity() + source->liquid.quads.capacity())
            * sizeof(ProjectionTransparentQuad);
    section.residentBytes = bytes;
}

void trimProjectionMeshResidency(ProjectionState& root, Vec3 const& camera) {
    bool const independent = root.structure && root.structure->independentFiles;
    auto const fileCount = independent ? root.files.size() : std::size_t{1};
    auto visitFiles = [&](auto visit) {
        if (independent) for (auto const& file : root.files) visit(file.get());
        else visit(&root);
    };
    auto squared = [](Vec3 const& p) { return p.x * p.x + p.y * p.y + p.z * p.z; };
    bool changed = root.residencyFileCount != fileCount;
    root.residencyFileCount = fileCount;
    visitFiles([&](auto* file) {
        if (!file->hasResidencyCamera || squared(camera - file->residencyCamera) >= 64.0f ||
            file->residencyTrimRevision != file->drawMeshRevision) changed = true;
    });
    if (!changed) return;
    std::vector<ProjectionResident> residents;
    std::vector<std::pair<ProjectionState*, std::size_t>> owners;
    std::size_t total{};
    visitFiles([&](auto* file) {
        // Retry deferred sections after moving one half-section. A stationary
        // camera must not rebuild/evict the same distant meshes every frame.
        auto delta = camera - file->residencyCamera;
        bool retry = !file->hasResidencyCamera || squared(delta) >= 64.0f;
        if (retry) {
            file->residencyCamera = camera; file->hasResidencyCamera = true;
            file->residencyDistanceLimit.reset();
        }
        for (std::size_t i = 0; i < file->sections.size(); ++i) {
            auto& section = file->sections[i];
            if (retry) section.residencyDeferred = false;
            if (!section.residentBytes) continue;
            total += section.residentBytes;
            residents.push_back({owners.size(), section.residentBytes, distanceSquared(*file,i,camera)});
            owners.emplace_back(file, i);
        }
    });
    auto evicted = selectProjectionEvictions(residents, ProjectionGeometryBudget);
    for (auto id : evicted) {
        auto [file, index] = owners[id];
        auto& section = file->sections[index];
        for (auto& mesh : section.meshes) mesh.reset();
        file->warningFillSectionMeshes[index].reset();
        file->correctionOutlineSectionMeshes[index].reset();
        file->wrongFillSectionMeshes[index].reset();
        file->wrongOutlineSectionMeshes[index].reset();
        file->blockEntityPlaceholderSectionMeshes[index].reset();
        if (index < file->transparentSections.size()) file->transparentSections[index].reset();
        file->transparentCache = {};
        section.residentBytes = 0;
        section.residencyDeferred = true;
        section.dirty = section.modelDirty = true;
        section.uploadedRevision = 0;
        ++section.requestedRevision;
        ++file->drawMeshRevision;
    }
    if (!evicted.empty()) {
        // Once full, do not spend the worker building more distant sections
        // only to throw them away. A camera move reopens admission above.
        float farthest{};
        for (auto const& resident : residents) {
            auto [file,index] = owners[resident.id];
            if (file->sections[index].residentBytes) farthest = std::max(farthest,resident.distanceSquared);
        }
        visitFiles([&](auto* file) { file->residencyDistanceLimit = farthest; });
    } else if (total < ProjectionGeometryBudget * 3 / 4) {
        visitFiles([&](auto* file) { if (file->residencyDistanceLimit) {
            file->residencyDistanceLimit.reset();
            for (auto& section : file->sections) section.residencyDeferred = false;
        }});
    }
    visitFiles([](auto* file) { file->residencyTrimRevision = file->drawMeshRevision; });
}
} // namespace lholo::projection::detail
