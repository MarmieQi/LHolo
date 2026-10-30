#include "structure/capture/BedrockStructureCapture.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/levelgen/structure/StructureSettings.h"
#include "mc/world/level/levelgen/structure/StructureTemplate.h"

namespace lholo::structure::capture {
std::unique_ptr<StructureTemplate> captureBedrockStructure(
    BlockSource& region, BlockPos const& minimum, BlockPos const& size, bool includeEntities) {
    // The SDK's create(region, bounds) helper default-initializes settings and
    // assigns only four fields. Never pass indeterminate flags/enums to native
    // capture. Use the region's Level so client/remote capture keeps working.
    StructureSettings settings{};
    settings.mPaletteName = std::string{"default"};
    settings.mIgnoreEntities = !includeEntities;
    settings.mReloadActorEquipment = false;
    settings.mIgnoreBlocks = false;
    settings.mIsWaterLogged = false;
    settings.mIgnoreJigsawBlocks = false;
    settings.mAllowNonTickingPlayerAndTickingAreaChunks = false;
    settings.mLastTouchedByPlayer = ActorUniqueID::INVALID_ID();
    settings.mStructureSize = size;
    settings.mStructureOffset = BlockPos{0, 0, 0};
    settings.mPivot = Vec3{0, 0, 0};
    settings.mMirror = Mirror::None;
    settings.mRotation = Rotation::None;
    settings.mAnimationMode = AnimationMode::None;
    settings.mAnimationSeconds = 0.0f;
    settings.mIntegrityValue = 100.0f;
    settings.mIntegritySeed = 0;
    auto structure = std::make_unique<StructureTemplate>(
        "lholo:client_export", region.getLevel().getUnknownBlockTypeRegistry());
    structure->fillFromWorld(region, minimum, settings);
    return structure;
}
}
