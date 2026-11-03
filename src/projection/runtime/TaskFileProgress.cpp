#include "projection/core/ProjectionState.h"
namespace lholo::projection::detail {
bool ProjectionState::refreshTaskCellVisibility(std::size_t index) {
    auto visible = worldLookup->owns(index);
    auto previous = correctionStates.visible(index);
    if (previous == visible)
        return visible;
    auto raw = correctionStates.get(index);
    if (visible)
        addTaskCellProgress(fileProgress, raw);
    else
        removeTaskCellProgress(fileProgress, raw);
    correctionStates.setVisible(index, visible);
    ++progressRevision;
    return visible;
}
} // namespace lholo::projection::detail
