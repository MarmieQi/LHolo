#pragma once
#include <unordered_set>

class BaseActorRenderContext;
class BlockActor;
namespace lholo::projection::detail {
struct ProjectionState;
struct TaskWorldLookups;
std::unordered_set<BlockActor const *> refreshTaskBlockActors(ProjectionState &, BaseActorRenderContext &,
                                                              TaskWorldLookups &);
} // namespace lholo::projection::detail
