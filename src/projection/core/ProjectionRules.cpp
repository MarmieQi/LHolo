// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "projection/core/ProjectionRules.h"

#include <string>
#include <type_traits>

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/deps/nbt/Tag.h"
#include "mc/world/Facing.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/VanillaStates.h"
#include "mc/world/level/block/states/VanillaBlockStateTransformUtils.h"
#include "mc/world/level/levelgen/structure/LegacyStructureSettings.h"

namespace lholo::projection::detail {
namespace {

CompoundTag const* serializedBlockStates(Block const& block) {
    for (auto const& [key, value] : block.getSerializationId()) {
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
        *block, settings.getRotation(), settings.getMirror()
    );
}

bool projectionStatesMatch(Block const& expected, Block const& actual) {
    if (expected == actual) return true;
    if (expected.getTypeName() != actual.getTypeName()) return false;
    // Litematic's Java `stage` maps to Bedrock's dynamic `age_bit`, which is
    // reset when a player places a sapling. Ignore only that growth bit; any
    // other present or future sapling state remains part of correction.
    if (isVanillaSaplingType(expected.getTypeName())) {
        return serializedStatesMatchExcept(expected, actual, "age_bit");
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
    // serialization hash can differ even for a correctly placed door. Only real
    // doors carry upper_block_bit; trapdoor names also end with "door", so the
    // presence of that state is the reliable discriminator.
    auto const expectedUpper = expected.getState<bool>(VanillaStates::UpperBlockBit());
    if (expectedUpper) {
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
    if (expectedOpen) {
        auto const actualOpen = actual.getState<bool>(VanillaStates::OpenBit());
        return actualOpen && *actualOpen == *expectedOpen
            && stateMatches(VanillaStates::Direction())
            && stateMatches(VanillaStates::UpsideDownBit());
    }

    return false;
}

// Front face (Facing 0-5) for a block-entity placeholder, read from whichever
// facing state the block actually carries. Chests and similar block entities
// moved from the integer facing_direction to the string
// minecraft:cardinal_direction, so both are handled. Returns -1 when the block
// has no horizontal facing.
int blockFrontFace(Block const& block) {
    for (auto const& [key, value] : block.getSerializationId()) {
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
