#include "projection/world/ProjectionTaskBlockActors.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "mc/world/level/levelgen/structure/LegacyStructureSettings.h"
#include "projection/core/ProjectionState.h"
#include "projection/world/ProjectionPlacement.h"

namespace lholo::projection::detail {
std::unordered_set<BlockActor const *> refreshTaskBlockActors(ProjectionState &root, BaseActorRenderContext &context,
                                                              TaskWorldLookups &next) {
    std::unordered_set<BlockActor const *> replace;
    if (auto const &old = root.taskWorld) {
        for (std::size_t i = 0; i < old->actors.size(); ++i) {
            if (!old->actors[i])
                continue;
            auto current = next.placements->fileIndex(old->placements->files()[i].id);
            bool ownChanged = !current || !structure::sameTaskPlacement(old->placements->files()[i],
                                                                        next.placements->files()[*current]);
            auto boxes = current ? next.placements->changedBounds(*current, old->placements.get())
                                 : std::vector<structure::TaskCellBounds>{};
            for (auto const &[key, actor] : *old->actors[i]) {
                auto [x, y, z] = key;
                bool changed = ownChanged || std::any_of(boxes.begin(), boxes.end(),
                                                         [&](auto const &b) { return b.contains({x, y, z}); });
                if (!changed)
                    continue;
                replace.insert(actor.get());
                // A native double chest owns a raw pointer to its partner.
                // Replace both endpoints, leaving old worker objects untouched.
                if (actor->mType == BlockActorType::Chest) {
                    auto *chest = static_cast<ChestBlockActor *>(actor.get());
                    if (chest->mLargeChestPaired)
                        replace.insert(chest->mLargeChestPaired);
                }
            }
        }
    }
    std::unordered_set<BlockActor const *> created;
    for (auto const &file : root.files) {
        auto index = *next.placements->fileIndex(file->fileId);
        std::vector<std::size_t> indices;
        for (auto const &[key, actor] : *file->expectedWorldBlockActors) {
            if (replace.contains(actor.get()))
                indices.push_back(*file->localWorldLookup->indexAt(key));
        }
        if (!indices.empty()) {
            auto const &t = *file->fileTransform;
            LegacyStructureSettings transform{getProjectionMirror(t.mirror), getProjectionRotation(t.rotation), nullptr,
                                              BoundingBox{}};
            rebuildProjectedBlockActors(*file, context.mBlockEntityRenderDispatcher, transform,
                                        {.mirrorMode = t.mirror,
                                         .rotationTurns = t.rotation,
                                         .offsetX = t.offsetX,
                                         .offsetY = t.offsetY,
                                         .offsetZ = t.offsetZ,
                                         .layerDisplayMode = t.layerDisplayMode,
                                         .displayLayer = t.displayLayer,
                                         .layerAxis = t.layerAxis,
                                         .identityTransform = t.mirror == 0 && t.rotation == 0},
                                        indices, false);
            for (auto cell : indices) {
                auto &section = file->sections[file->correctionStates.section(cell)];
                section.dirty = section.modelDirty = true;
                ++section.requestedRevision;
            }
        }
        next.actors[index] = file->expectedWorldBlockActors;
        auto oldIndex = root.taskWorld ? root.taskWorld->placements->fileIndex(file->fileId) : std::nullopt;
        for (auto const &[key, actor] : *file->expectedWorldBlockActors) {
            bool unchanged = false;
            if (oldIndex && root.taskWorld->actors[*oldIndex]) {
                auto const &previous = *root.taskWorld->actors[*oldIndex];
                auto it = previous.find(key);
                unchanged = it != previous.end() && it->second == actor;
            }
            if (!unchanged)
                created.insert(actor.get());
        }
    }
    return created;
}
} // namespace lholo::projection::detail
