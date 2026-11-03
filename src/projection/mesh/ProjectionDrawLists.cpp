#include "projection/mesh/ProjectionDrawLists.h"
#include "ll/api/mod/NativeMod.h"
#include "plugin/LHolo.h"
#include "projection/core/ProjectionState.h"

#include <algorithm>
#include <chrono>

namespace lholo::projection::detail {

std::uint64_t prepareProjectionDrawLists(ProjectionState& state, Vec3 const& camera, BlockPos const& origin) {
    auto const started = std::chrono::steady_clock::now();
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
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started)
            .count());
}

void recordProjectionDrawPass(ProjectionState& state, bool alpha, std::uint64_t totalMicros,
                              std::uint64_t prepareMicros, std::size_t draws) {
    auto& timings = state.drawTimings;
    ProjectionDrawTimings::Sample sample{
        prepareMicros, totalMicros >= prepareMicros ? totalMicros - prepareMicros : 0, draws};
    if (!alpha) {
        sample.visibility = timings.lastVisibilityMicros;
        timings.opaque = sample;
        return;
    }
    sample.prepare += timings.opaque.prepare;
    sample.submit += timings.opaque.submit;
    sample.draws += timings.opaque.draws;
    sample.visibility = timings.opaque.visibility;
    timings.opaque = {};
    timings.samples[timings.frames++ % timings.samples.size()] = sample;
    if (timings.frames % timings.samples.size())
        return;

    std::array<std::uint64_t, 120> prepare{}, submit{}, visibility{};
    std::uint64_t prepareSum{}, submitSum{}, visibilitySum{}, drawSum{}, maxDraws{};
    for (std::size_t index = 0; index < timings.samples.size(); ++index) {
        auto const& item = timings.samples[index];
        prepare[index] = item.prepare;
        submit[index] = item.submit;
        visibility[index] = item.visibility;
        prepareSum += item.prepare;
        submitSum += item.submit;
        visibilitySum += item.visibility;
        drawSum += item.draws;
        maxDraws = std::max(maxDraws, static_cast<std::uint64_t>(item.draws));
    }
    std::sort(prepare.begin(), prepare.end());
    std::sort(submit.begin(), submit.end());
    std::sort(visibility.begin(), visibility.end());
    LHolo::getInstance().getSelf().getLogger().debug(
        "Projection draw CPU (120 alpha passes): prepare avg/p95/max {}/{}/{} us; submit {}/{}/{} us; draws "
        "avg/max {}/{}; visibility CPU {}/{}/{} us; visible {}/{}, resident {}; order rebuilds {}",
        prepareSum / 120, prepare[113], prepare.back(), submitSum / 120, submit[113], submit.back(),
        drawSum / 120, maxDraws, visibilitySum / 120, visibility[113], visibility.back(),
        state.visibleSectionCount, state.sections.size(), state.drawCache.residentCount(),
        state.drawCache.orderBuilds - timings.lastOrderBuilds);
    timings.lastOrderBuilds = state.drawCache.orderBuilds;
}

} // namespace lholo::projection::detail
