#pragma once
#include <memory>
#include <span>
#include "projection/runtime/ProjectionWorldEvents.h"
class BaseActorRenderContext;
namespace lholo::structure {
struct LoadedStructure;
}
namespace lholo::projection::detail {
struct ProjectionState;
bool updateIndependentFiles(ProjectionState&, BaseActorRenderContext&,
                            std::shared_ptr<structure::LoadedStructure const> const&);
void publishIndependentFileViews(ProjectionState &, BaseActorRenderContext &);
void invalidateIndependentFileNeighbors(ProjectionState &);
void distributeIndependentFileEvents(ProjectionState&, std::span<CorrectionEventBatch const>, std::span<SubChunkKey const>);
ProjectionState* selectedFileProjection(ProjectionState&);
} // namespace lholo::projection::detail
