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

#pragma once

#include <string>
#include <utility>
#include <vector>

class Block;

namespace lholo::structure {

// The result of resolving one Java block state (name + properties) to Bedrock.
// A cell may carry both a solid block and a liquid at once (waterlogged blocks
// and Java liquids both feed the `liquid` slot), matching the two-layer
// RenderBlock model shared with the .mcstructure path.
struct ResolvedJavaBlock {
    Block const* block  = nullptr;  // Solid/body block; nullptr for pure liquids,
                                    // air, unknown names, and game-derived blocks.
    Block const* liquid = nullptr;  // Water/lava layer; nullptr when dry.
};

// Translate a Java Edition block state to a Bedrock block, carrying over
// orientation and other stored states (facing, stairs half, log axis, wall
// connections, waterlogged, ...). Unknown properties are ignored and states
// that a block does not actually have are never emitted, so the result always
// resolves to at least the correct block type. `properties` is the Java
// `Properties` compound as (key, value) string pairs.
ResolvedJavaBlock resolveJavaBlockState(
    std::string const&                                       javaName,
    std::vector<std::pair<std::string, std::string>> const& properties
);

} // namespace lholo::structure
