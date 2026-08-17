// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// Java -> Bedrock block-state translation for the .litematic loader.
//
// The .mcstructure path goes through the vanilla StructureTemplate loader,
// which upgrades and preserves every block state. Java `.litematic` files carry
// Java Edition names and Java property strings, so this module reproduces the
// missing step: map the Java state to the equivalent Bedrock state and resolve
// it to a real Bedrock block.
//
// Design (adapted from the LitematicaBE mapping module, minus its TSV table and
// server-side ghost/packet machinery, which do not apply to LHolo's client-side
// tessellator):
//
//   1. Look up the Bedrock block by name and enumerate its permutations, once
//      per name, caching the real state key/value strings of each variant.
//   2. Inspect which state keys the block actually has, and emit only the
//      Bedrock states that exist on it, converting Java property values with the
//      per-family rules below (verified facing/weirdo/trapdoor tables come from
//      LitematicaBE's StateSemantics).
//   3. Resolve by partial match: the first permutation whose specified states
//      all agree wins; if none match, fall back to the default permutation.
//
// Because emitted states are gated on the block's real schema and matching
// always has a same-type fallback, an imperfect mapping degrades to "correct
// block, default orientation" rather than a crash or a garbage block.

#include "structure/JavaBlockMapping.h"

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/ByteTag.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/deps/nbt/Tag.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/registry/BlockTypeRegistry.h"
#include "mc/world/level/material/Material.h"

