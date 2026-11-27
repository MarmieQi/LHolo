#include "structure/write/local/LocalWorldWriter.h"
#include "structure/write/local/BedrockTemplateBuilder.h"
#include "structure/write/local/LocalWriteQueue.h"
#include "structure/write/StructureWritePlan.h"
#include "structure/StructureFileLoader.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPalette.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/chunk/ChunkState.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/levelgen/structure/BoundingBox.h"
#include "mc/world/level/levelgen/structure/StructureSettings.h"
#include "mc/world/level/levelgen/structure/StructureTemplate.h"
#include <climits>

namespace lholo::structure::write {
namespace {

ll::event::ListenerPtr listener;
LocalWriteQueue queue;

bool isChunkWritable(
    BlockSource& region,
    int cx,
    int cz,
    std::array<int, 3> const& worldMin,
    std::array<int, 3> const& worldMax
) {
    auto const* chunk = region.getChunk(ChunkPos{cx, cz});
    if (!chunk || chunk->mReadOnly || chunk->mLoadState->load() != ChunkState::Loaded) return false;
    BlockPos const checkMin{worldMin[0] - 1, worldMin[1], worldMin[2] - 1};
    BlockPos const checkMax{worldMax[0] + 1, worldMax[1], worldMax[2] + 1};
    return region.areChunksFullyLoaded(checkMin, checkMax);
}

WriteResult placeChunkTemplate(StructureWritePlan const& plan, Level& level, BlockSource& region) {
    auto const& p = plan.target.position;
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
    auto const bounds = structure->getTransformedBounds(BlockPos{0, 0, 0}, settings);
    std::array<int, 3> const transformedMinimum{bounds.min.x, bounds.min.y, bounds.min.z};
    BlockPos placement;
    for (int axis = 0; axis < 3; ++axis) {
        auto const value = static_cast<long long>(p[axis]) - transformedMinimum[axis];
        if (value < INT_MIN || value > INT_MAX) return WriteResult::CoordinateLimit;
        placement[axis] = static_cast<int>(value);
    }
    structure->placeInWorld(region, level.getBlockPalette(), placement, settings, nullptr, true);
    return WriteResult::Complete;
}

void tickLocalWrite(Level& level) {
    std::shared_ptr<Dimension> dimension;
    BlockSource* region{};
    queue.runTick(
        detail::structureFileWorldRevision(),
        [&](int targetDimension, std::array<int, 2> const& yBounds) -> LocalEnvironment {
            auto* player = level.getPrimaryLocalPlayer();
            if (player && static_cast<int>(player->getDimensionId()) != targetDimension) {
                return {LocalEnvironment::State::WaitingDimension, std::nullopt};
            }
            dimension = level.getDimension(DimensionType{targetDimension}).lock();
            if (!dimension) return {LocalEnvironment::State::ContextChanged, std::nullopt};
            region = &dimension->getBlockSourceFromMainChunkSource();
            if (yBounds[0] < region->getMinHeight() || yBounds[1] >= region->getMaxHeight()) {
                return {LocalEnvironment::State::HeightLimit, std::nullopt};
            }
            std::optional<std::array<int, 3>> feet;
            if (player) {
                auto const pos = player->getFeetBlockPos();
                feet = {pos.x, pos.y, pos.z};
            }
            return {LocalEnvironment::State::Ready, feet};
        },
        [&](int cx, int cz, std::array<int, 3> const& worldMin, std::array<int, 3> const& worldMax) {
            return isChunkWritable(*region, cx, cz, worldMin, worldMax);
        },
        [&](StructureWritePlan const& subPlan) {
            return placeChunkTemplate(subPlan, level, *region);
        }
    );
}

} // namespace

bool localWorldAvailable() { return queue.enabled() && ll::service::getServerInstance().has_value(); }

bool installLocalWorldWriter() {
    if (queue.enabled()) return true;
    listener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>(
        [](ll::event::ServerLevelTickEvent& event) { tickLocalWrite(event.level()); });
    if (listener) queue.enable();
    return listener != nullptr;
}

void cancelLocalWrite() { queue.cancel(); }

void uninstallLocalWorldWriter() {
    queue.disable();
    if (listener) ll::event::EventBus::getInstance().removeListener(listener);
    listener.reset();
}

bool startLocalWrite(StructureWritePlan plan, std::uint64_t revision) {
    return localWorldAvailable() && queue.start(std::move(plan), revision);
}

WriteProgress localWriteStatus() { return queue.status(); }

void resumeLocalWrite() { queue.resume(); }

std::optional<WriteCompletion> takeLocalWriteResult() { return queue.take(); }

} // namespace lholo::structure::write
