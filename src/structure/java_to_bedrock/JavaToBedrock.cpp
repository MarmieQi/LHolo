// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The mapping data consumed here is generated from Chunker (MIT) and targets
// Bedrock 1.26.20. See THIRD_PARTY_NOTICES.md and tools/java_to_bedrock/.

#include "structure/java_to_bedrock/JavaToBedrock.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <zlib.h>

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

#include "structure/java_to_bedrock/GeneratedChunkerMappings.inc"

struct Mapping {
    int         minDataVersion{};
    std::string bedrockName;
    StatePairs  bedrockStates;
    bool        waterlogged{};
};

using MappingTable = std::unordered_map<std::string, std::vector<Mapping>>;

std::string canonicalProperties(StatePairs properties) {
    std::ranges::sort(properties);
    std::string result;
    for (auto const& [key, value] : properties) {
        if (!result.empty()) result.push_back(',');
        result.append(key).push_back('=');
        result.append(value);
    }
    return result;
}

StatePairs parseStates(std::string_view text, bool& waterlogged) {
    StatePairs states;
    while (!text.empty()) {
        auto const comma = text.find(',');
        auto const item = text.substr(0, comma);
        auto const equal = item.find('=');
        if (equal != std::string_view::npos) {
            std::string key{item.substr(0, equal)};
            std::string value{item.substr(equal + 1)};
            if (key == "waterlogged") {
                waterlogged = value == "true";
            } else {
                // Chunker represents byte states as booleans; the game registry
                // serializes those same states as 0/1 ByteTags.
                if (value == "true") value = "1";
                else if (value == "false") value = "0";
                states.emplace_back(std::move(key), std::move(value));
            }
        }
        if (comma == std::string_view::npos) break;
        text.remove_prefix(comma + 1);
    }
    return states;
}

MappingTable loadMappings() {
    std::string table(kChunkerMappingRawSize, '\0');
    uLongf outputSize = static_cast<uLongf>(table.size());
    auto const result = uncompress(
        reinterpret_cast<Bytef*>(table.data()),
        &outputSize,
        kChunkerMappingCompressed,
        static_cast<uLong>(sizeof(kChunkerMappingCompressed))
    );
    if (result != Z_OK || outputSize != table.size()) {
        throw std::runtime_error("Could not decompress generated Chunker block mappings");
    }

    MappingTable mappings;
    std::string_view remaining{table};
    while (!remaining.empty()) {
        auto const newline = remaining.find('\n');
        auto const line = remaining.substr(0, newline);
        if (!line.empty() && line.front() != '#') {
            std::string_view columns[5];
            auto rest = line;
            for (int i = 0; i < 4; ++i) {
                auto const tab = rest.find('\t');
                columns[i] = rest.substr(0, tab);
                rest.remove_prefix(tab + 1);
            }
            columns[4] = rest;

            Mapping mapping;
            std::from_chars(
                columns[0].data(), columns[0].data() + columns[0].size(), mapping.minDataVersion
            );
            mapping.bedrockName = columns[3];
            mapping.bedrockStates = parseStates(columns[4], mapping.waterlogged);

            std::string key{columns[1]};
            key.push_back('\0');
            key.append(columns[2]);
            mappings[std::move(key)].push_back(std::move(mapping));
        }
        if (newline == std::string_view::npos) break;
        remaining.remove_prefix(newline + 1);
    }
    return mappings;
}

Mapping const* findMapping(
    std::string const& javaName,
    StatePairs const&  properties,
    int                javaDataVersion
) {
    static MappingTable const mappings = loadMappings();
    std::string key = javaName;
    key.push_back('\0');
    key.append(canonicalProperties(properties));
    auto const found = mappings.find(key);
    if (found == mappings.end()) return nullptr;

    auto const& versions = found->second;
    Mapping const* selected = &versions.front();
    for (auto const& version : versions) {
        if (javaDataVersion > 0 && version.minDataVersion > javaDataVersion) break;
        selected = &version;
    }
    return selected;
}

struct PermutationTable {
    std::vector<std::pair<StatePairs, Block const*>> permutations;
};

