#include "structure/write/local/LocalWorldWriter.h"
#include "structure/write/local/BedrockTemplateBuilder.h"
#include "structure/write/local/LocalWriteQueue.h"
#include "structure/write/StructureWritePlan.h"
#include "structure/write/StructureWriteIndices.h"
#include "structure/StructureFileLoader.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/BlockPalette.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/levelgen/structure/BoundingBox.h"
#include "mc/world/level/levelgen/structure/StructureSettings.h"
#include "mc/world/level/levelgen/structure/StructureTemplate.h"
#include <climits>

namespace lholo::structure::write {
namespace {
ll::event::ListenerPtr listener;
LocalWriteQueue queue;
WriteResult execute(StructureWritePlan const& plan, Level& level) {
    auto dimension = level.getDimension(DimensionType{plan.target.dimension}).lock();
    if (!dimension) return WriteResult::ContextChanged;
    auto& region = dimension->getBlockSourceFromMainChunkSource();
    auto const& p = plan.target.position;
    // Reject unsupported sizes/unloaded destinations before allocating dense
    // template layers, especially for sparse files with widely separated regions.
    writeVolume(plan.size);
    std::array<int, 3> expectedMaximum;
    if (!writeBounds(plan.target, plan.size, expectedMaximum)) return WriteResult::CoordinateLimit;
    BlockPos const minimum{p[0], p[1], p[2]};
    BlockPos const expectedMax{expectedMaximum[0], expectedMaximum[1], expectedMaximum[2]};
    if (minimum.y < region.getMinHeight() || expectedMax.y >= region.getMaxHeight()) return WriteResult::HeightLimit;
    if (!region.areChunksFullyLoaded(minimum, expectedMax)) return WriteResult::RegionNotLoaded;
    auto structure = buildBedrockTemplate(plan, level);
    if (!structure) return WriteResult::TemplateFailed;
    StructureSettings settings{};
    settings.mIgnoreBlocks = false;
    settings.mIgnoreEntities = true;
    settings.mPaletteName = std::string{"default"};
    settings.mStructureSize = BlockPos{plan.size[0], plan.size[1], plan.size[2]};
    settings.mStructureOffset = BlockPos{0, 0, 0};
    settings.mPivot = Vec3{0, 0, 0};
    settings.mRotation = static_cast<Rotation>(plan.target.rotation);
    // LHolo names the flipped coordinate; vanilla names the retained axis.
    settings.mMirror = plan.target.mirror == 1 ? Mirror::Z : plan.target.mirror == 2 ? Mirror::X : Mirror::None;
    settings.mAnimationMode = AnimationMode::None;
    settings.mIntegrityValue = 100.0f;
    settings.mIntegritySeed = 0;
    // Ask vanilla for its transformed bounds rather than assuming its pivot convention.
    auto const bounds = structure->getTransformedBounds(BlockPos{0, 0, 0}, settings);
    std::array<int, 3> const transformedMinimum{bounds.min.x, bounds.min.y, bounds.min.z};
    std::array<int, 3> const transformedMaximum{bounds.max.x, bounds.max.y, bounds.max.z};
    BlockPos placement, maximum;
    for (int axis = 0; axis < 3; ++axis) {
        auto const value = static_cast<long long>(p[axis]) - transformedMinimum[axis];
        if (value < INT_MIN || value > INT_MAX) return WriteResult::CoordinateLimit;
        placement[axis] = static_cast<int>(value);
        auto const end = value + transformedMaximum[axis];
        if (end < INT_MIN || end > INT_MAX) return WriteResult::CoordinateLimit;
        maximum[axis] = static_cast<int>(end);
    }
    if (minimum.y < region.getMinHeight() || maximum.y >= region.getMaxHeight()) return WriteResult::HeightLimit;
    if (!region.areChunksFullyLoaded(minimum, maximum)) return WriteResult::RegionNotLoaded;
    structure->placeInWorld(region, level.getBlockPalette(), placement, settings, nullptr, true);
    return WriteResult::Complete;
}
}
bool localWorldAvailable() { return queue.enabled() && ll::service::getServerInstance().has_value(); }
bool installLocalWorldWriter() {
    if (queue.enabled()) return true;
    listener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>(
        [](ll::event::ServerLevelTickEvent& event) {
            queue.run(detail::structureFileWorldRevision(),
                [&](StructureWritePlan const& plan) { return execute(plan, event.level()); });
        });
    if (listener) queue.enable();
    return listener != nullptr;
}
void cancelLocalWrite() { queue.cancel(); }
void uninstallLocalWorldWriter() {
    queue.disable();
    if (listener) ll::event::EventBus::getInstance().removeListener(listener);
    listener.reset();
}
bool queueLocalWrite(StructureWritePlan plan, std::uint64_t revision) {
    return localWorldAvailable() && queue.enqueue(std::move(plan), revision);
}
std::optional<WriteCompletion> takeLocalWriteResult() {
    return queue.take();
}
}
