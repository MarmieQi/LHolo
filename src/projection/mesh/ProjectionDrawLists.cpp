#include "projection/mesh/ProjectionDrawLists.h"
#include "projection/core/ProjectionState.h"

namespace lholo::projection::detail {

void prepareProjectionDrawLists(ProjectionState& state, Vec3 const& camera, BlockPos const& origin) {
    static_assert(static_cast<unsigned>(RenderBucket::Count) == 4);
    static_assert(static_cast<unsigned>(RenderBucket::Opaque) ==
                  static_cast<unsigned>(ProjectionDrawSlot::Opaque));
    static_assert(static_cast<unsigned>(RenderBucket::Alpha) ==
                  static_cast<unsigned>(ProjectionDrawSlot::Alpha));
    static_assert(static_cast<unsigned>(RenderBucket::AlphaOneSided) ==
                  static_cast<unsigned>(ProjectionDrawSlot::AlphaOneSided));
    static_assert(static_cast<unsigned>(RenderBucket::Blend) ==
                  static_cast<unsigned>(ProjectionDrawSlot::Blend));
    state.drawCache.prepare(
        state.activationGeneration, state.drawMeshRevision, state.visibilityRevision, state.sections.size(),
        {camera.x - static_cast<float>(origin.x), camera.y - static_cast<float>(origin.y),
         camera.z - static_cast<float>(origin.z)},
        [&](std::size_t index) {
            auto const& section = state.sections[index];
            ProjectionDrawRecord record{index, {section.center.x, section.center.y, section.center.z}, 0};
            for (std::size_t bucket = 0; bucket < section.meshes.size(); ++bucket) {
                if (section.meshes[bucket])
                    record.slots |= static_cast<std::uint16_t>(1u << bucket);
            }
            // Store presence rather than validity. Engine buffer readiness can
            // change later; submission still checks isValid() every time.
            auto add = [&](auto const& meshes, ProjectionDrawSlot slot) {
                if (index < meshes.size() && meshes[index])
                    record.slots |= projectionDrawBit(slot);
            };
            if (index < state.transparentSections.size() && state.transparentSections[index]) {
                auto const& geometry = *state.transparentSections[index];
                if (!geometry.terrain.quads.empty()) record.slots |= projectionDrawBit(ProjectionDrawSlot::Blend);
                if (!geometry.liquid.quads.empty()) record.slots |= projectionDrawBit(ProjectionDrawSlot::Liquid);
            }
            add(state.blockEntityPlaceholderSectionMeshes, ProjectionDrawSlot::Placeholder);
            add(state.warningFillSectionMeshes, ProjectionDrawSlot::MissingFill);
            add(state.correctionOutlineSectionMeshes, ProjectionDrawSlot::MissingOutline);
            add(state.wrongFillSectionMeshes, ProjectionDrawSlot::WrongFill);
            add(state.wrongOutlineSectionMeshes, ProjectionDrawSlot::WrongOutline);
            return record;
        },
        [&](std::size_t index) { return state.sections[index].renderVisible; });
}

} // namespace lholo::projection::detail