std::mutex                                        gCacheMutex;
std::unordered_map<std::string, PermutationTable> gPermutationCache;
Block const*                                      gWaterSource{};

StatePairs readBlockStates(Block const& block) {
    StatePairs states;
    for (auto const& [key, value] : block.getSerializationId()) {
        if (key != "states" || !value.hold<::CompoundTag>()) continue;
        for (auto const& [stateKey, stateValue] : value.get<::CompoundTag>()) {
            switch (stateValue.getId()) {
            case ::Tag::Type::Byte:
                states.emplace_back(
                    stateKey, std::to_string(static_cast<int>(stateValue.get<::ByteTag>().data))
                );
                break;
            case ::Tag::Type::Int:
                states.emplace_back(stateKey, std::to_string(stateValue.get<::IntTag>().data));
                break;
            case ::Tag::Type::String:
                states.emplace_back(stateKey, static_cast<std::string const&>(stateValue.get<::StringTag>()));
                break;
            default:
                break;
            }
        }
        break;
    }
    return states;
}

PermutationTable const& permutationsFor(std::string const& name) {
    auto const cached = gPermutationCache.find(name);
    if (cached != gPermutationCache.end()) return cached->second;

    PermutationTable table;
    auto const& defaultBlock = BlockTypeRegistry::get().getDefaultBlockState(HashedString(name), false);
    if (defaultBlock.getTypeName() == name) {
        defaultBlock.getBlockType().forEachBlockPermutation([&](Block const& permutation) {
            table.permutations.emplace_back(readBlockStates(permutation), &permutation);
            return true;
        });
    }
    return gPermutationCache.emplace(name, std::move(table)).first->second;
}

Block const* resolvePermutation(PermutationTable const& table, StatePairs const& requested) {
    for (auto const& [states, block] : table.permutations) {
        bool matches = std::ranges::all_of(requested, [&](auto const& requestedState) {
            return std::ranges::find(states, requestedState) != states.end();
        });
        if (matches) return block;
    }
    return nullptr;
}

Block const* waterSource() {
    if (!gWaterSource) {
        auto const& water = BlockTypeRegistry::get().getDefaultBlockState(
            HashedString("minecraft:water"), false
        );
        if (!water.isAir()) gWaterSource = &water;
    }
    return gWaterSource;
}

Block const* resolveExactBedrockBlock(std::string const& bedrockName) {
    if (bedrockName.empty()) return nullptr;
    auto const& block = BlockTypeRegistry::get().getDefaultBlockState(
        HashedString(bedrockName), false
    );
    if (block.getTypeName() == bedrockName && !block.isAir()) return &block;
    return nullptr;
}

} // namespace

ResolvedJavaBlock resolveJavaBlockState(
    std::string const&                                      javaName,
    std::vector<std::pair<std::string, std::string>> const& properties,
    int                                                     javaDataVersion
) {
    auto const* mapping = findMapping(javaName, properties, javaDataVersion);
    if (!mapping) {
        std::lock_guard lock(gCacheMutex);
        auto const* resolved = resolveExactBedrockBlock(javaName);
        if (!resolved || resolved->isAir()) return {};

        ResolvedJavaBlock result{.mapped = true};
        if (resolved->getMaterial().isLiquid()) {
            result.liquid = resolved;
        } else {
            result.block = resolved;
        }
        return result;
    }
    if (mapping->bedrockName == "minecraft:air") return {.mapped = true};

    std::lock_guard lock(gCacheMutex);
    auto const* resolved = resolvePermutation(
        permutationsFor(mapping->bedrockName), mapping->bedrockStates
    );
    if (!resolved || resolved->isAir()) {
        resolved = resolveExactBedrockBlock(mapping->bedrockName);
    }
    if (!resolved || resolved->isAir()) return {};

    ResolvedJavaBlock result{.mapped = true};
    if (resolved->getMaterial().isLiquid()) {
        result.liquid = resolved;
    } else {
        result.block = resolved;
        if (mapping->waterlogged) result.liquid = waterSource();
    }
    return result;
}

void resetJavaBlockMappingCache() {
    std::lock_guard lock(gCacheMutex);
    gPermutationCache.clear();
    gWaterSource = nullptr;
}

} // namespace lholo::structure
