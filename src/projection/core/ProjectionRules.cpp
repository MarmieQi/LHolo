// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "projection/core/ProjectionRules.h"
#include "projection/correction/StairShapeComparison.h"

#include <array>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/deps/nbt/Tag.h"
#include "mc/world/Facing.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/NeighborBlockDirections.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/NeighborDirection.h"
#include "mc/world/level/block/VanillaStates.h"
#include "mc/world/level/block/WallBlock.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/components/BlockConnectionComponent.h"
#include "mc/world/level/block/states/VanillaBlockStateTransformUtils.h"
#include "mc/world/level/block/states/BuiltInBlockStates.h"
#include "mc/world/level/levelgen/structure/LegacyStructureSettings.h"

#include "projection/world/ProjectionVirtualWorld.h"

namespace lholo::projection::detail {
namespace {

CompoundTag const* serializedBlockStates(Block const& block) {
    for (auto const& [key, value] : block.mSerializationId.get()) {
        if (key == "states" && value.hold<CompoundTag>()) return &value.get<CompoundTag>();
    }
    return nullptr;
}

bool serializedStatesMatchExcept(
    Block const& expected,
    Block const& actual,
    std::string_view ignoredState
) {
    auto const* expectedStates = serializedBlockStates(expected);
    auto const* actualStates = serializedBlockStates(actual);
    if (!expectedStates || !actualStates) return expectedStates == actualStates;

    std::size_t comparedExpected{};
    std::size_t comparedActual{};
    for (auto const& [key, value] : *expectedStates) {
        if (key == ignoredState) continue;
        ++comparedExpected;
        auto const found = actualStates->mTags.find(key);
        if (found == actualStates->mTags.end() || !(value == found->second)) return false;
    }
    for (auto const& [key, value] : *actualStates) {
        (void)value;
        if (key != ignoredState) ++comparedActual;
    }
    return comparedExpected == comparedActual;
}

Block const& withWallConnections(Block const& block, BlockSource& region, BlockPos const& position) {
    auto const& wall = static_cast<WallBlock const&>(block.getBlockType());
    std::array const connections{
        std::pair{&VanillaStates::WallConnectionTypeNorth(), Facing::Name::North},
        std::pair{&VanillaStates::WallConnectionTypeSouth(), Facing::Name::South},
        std::pair{&VanillaStates::WallConnectionTypeWest(), Facing::Name::West},
        std::pair{&VanillaStates::WallConnectionTypeEast(), Facing::Name::East}
    };
    auto const* connected = &block;
    for (auto const& [state, face] : connections) {
        auto const desired = wall._desiredConnectionState(region, position, static_cast<uchar>(face));
        // The SDK's altered-state setter takes an integer, including enum states.
        auto const next = connected->setState(*state, static_cast<int>(desired));
        if (!next) return block;
        connected = next.as_ptr();
    }
    // Wall post visibility depends on the completed four-arm state. Select
    // immutable palette permutations only; never invoke tryFixWallStates,
    // which applies its result to the region and schedules world updates.
    auto const result = connected->setState(
        VanillaStates::WallPostBit(), WallBlock::_shouldBePost(region, position, *connected)
    );
    return result ? *result : block;
}

} // namespace

Block const* transformExpectedBlock(
    Block const*                   block,
    LegacyStructureSettings const& settings,
    bool                           identityTransform
) {
    if (!block) return nullptr;
    if (identityTransform) return block;
    // Use Bedrock's current generic state transformer. The legacy structure
    // aux-data mapper does not cover every modern state (notably
    // rail_direction), while this path owns the complete rotation/mirror
    // handling used by current blocks.
    return VanillaBlockStateTransformUtils::transformBlock(
        *block, settings.mRotation, settings.mMirror
    );
}

Block const& withNeighborConnections(
    Block const&    block,
    BlockSource&    region,
    BlockPos const& position
) {
    auto const& blockType = block.getBlockType();
    auto const wall = blockType.isWallBlock();
    // Native connection callbacks also own derived states such as stair
    // corners. Select the capability, rather than an incomplete family list.
    if (!wall && !block.tryGetComponent<BlockConnectionComponent>()) return block;
    // The connection update resolves the callbacks registered by the game.
    // It also APPLIES the recomputed block to the region
    // it is given — that filled the real world with ghost blocks — so run it
    // with region writes suppressed and use only its return value. Neighbor
    // reads still answer with the projected blocks while a tessellation scope
    // is active, and with the real world during correction.
    ScopedRegionWriteSuppression suppression;
    // Walls store their arms/post in serialized states and use a separate
    // native update path, rather than the flattened-connection component.
    if (wall) return withWallConnections(block, region, position);
    NeighborBlockDirections      directions{};
    auto&                        directionSet = directions.mDirections.get();
    for (int direction = 0; direction < static_cast<int>(NeighborDirection::Count); ++direction) {
        directionSet.insert(static_cast<NeighborDirection>(direction));
    }
    return blockType.connectionUpdate(region, block, position, directions);
}

bool projectionStatesMatch(Block const& expected, Block const& actual) {
    if (expected == actual) return true;
    auto const& typeName = expected.getTypeName();
    if (typeName != actual.getTypeName()) return false;
    // The native detector tick owns its environmental output strength. Keep
    // normal/inverted identity (checked above) and every other state strict.
    if (expected.getBlockType().getBlockEntityType() == BlockActorType::DaylightDetector) {
        auto const signal = actual.getState<int>(VanillaStates::RedstoneSignal());
        if (!signal) return false;
        auto const normalized = expected.setState(VanillaStates::RedstoneSignal(), *signal);
        return normalized && *normalized == actual;
    }
    // Chunker maps Java's bottom flag to Bedrock's stability-check marker.
    // That marker records a native support check, not placement orientation.
    // Keep the support distance and every other state in the full comparison.
    if (typeName == "minecraft:scaffolding") {
        auto const checked = actual.getState<bool>(VanillaStates::StabilityCheckBit());
        if (!checked) return false;
        auto const normalized = expected.setState(VanillaStates::StabilityCheckBit(), *checked);
        return normalized && *normalized == actual;
    }
    // Litematic's Java `stage` maps to Bedrock's dynamic `age_bit`, which is
    // reset when a player places a sapling. Ignore only that growth bit; any
    // other present or future sapling state remains part of correction.
    if (isVanillaSaplingType(typeName)) {
        return serializedStatesMatchExcept(expected, actual, "age_bit");
    }
    // Natural/Java leaves and player-placed leaves can have different decay
    // bookkeeping without a different build. Normalize only those two native
    // flags, then retain the complete comparison for every other state.
    if (typeName.starts_with("minecraft:") && expected.hasProperty(BlockProperty::Leaves)) {
        std::array const decayStates{&VanillaStates::PersistentBit(), &VanillaStates::UpdateBit()};
        auto const* normalized = &expected;
        for (auto const* state : decayStates) {
            auto const value = actual.getState<bool>(*state);
            if (!value) continue;
            auto const next = normalized->setState(*state, *value);
            if (!next) return false;
            normalized = next.as_ptr();
        }
        return *normalized == actual;
    }
    // Bamboo states (age_bit, bamboo_leaf_size, bamboo_stalk_thickness) are
    // dynamic botanical growth states computed by the game engine based on column
    // height and growth ticks. Players have zero manual control over them.
    // Matching block type identity is sufficient.
    if (typeName == "minecraft:bamboo") {
        return true;
    }

    auto stateMatches = [&](auto const& state) {
        using StateValue = typename std::remove_cvref_t<decltype(state)>::Type;
        auto const expectedValue = expected.getState<StateValue>(state);
        auto const actualValue = actual.getState<StateValue>(state);
        return expectedValue && actualValue && *expectedValue == *actualValue;
    };

    // A real door stores its placement state across two blocks: the lower half
    // owns direction/open, the upper half owns hinge. Bedrock may normalize the
    // duplicated fields differently after a structure load, so the complete
    // serialization hash can differ even for a correctly placed door. Require
    // the native Door property too: tall plants also carry upper_block_bit.
    auto const expectedUpper = expected.getState<bool>(VanillaStates::UpperBlockBit());
    if (expected.hasProperty(BlockProperty::Door) && expectedUpper) {
        auto const actualUpper = actual.getState<bool>(VanillaStates::UpperBlockBit());
        if (!actualUpper || *actualUpper != *expectedUpper) return false;
        return *expectedUpper
            ? stateMatches(VanillaStates::DoorHingeBit())
            : stateMatches(VanillaStates::Direction())
                && stateMatches(VanillaStates::OpenBit());
    }

    // Trapdoors are single blocks: compare their own placement states instead
    // of treating them like a two-block door.
    auto const expectedOpen = expected.getState<bool>(VanillaStates::OpenBit());
    if (expected.hasProperty(BlockProperty::Trapdoor) && expectedOpen) {
        auto const actualOpen = actual.getState<bool>(VanillaStates::OpenBit());
        return actualOpen && *actualOpen == *expectedOpen
            && stateMatches(VanillaStates::Direction())
            && stateMatches(VanillaStates::UpsideDownBit());
    }

    return false;
}

bool projectionStatesMatch(
    Block const& expected, Block const& actual, IConstBlockSource const& region, BlockPos const& position
) {
    if (projectionStatesMatch(expected, actual)) return true;
    if (expected.getTypeName() != actual.getTypeName()
        || !expected.getTypeName().starts_with("minecraft:")
        || !expected.hasProperty(BlockProperty::Stair)
        || !actual.hasProperty(BlockProperty::Stair)) return false;

    auto const expectedCorner = expected.getState<CornerShape>(BuiltInBlockStates::Corner());
    auto const actualCorner = actual.getState<CornerShape>(BuiltInBlockStates::Corner());
    if (!expectedCorner || !actualCorner
        || *expectedCorner == CornerShape::None || *actualCorner == CornerShape::None) return false;
    auto const actualDirection = actual.getState<int>(VanillaStates::WeirdoDirection());
    if (!actualDirection) return false;
    // Only the direction/corner representation may differ. Palette setters
    // preserve the upside-down flag and every other present or future state.
    auto const alignedDirection = expected.setState(VanillaStates::WeirdoDirection(), *actualDirection);
    if (!alignedDirection) return false;
    auto const aligned = alignedDirection->setState(BuiltInBlockStates::Corner(), static_cast<int>(*actualCorner));
    if (!aligned || !(*aligned == actual)) return false;

    std::vector<AABB> expectedShape, actualShape;
    expected.getBlockType().addAABBs(expected, region, position, nullptr, expectedShape);
    actual.getBlockType().addAABBs(actual, region, position, nullptr, actualShape);
    return sameStairShape(expectedShape, actualShape);
}

// Front face (Facing 0-5) for a block-entity placeholder, read from whichever
// facing state the block actually carries. Chests and similar block entities
// moved from the integer facing_direction to the string
// minecraft:cardinal_direction, so both are handled. Returns -1 when the block
// has no horizontal facing.
int blockFrontFace(Block const& block) {
    for (auto const& [key, value] : block.mSerializationId.get()) {
        if (key != "states") continue;
        if (!value.hold<CompoundTag>()) break;
        for (auto const& [stateKey, stateValue] : value.get<CompoundTag>()) {
            if (stateKey == "facing_direction" && stateValue.getId() == Tag::Type::Int) {
                return stateValue.get<IntTag>().data;
            }
            if (stateKey == "minecraft:cardinal_direction" && stateValue.getId() == Tag::Type::String) {
                std::string const& facing = static_cast<std::string const&>(stateValue.get<StringTag>());
                if (facing == "north") return static_cast<int>(Facing::Name::North);
                if (facing == "south") return static_cast<int>(Facing::Name::South);
                if (facing == "west")  return static_cast<int>(Facing::Name::West);
                if (facing == "east")  return static_cast<int>(Facing::Name::East);
            }
        }
        break;
    }
    return -1;
}

} // namespace lholo::projection::detail
