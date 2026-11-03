#include "projection/runtime/ProjectionVisibility.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/deps/renderer/Camera.h"
#include "projection/core/ProjectionState.h"
#include <chrono>

namespace lholo::projection::detail {
void updateProjectionVisibility(ProjectionState& state, BaseActorRenderContext& context, Vec3 const& camera, float renderDistance) {
    struct MeasureVisibility {
        std::uint64_t& output;
        std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        ~MeasureVisibility() {
            output = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                                    std::chrono::steady_clock::now() - started)
                                                    .count());
        }
    } measure{state.drawTimings.lastVisibilityMicros};
    ProjectionViewKey key;
    glm::mat4 viewProjection{1};
    auto const& cameraObject = context.mScreenContext.camera;
    if (!cameraObject.viewMatrixStack->stack->empty() &&
        !cameraObject.projectionMatrixStack->stack->empty()) {
        key.hasMatrix = true;
        viewProjection = cameraObject.getProjectionMatrix() * *cameraObject.viewMatrixStack->top()._m;
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row) {
                key.matrix[column * 4 + row] = viewProjection[column][row];
            }
    }
    Vec3 const origin{static_cast<float>(state.anchor.x + state.cachedOffsetX) - camera.x,
                      static_cast<float>(state.anchor.y + state.cachedOffsetY) - camera.y,
                      static_cast<float>(state.anchor.z + state.cachedOffsetZ) - camera.z};
    key.originX = origin.x;
    key.originY = origin.y;
    key.originZ = origin.z;
    key.renderDistance = renderDistance;
    key.activation = state.activationGeneration;
    key.meshRevision = state.drawMeshRevision;
    key.sections = state.sections.size();
    if (state.visibilityKey && *state.visibilityKey == key)
        return;
    if (!state.visibilityKey || projectionResidencyViewChanged(*state.visibilityKey, key))
        state.residencyScan.request(state.sections.size());
    state.visibilityKey = key;
    auto const frustum =
        key.hasMatrix ? ProjectionFrustum::fromViewProjection(viewProjection) : ProjectionFrustum{};
    state.visibleSectionCount = 0;
    ++state.visibilityRevision;
    for (auto& section : state.sections) {
        // Actual vertex bounds include liquid and correction geometry. Before
        // geometry exists, build priority uses transformed source-cell bounds.
        auto const visibility = classifyProjectionSection(frustum, section.buildBounds, section.meshBounds, origin, renderDistance);
        section.renderVisible = visibility.renderVisible;
        section.buildVisible = visibility.buildVisible;
        section.prefetchVisible = visibility.prefetchVisible;
        section.withinBuildDistance = visibility.withinBuildDistance;
        section.withinRetentionDistance = visibility.withinRetentionDistance;
        if (section.renderVisible)
            ++state.visibleSectionCount;
    }
}
} // namespace lholo::projection::detail