namespace lholo::structure {
namespace {

using StatePairs = std::vector<std::pair<std::string, std::string>>;

// -----------------------------------------------------------------------------
// Name classification helpers
// -----------------------------------------------------------------------------

bool endsWith(std::string const& s, std::string_view suffix) {
    return s.size() >= suffix.size()
        && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool isStairs(std::string const& name) { return endsWith(name, "_stairs"); }
bool isTrapdoor(std::string const& name) {
    return endsWith(name, "_trapdoor") || name == "minecraft:trapdoor";
}
bool isDoor(std::string const& name) {
    return endsWith(name, "_door") && !endsWith(name, "_trapdoor");
}
bool isWall(std::string const& name) { return endsWith(name, "_wall"); }

// Blocks the game generates on its own (piston heads, portals, fire, ...). The
// projection skips them so the user is never asked to place an unobtainable
// block. Straight from LitematicaBE's isDerivedBlock.
bool isDerivedBlock(std::string const& name) {
    return name == "minecraft:piston_head"
        || name == "minecraft:moving_piston"
        || name == "minecraft:bubble_column"
        || name == "minecraft:fire"
        || name == "minecraft:soul_fire"
        || name == "minecraft:frosted_ice"
        || name == "minecraft:nether_portal"
        || name == "minecraft:end_portal"
        || name == "minecraft:end_gateway";
}

// Java blocks that sit in water without a `waterlogged` property. On Bedrock the
// water must be placed on the second layer explicitly.
bool isImplicitlyWaterlogged(std::string const& name) {
    return name == "minecraft:seagrass"
        || name == "minecraft:tall_seagrass"
        || name == "minecraft:kelp"
        || name == "minecraft:kelp_plant"
        || name == "minecraft:tube_coral"
        || name == "minecraft:brain_coral"
        || name == "minecraft:bubble_coral"
        || name == "minecraft:fire_coral"
        || name == "minecraft:horn_coral"
        || name == "minecraft:tube_coral_fan"
        || name == "minecraft:brain_coral_fan"
        || name == "minecraft:bubble_coral_fan"
        || name == "minecraft:fire_coral_fan"
        || name == "minecraft:horn_coral_fan";
}

// -----------------------------------------------------------------------------
// Java facing/value conversion tables (verified in LitematicaBE)
// -----------------------------------------------------------------------------

// Bedrock `facing_direction` (down/up/north/south/west/east).
int standardFacingDirection(std::string const& facing) {
    if (facing == "down")  return 0;
    if (facing == "up")    return 1;
    if (facing == "north") return 2;
    if (facing == "south") return 3;
    if (facing == "west")  return 4;
    if (facing == "east")  return 5;
    return -1;
}

// Bedrock stair `weirdo_direction`.
int weirdoDirection(std::string const& facing) {
    if (facing == "east")  return 0;
    if (facing == "west")  return 1;
    if (facing == "south") return 2;
    if (facing == "north") return 3;
    return -1;
}

// Bedrock trapdoor `direction`.
int trapdoorDirection(std::string const& facing) {
    if (facing == "east")  return 0;
    if (facing == "west")  return 1;
    if (facing == "south") return 2;
    if (facing == "north") return 3;
    return -1;
}

// Bedrock door `direction`. Java `facing` is the direction the closed door
// faces; Bedrock stores the same four directions on `direction`.
int doorDirection(std::string const& facing) {
    if (facing == "east")  return 0;
    if (facing == "south") return 1;
    if (facing == "west")  return 2;
    if (facing == "north") return 3;
    return -1;
}

// Java wall connection height (none/low/tall) -> Bedrock wall_connection_type.
std::string wallConnection(std::string const& value) {
    if (value == "tall") return "tall";
    if (value == "low")  return "short";
    return "none";
}

// Java rail `shape` -> Bedrock `rail_direction` (identical index order).
int railDirection(std::string const& shape) {
    if (shape == "north_south")     return 0;
    if (shape == "east_west")       return 1;
    if (shape == "ascending_east")  return 2;
    if (shape == "ascending_west")  return 3;
    if (shape == "ascending_north") return 4;
    if (shape == "ascending_south") return 5;
    if (shape == "south_east")      return 6;
    if (shape == "south_west")      return 7;
    if (shape == "north_west")      return 8;
    if (shape == "north_east")      return 9;
    return -1;
}

// Opposite horizontal facing; up/down unchanged.
std::string flipHorizontal(std::string const& facing) {
    if (facing == "north") return "south";
    if (facing == "south") return "north";
    if (facing == "west")  return "east";
    if (facing == "east")  return "west";
    return facing;
}

// Swap only the east/west axis. Repeaters/comparators and torches came out
// mirrored on X in-game (verified against the actual game, not the reference
// table), while other cardinal_direction blocks like chests were correct.
std::string swapEastWest(std::string const& facing) {
    if (facing == "east") return "west";
    if (facing == "west") return "east";
    return facing;
}

// Repeaters and comparators (diodes) whose cardinal_direction needs the X swap.
bool isDiode(std::string const& name) {
    return name == "minecraft:unpowered_repeater" || name == "minecraft:powered_repeater"
        || name == "minecraft:unpowered_comparator" || name == "minecraft:powered_comparator";
}

bool isLever(std::string const& name)  { return name == "minecraft:lever"; }
bool isButton(std::string const& name) { return endsWith(name, "_button"); }
bool isRail(std::string const& name) {
    return name == "minecraft:rail" || name == "minecraft:golden_rail"
        || name == "minecraft:detector_rail" || name == "minecraft:activator_rail";
}

// Pistons face the opposite horizontal way between editions; their up/down
// orientation already matches, so only the horizontal cardinal is flipped.
// (Confirmed in testing: only pistons need this; hoppers/droppers/observers are
// already correct with the standard facing.)
bool flipsHorizontalFacing(std::string const& name) {
    return name == "minecraft:piston" || name == "minecraft:sticky_piston";
}

// Java lever (face + facing) -> Bedrock `lever_direction` string.
std::string leverDirection(std::string const& face, std::string const& facing) {
    bool const axisEastWest = (facing == "east" || facing == "west");
    if (face == "floor")   return axisEastWest ? "up_east_west"   : "up_north_south";
    if (face == "ceiling") return axisEastWest ? "down_east_west" : "down_north_south";
    if (facing == "north" || facing == "south" || facing == "east" || facing == "west") {
        return facing;  // wall-mounted: points out along `facing`
    }
    return "north";
}

// Java button/grindstone `face`+`facing` -> Bedrock `facing_direction` (0-5).
int mountedFacingDirection(std::string const& face, std::string const& facing) {
    if (face == "floor")   return 1;  // up
    if (face == "ceiling") return 0;  // down
    return standardFacingDirection(facing);
}

// Java -> Bedrock block name renames (only the ones that actually differ, so
// they resolve instead of silently vanishing).
std::string bedrockName(std::string const& java) {
    static std::unordered_map<std::string, std::string> const kRenames = {
        {"minecraft:note_block", "minecraft:noteblock"},
        {"minecraft:powered_rail", "minecraft:golden_rail"},
        {"minecraft:lily_pad", "minecraft:waterlily"},
        {"minecraft:cobweb", "minecraft:web"},
        {"minecraft:dirt_path", "minecraft:grass_path"},
        {"minecraft:magma_block", "minecraft:magma"},
        {"minecraft:nether_quartz_ore", "minecraft:quartz_ore"},
        {"minecraft:snow_block", "minecraft:snow"},
        {"minecraft:snow", "minecraft:snow_layer"},
        {"minecraft:wall_torch", "minecraft:torch"},
        {"minecraft:soul_wall_torch", "minecraft:soul_torch"},
        {"minecraft:redstone_wall_torch", "minecraft:redstone_torch"},
        {"minecraft:comparator", "minecraft:unpowered_comparator"},
        {"minecraft:repeater", "minecraft:unpowered_repeater"},
    };
    auto const it = kRenames.find(java);
    return it == kRenames.end() ? java : it->second;
}

// -----------------------------------------------------------------------------
// Permutation cache: name -> every variant's (state key/value strings, block)
// -----------------------------------------------------------------------------

struct PermTable {
    std::vector<std::pair<StatePairs, Block const*>> perms;
};

std::mutex                                  gCacheMutex;
std::unordered_map<std::string, PermTable>  gPermCache;
Block const*                                gWaterSource = nullptr;
bool                                        gWaterResolved = false;

// Read a block's serialized `states` compound into (key, value) string pairs.
StatePairs readBlockStates(Block const& block) {
    StatePairs out;
    for (auto const& [key, value] : block.getSerializationId()) {
        if (key != "states") continue;
        if (!value.hold<::CompoundTag>()) break;
        for (auto const& [stateKey, stateValue] : value.get<::CompoundTag>()) {
            switch (stateValue.getId()) {
            case ::Tag::Type::Byte:
                out.emplace_back(stateKey, std::to_string(static_cast<int>(stateValue.get<::ByteTag>().data)));
                break;
            case ::Tag::Type::Int:
                out.emplace_back(stateKey, std::to_string(stateValue.get<::IntTag>().data));
                break;
            case ::Tag::Type::String:
                out.emplace_back(stateKey, static_cast<std::string const&>(stateValue.get<::StringTag>()));
                break;
            default:
                break;
            }
        }
        break;
    }
    return out;
}

// Caller holds gCacheMutex. Returns an empty table for unknown names.
PermTable const& permutationsFor(std::string const& name) {
    auto const cached = gPermCache.find(name);
    if (cached != gPermCache.end()) return cached->second;

    PermTable table;
    auto const& def = BlockTypeRegistry::get().getDefaultBlockState(HashedString(name), false);
    // getDefaultBlockState returns air for unknown names; only enumerate when the
    // resolved type genuinely matches the requested name.
    if (def.getTypeName() == name) {
        def.getBlockType().forEachBlockPermutation([&](Block const& permutation) {
            table.perms.emplace_back(readBlockStates(permutation), &permutation);
            return true;
        });
    }
    auto const inserted = gPermCache.emplace(name, std::move(table)).first;
    return inserted->second;
}

Block const* waterSource() {
    if (!gWaterResolved) {
        auto const& def = BlockTypeRegistry::get().getDefaultBlockState(HashedString("minecraft:water"), false);
        gWaterSource   = def.isAir() ? nullptr : &def;
        gWaterResolved = true;
    }
    return gWaterSource;
}

// -----------------------------------------------------------------------------
// Java properties -> Bedrock states (gated on the block's real state schema)
// -----------------------------------------------------------------------------

StatePairs mapProperties(
    std::string const&                                      name,
    std::vector<std::pair<std::string, std::string>> const& properties,
    std::unordered_set<std::string> const&                  keys
) {
    StatePairs requested;
    auto const has  = [&](char const* key) { return keys.count(key) != 0; };
    auto const emit = [&](std::string key, std::string value) {
        requested.emplace_back(std::move(key), std::move(value));
    };
    auto const find = [&](char const* key) -> std::string {
        for (auto const& [pk, pv] : properties) if (pk == key) return pv;
        return {};
    };

    bool const lever  = isLever(name);
    bool const button = isButton(name);
    bool const rail   = isRail(name);
    bool const flipH  = flipsHorizontalFacing(name);
    bool const torch  = has("torch_facing_direction");

    // Multi-property families: consume `face`/`facing`/`shape` up front so the
    // per-property loop below does not also touch them.
    if (lever && has("lever_direction")) {
        emit("lever_direction", leverDirection(find("face"), find("facing")));
    }
    if (button && has("facing_direction")) {
        int const d = mountedFacingDirection(find("face"), find("facing"));
        if (d >= 0) emit("facing_direction", std::to_string(d));
    }
    if (rail && has("rail_direction")) {
        int const d = railDirection(find("shape"));
        if (d >= 0) emit("rail_direction", std::to_string(d));
    }
    if (torch) {
        // Wall torches carry `facing`; standing torches have none -> "top".
        // Bedrock names the wall side (opposite of Java on north/south), but the
        // east/west axis came out mirrored in-game, so undo the X flip.
        std::string const f = find("facing");
        emit("torch_facing_direction", f.empty() ? "top" : swapEastWest(flipHorizontal(f)));
    }

    for (auto const& [key, value] : properties) {
        if (key == "facing") {
            if (lever || button || torch) continue;  // already consumed above
            std::string const f = flipH ? flipHorizontal(value) : value;
            if (has("weirdo_direction")) {
                int const d = weirdoDirection(f);
                if (d >= 0) emit("weirdo_direction", std::to_string(d));
            } else if (has("facing_direction")) {
                int const d = standardFacingDirection(f);
                if (d >= 0) emit("facing_direction", std::to_string(d));
            } else if (has("minecraft:cardinal_direction")) {
                emit("minecraft:cardinal_direction", isDiode(name) ? swapEastWest(f) : f);
            } else if (has("minecraft:facing_direction")) {
                emit("minecraft:facing_direction", f);
            } else if (has("direction")) {
                int const d = isDoor(name) ? doorDirection(f) : trapdoorDirection(f);
                if (d >= 0) emit("direction", std::to_string(d));
            }
        } else if (key == "half") {
            // Stairs/trapdoors: top vs bottom. Doors/tall plants: upper vs lower.
            if (has("upside_down_bit"))         emit("upside_down_bit", value == "top" ? "1" : "0");
            if (has("upper_block_bit"))         emit("upper_block_bit", value == "upper" ? "1" : "0");
            if (has("minecraft:vertical_half")) emit("minecraft:vertical_half", value);
        } else if (key == "type") {
            // Slabs: top/bottom (double slabs are a separate block, left as-is).
            if (value != "double") {
                if (has("minecraft:vertical_half")) emit("minecraft:vertical_half", value);
                else if (has("top_slot_bit"))       emit("top_slot_bit", value == "top" ? "1" : "0");
            }
        } else if (key == "axis") {
            if (has("pillar_axis")) emit("pillar_axis", value);
        } else if (key == "open") {
            if (has("open_bit")) emit("open_bit", value == "true" ? "1" : "0");
        } else if (key == "hinge") {
            if (has("door_hinge_bit")) emit("door_hinge_bit", value == "right" ? "1" : "0");
        } else if (key == "level") {
            // Water/lava fill. Bedrock liquid_depth mirrors the Java level number.
            if (has("liquid_depth")) emit("liquid_depth", value);
        } else if (key == "delay") {
            // Repeater: Java delay 1..4 -> Bedrock repeater_delay 0..3.
            if (has("repeater_delay")) {
                int const d = std::atoi(value.c_str()) - 1;
                if (d >= 0 && d <= 3) emit("repeater_delay", std::to_string(d));
            }
        } else if (key == "mode") {
            // Comparator: compare/subtract.
            if (has("output_subtract_bit")) emit("output_subtract_bit", value == "subtract" ? "1" : "0");
        } else if (key == "powered") {
            if (rail && has("rail_data_bit"))    emit("rail_data_bit", value == "true" ? "1" : "0");
            else if (has("button_pressed_bit"))  emit("button_pressed_bit", value == "true" ? "1" : "0");
        } else if (key == "rotation") {
            // Standing signs / banners, 0..15.
            if (has("ground_sign_direction")) emit("ground_sign_direction", value);
        } else if (key == "north" || key == "east" || key == "south" || key == "west") {
            if (isWall(name)) {
                std::string const stateKey = "wall_connection_type_" + key;
                if (has(stateKey.c_str())) emit(stateKey, wallConnection(value));
            }
        } else if (key == "up") {
            if (isWall(name) && has("wall_post_bit")) emit("wall_post_bit", value == "true" ? "1" : "0");
        }
        // Other Java properties (shape on non-rails, snowy, distance, ...) are
        // intentionally ignored: neighbor-derived (recomputed by Bedrock at
        // render time) or handled separately (waterlogged in the caller).
    }
    return requested;
}

// First permutation whose requested states all agree; else the default variant.
Block const* resolvePermutation(PermTable const& table, StatePairs const& requested) {
    if (table.perms.empty()) return nullptr;
    for (auto const& [states, block] : table.perms) {
        bool matches = true;
        for (auto const& [key, value] : requested) {
            auto const found = std::find_if(states.begin(), states.end(),
                [&](auto const& s) { return s.first == key; });
            if (found == states.end() || found->second != value) {
                matches = false;
                break;
            }
        }
        if (matches) return block;
    }
    return table.perms.front().second;
}

} // namespace

ResolvedJavaBlock resolveJavaBlockState(
    std::string const&                                      javaName,
    std::vector<std::pair<std::string, std::string>> const& properties
) {
    if (javaName.empty() || javaName == "minecraft:air") return {};
    if (isDerivedBlock(javaName)) return {};

    // Some blocks are named differently on Bedrock; translate before lookup so
    // they resolve instead of silently disappearing.
    std::string const name = bedrockName(javaName);

    std::lock_guard lock(gCacheMutex);

    auto const& table = permutationsFor(name);
    if (table.perms.empty()) return {};  // Unknown on Bedrock; caller skips it.

    // Which Bedrock states does this block actually carry?
    std::unordered_set<std::string> keys;
    for (auto const& [key, value] : table.perms.front().first) keys.insert(key);

    auto const requested = mapProperties(name, properties, keys);
    Block const* resolved = resolvePermutation(table, requested);
    if (resolved && resolved->isAir()) resolved = nullptr;

    // Waterlogged: explicit property or an implicitly submerged block.
    bool waterlogged = isImplicitlyWaterlogged(javaName);
    if (!waterlogged) {
        for (auto const& [key, value] : properties) {
            if (key == "waterlogged" && value == "true") { waterlogged = true; break; }
        }
    }

    ResolvedJavaBlock result;
    if (resolved && resolved->getMaterial().isLiquid()) {
        // A Java liquid block feeds the liquid layer, matching .mcstructure.
        result.liquid = resolved;
    } else {
        result.block = resolved;
        if (waterlogged) result.liquid = waterSource();
    }
    return result;
}

} // namespace lholo::structure
