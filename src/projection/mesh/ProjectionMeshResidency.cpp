#include "projection/mesh/ProjectionMeshResidency.h"
#include "projection/core/ProjectionState.h"

#include <algorithm>

namespace lholo::projection::detail {
namespace {
bool hasResidentGeometry(ProjectionState const& file, std::size_t index) {
    auto const& section = file.sections[index];
    if (std::any_of(section.meshes.begin(), section.meshes.end(), [](auto const& mesh) { return bool(mesh); })
        || file.warningFillSectionMeshes[index] || file.correctionOutlineSectionMeshes[index]
        || file.wrongFillSectionMeshes[index] || file.wrongOutlineSectionMeshes[index]
        || file.blockEntityPlaceholderSectionMeshes[index]) return true;
    if (index < file.transparentSections.size()) {
        auto const& source = file.transparentSections[index];
        return source && (!source->terrain.quads.empty() || !source->liquid.quads.empty());
    }
    return false;
}
}

void trimProjectionMeshResidency(ProjectionState& root) {
    bool const independent = root.structure && root.structure->independentFiles;
    auto const count = independent ? root.files.size() : std::size_t{1};
    if (!count) return;
    std::size_t probes{}, released{};
    // Advance the file cursor before consuming its scan. A large file can
    // exhaust this frame's allowance without starving the following files.
    for (std::size_t visited = 0; visited < std::min(count, ProjectionResidencyProbesPerFrame)
         && probes < ProjectionResidencyProbesPerFrame && released < ProjectionResidencyReleasesPerFrame; ++visited) {
        root.residencyFileCursor %= count;
        auto* file = independent ? root.files[root.residencyFileCursor].get() : &root;
        root.residencyFileCursor = (root.residencyFileCursor + 1) % count;
        while (file->residencyScan.pending() && probes < ProjectionResidencyProbesPerFrame
               && released < ProjectionResidencyReleasesPerFrame) {
            auto const index = file->residencyScan.next();
            ++probes;
            auto& section = file->sections[index];
            if (section.withinRetentionDistance || !hasResidentGeometry(*file, index)) continue;
            for (auto& mesh : section.meshes) mesh.reset();
            file->warningFillSectionMeshes[index].reset();
            file->correctionOutlineSectionMeshes[index].reset();
            file->wrongFillSectionMeshes[index].reset();
            file->wrongOutlineSectionMeshes[index].reset();
            file->blockEntityPlaceholderSectionMeshes[index].reset();
            if (index < file->transparentSections.size()) file->transparentSections[index].reset();
            // An off-screen eviction must not rebuild an unrelated visible
            // transparent submission. Clear only if it still retains this page.
            if (std::binary_search(file->transparentCache.sections.begin(), file->transparentCache.sections.end(), index))
                file->transparentCache = {};
            section.meshBounds = {};
            section.dirty = section.modelDirty = true;
            section.uploadedRevision = 0;
            ++section.requestedRevision;
            ++file->drawMeshRevision;
            ++released;
        }
    }
}
} // namespace lholo::projection::detail
