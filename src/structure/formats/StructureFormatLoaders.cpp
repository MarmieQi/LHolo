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

#include "structure/formats/StructureFormatLoaders.h"
#include "structure/StructureLoader.h"
#include "block/BlockPlacementRules.h"
#include "structure/java_to_bedrock/JavaBlockEntityToBedrock.h"
#include "structure/java_to_bedrock/JavaToBedrock.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

#include <zlib.h>

#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/registry/BlockTypeRegistry.h"
#include "mc/world/level/material/Material.h"
#include "mc/world/level/levelgen/structure/StructureBlockPalette.h"
#include "mc/world/level/levelgen/structure/StructureTemplate.h"
#include "mc/world/level/Level.h"

namespace lholo::structure::detail {
namespace {

constexpr std::uintmax_t kMaximumStructureFileSize = 512ull * 1024ull * 1024ull;
constexpr std::size_t    kMaximumInflatedFileSize  = 1024ull * 1024ull * 1024ull;
std::atomic_uint64_t     gGeneration{0};

std::string materialKey(Block const* block) {
    if (!block) return {};
    auto const typeName = std::string{block->getTypeName()};
    if (typeName == "minecraft:bubble_column"
        || typeName == "minecraft:piston_arm_collision"
        || typeName == "minecraft:sticky_piston_arm_collision"
        || typeName == "minecraft:moving_block") return {};
    if (typeName == "minecraft:water" || typeName == "minecraft:flowing_water") {
        return "minecraft:water";
    }
    if (typeName == "minecraft:lava" || typeName == "minecraft:flowing_lava") {
        return "minecraft:lava";
    }
    auto const baseName = std::string{block::placeableBaseName(typeName)};
    if (baseName == "minecraft:redstone_wire") return "item:minecraft:redstone";
    if (baseName == "minecraft:unpowered_comparator"
        || baseName == "minecraft:powered_comparator") {
        return "item:minecraft:comparator";
    }
    if (baseName == "minecraft:unpowered_repeater"
        || baseName == "minecraft:powered_repeater") {
        return "item:minecraft:repeater";
    }
    if (baseName == "minecraft:unlit_redstone_torch") {
        return "item:minecraft:redstone_torch";
    }
    return "item:" + baseName;
}

void assignMaterialIndices(LoadedStructure& loaded) {
    std::map<std::string, std::uint64_t> bodyCounts;
    std::map<std::string, std::uint64_t> liquidCounts;
    for (auto const& entry : loaded.renderBlocks) {
        auto const body = materialKey(entry.block);
        auto const liquid = materialKey(entry.liquid);
        if (!body.empty()) ++bodyCounts[body];
        if (!liquid.empty()) ++liquidCounts[liquid];
    }
    auto sortedKeys = [](auto const& counts) {
        std::vector<std::string> keys;
        for (auto const& [key, count] : counts) {
            (void)count;
            keys.push_back(key);
        }
        std::sort(keys.begin(), keys.end(), [&](auto const& left, auto const& right) {
            if (counts.at(left) != counts.at(right)) return counts.at(left) > counts.at(right);
            return left < right;
        });
        return keys;
    };
    auto const bodyKeys = sortedKeys(bodyCounts);
    auto const liquidKeys = sortedKeys(liquidCounts);
    std::map<std::string, int> bodyIndices;
    std::map<std::string, int> liquidIndices;
    for (std::size_t index = 0; index < bodyKeys.size(); ++index) {
        bodyIndices.emplace(bodyKeys[index], static_cast<int>(index));
    }
    for (std::size_t index = 0; index < liquidKeys.size(); ++index) {
        liquidIndices.emplace(
            liquidKeys[index], static_cast<int>(bodyKeys.size() + index)
        );
    }
    for (auto& entry : loaded.renderBlocks) {
        if (auto const found = bodyIndices.find(materialKey(entry.block)); found != bodyIndices.end()) {
            entry.materialIndex = found->second;
        }
        if (auto const found = liquidIndices.find(materialKey(entry.liquid)); found != liquidIndices.end()) {
            entry.liquidMaterialIndex = found->second;
        }
    }
    loaded.materialCount = bodyKeys.size() + liquidKeys.size();
}

std::optional<std::string> readFile(std::filesystem::path const& path, std::string& error) {
    std::error_code filesystemError;
    if (!std::filesystem::is_regular_file(path, filesystemError)) {
        error = filesystemError ? filesystemError.message() : "路径不是普通文件";
        return std::nullopt;
    }
    auto const size = std::filesystem::file_size(path, filesystemError);
    if (filesystemError) {
        error = filesystemError.message();
        return std::nullopt;
    }
    if (size == 0 || size > kMaximumStructureFileSize) {
        error = "文件为空或超过 512 MiB 限制";
        return std::nullopt;
    }
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        error = "文件过大";
        return std::nullopt;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "无法打开文件";
        return std::nullopt;
    }
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
        error = "读取文件失败";
        return std::nullopt;
    }
    return bytes;
}

CompoundTagVariant const* findTag(CompoundTag const& parent, std::string_view name) {
    auto const found = parent.mTags.find(name);
    return found == parent.mTags.end() ? nullptr : &found->second;
}

CompoundTag const* findCompound(CompoundTag const& parent, std::string_view name) {
    auto const* value = findTag(parent, name);
    return value && value->hold<CompoundTag>() ? &value->get<CompoundTag>() : nullptr;
}

ListTag const* findList(CompoundTag const& parent, std::string_view name) {
    auto const* value = findTag(parent, name);
    return value && value->hold<ListTag>() ? &value->get<ListTag>() : nullptr;
}

bool readThreeInts(ListTag const& list, int& x, int& y, int& z) {
    if (list.size() != 3) return false;
    auto const read = [&list](std::size_t index, int& output) {
        auto const& value = list[index];
        if (!value.hold<IntTag>()) return false;
        output = static_cast<int>(value.get<IntTag>());
        return true;
    };
    return read(0, x) && read(1, y) && read(2, z);
}

bool inspectBlockLayer(ListTag const& layer, std::uint64_t volume, std::uint64_t& occupied) {
    if (static_cast<std::uint64_t>(layer.size()) != volume) return false;
    occupied = 0;
    for (auto const& value : layer) {
        if (!value.hold<IntTag>()) return false;
        if (static_cast<int>(value.get<IntTag>()) >= 0) ++occupied;
    }
    return true;
}

struct JavaNbtTag {
    using ByteArray = std::vector<std::uint8_t>;
    using List = std::vector<JavaNbtTag>;
    using Compound = std::unordered_map<std::string, JavaNbtTag>;
    using IntArray = std::vector<std::int32_t>;
    using LongArray = std::vector<std::int64_t>;
    using Value = std::variant<
        std::monostate, std::int8_t, std::int16_t, std::int32_t, std::int64_t, float, double,
        ByteArray, std::string, List, Compound, IntArray, LongArray>;
    Value value;
};

class JavaNbtReader {
public:
    explicit JavaNbtReader(std::string_view bytes) : mBytes(bytes) {}

    JavaNbtTag::Compound readRoot() {
        auto const type = readU8();
        if (type != 10) throw std::runtime_error("Litematic 根标签不是 Compound");
        (void)readString();
        auto root = readPayload(type);
        if (!std::holds_alternative<JavaNbtTag::Compound>(root.value)) {
            throw std::runtime_error("Litematic 根标签无效");
        }
        return std::move(std::get<JavaNbtTag::Compound>(root.value));
    }

private:
    std::string_view mBytes;
    std::size_t mOffset{};

    void require(std::size_t count) const {
        if (count > mBytes.size() - std::min(mOffset, mBytes.size())) {
            throw std::runtime_error("Litematic NBT 数据被截断");
        }
    }

    std::uint8_t readU8() {
        require(1);
        return static_cast<std::uint8_t>(mBytes[mOffset++]);
    }

    template <class T>
    T readBigEndian() {
        require(sizeof(T));
        T value{};
        std::memcpy(&value, mBytes.data() + mOffset, sizeof(T));
        mOffset += sizeof(T);
        if constexpr (sizeof(T) > 1) {
            if constexpr (std::endian::native == std::endian::little) {
                auto* first = reinterpret_cast<std::uint8_t*>(&value);
                std::reverse(first, first + sizeof(T));
            }
        }
        return value;
    }

    std::string readString() {
        auto const length = readBigEndian<std::uint16_t>();
        require(length);
        std::string result{mBytes.substr(mOffset, length)};
        mOffset += length;
        return result;
    }

    std::size_t readArrayLength() {
        auto const length = readBigEndian<std::int32_t>();
        if (length < 0) throw std::runtime_error("Litematic NBT 数组长度为负数");
        return static_cast<std::size_t>(length);
    }

    JavaNbtTag readPayload(std::uint8_t type) {
        JavaNbtTag tag;
        switch (type) {
        case 1: tag.value = static_cast<std::int8_t>(readU8()); break;
        case 2: tag.value = readBigEndian<std::int16_t>(); break;
        case 3: tag.value = readBigEndian<std::int32_t>(); break;
        case 4: tag.value = readBigEndian<std::int64_t>(); break;
        case 5: tag.value = readBigEndian<float>(); break;
        case 6: tag.value = readBigEndian<double>(); break;
        case 7: {
            auto const count = readArrayLength();
            require(count);
            JavaNbtTag::ByteArray values(count);
            std::memcpy(values.data(), mBytes.data() + mOffset, count);
            mOffset += count;
            tag.value = std::move(values);
            break;
        }
        case 8: tag.value = readString(); break;
        case 9: {
            auto const elementType = readU8();
            auto const count = readArrayLength();
            if (count > kMaximumInflatedFileSize) throw std::runtime_error("Litematic NBT 列表过大");
            JavaNbtTag::List values;
            values.reserve(count);
            for (std::size_t i = 0; i < count; ++i) values.push_back(readPayload(elementType));
            tag.value = std::move(values);
            break;
        }
        case 10: {
            JavaNbtTag::Compound values;
            for (;;) {
                auto const childType = readU8();
                if (childType == 0) break;
                auto name = readString();
                values.insert_or_assign(std::move(name), readPayload(childType));
            }
            tag.value = std::move(values);
            break;
        }
        case 11: {
            auto const count = readArrayLength();
            if (count > kMaximumInflatedFileSize / sizeof(std::int32_t)) throw std::runtime_error("Litematic IntArray 过大");
            JavaNbtTag::IntArray values(count);
            for (auto& value : values) value = readBigEndian<std::int32_t>();
            tag.value = std::move(values);
            break;
        }
        case 12: {
            auto const count = readArrayLength();
            if (count > kMaximumInflatedFileSize / sizeof(std::int64_t)) throw std::runtime_error("Litematic LongArray 过大");
            JavaNbtTag::LongArray values(count);
            for (auto& value : values) value = readBigEndian<std::int64_t>();
            tag.value = std::move(values);
            break;
        }
        default: throw std::runtime_error("Litematic 包含不支持的 NBT 标签类型");
        }
        return tag;
    }
};

template <class T>
T const* javaValue(JavaNbtTag::Compound const& compound, std::string_view name) {
    auto const found = compound.find(std::string{name});
    if (found == compound.end()) return nullptr;
    return std::get_if<T>(&found->second.value);
}

bool readJavaVec3(JavaNbtTag::Compound const& parent, std::string_view name, int& x, int& y, int& z) {
    auto const* compound = javaValue<JavaNbtTag::Compound>(parent, name);
    if (!compound) return false;
    auto const* xValue = javaValue<std::int32_t>(*compound, "x");
    auto const* yValue = javaValue<std::int32_t>(*compound, "y");
    auto const* zValue = javaValue<std::int32_t>(*compound, "z");
    if (!xValue || !yValue || !zValue) return false;
    x = *xValue;
    y = *yValue;
    z = *zValue;
    return true;
}

std::optional<bool> readJavaBoolean(JavaNbtTag::Compound const& parent, std::string_view name) {
    if (auto const* value = javaValue<std::int8_t>(parent, name)) return *value != 0;
    return std::nullopt;
}

bool readJavaSignSide(JavaNbtTag::Compound const& parent, std::string_view name, JavaSignSideData& output) {
    auto const* side = javaValue<JavaNbtTag::Compound>(parent, name);
    if (!side) return false;
    if (auto const* messages = javaValue<JavaNbtTag::List>(*side, "messages")) {
        auto const count = std::min(messages->size(), output.messages.size());
        for (std::size_t line = 0; line < count; ++line) {
            if (auto const* message = std::get_if<std::string>(&(*messages)[line].value)) {
                output.messages[line] = *message;
            }
        }
    }
    if (auto const glowing = readJavaBoolean(*side, "has_glowing_text")) output.glowing = *glowing;
    return true;
}

std::optional<JavaSignBlockEntityData> readJavaSignBlockEntity(JavaNbtTag::Compound const& entity) {
    JavaSignBlockEntityData result;
    bool const hasModernFront = readJavaSignSide(entity, "front_text", result.front);
    bool const hasModernBack = readJavaSignSide(entity, "back_text", result.back);
    bool       hasLegacyText{};
    if (!hasModernFront) {
        static constexpr std::array<std::string_view, 4> legacyLines{"Text1", "Text2", "Text3", "Text4"};
        for (std::size_t line = 0; line < legacyLines.size(); ++line) {
            if (auto const* message = javaValue<std::string>(entity, legacyLines[line])) {
                result.front.messages[line] = *message;
                hasLegacyText = true;
            }
        }
        if (auto const glowing = readJavaBoolean(entity, "GlowingText")) result.front.glowing = *glowing;
    }
    if (auto const waxed = readJavaBoolean(entity, "is_waxed")) result.waxed = *waxed;
    return hasModernFront || hasModernBack || hasLegacyText
        ? std::optional<JavaSignBlockEntityData>{std::move(result)} : std::nullopt;
}

std::uint64_t localCellIndex(int x, int y, int z, int sizeY, int sizeZ) {
    return (static_cast<std::uint64_t>(x) * static_cast<std::uint64_t>(sizeY)
            + static_cast<std::uint64_t>(y))
        * static_cast<std::uint64_t>(sizeZ) + static_cast<std::uint64_t>(z);
}

std::optional<std::string> inflateGzip(std::string_view compressed, std::string& error) {
    if (compressed.size() < 2 || static_cast<std::uint8_t>(compressed[0]) != 0x1f
        || static_cast<std::uint8_t>(compressed[1]) != 0x8b) {
        return std::string{compressed};
    }
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(std::min<std::size_t>(compressed.size(), std::numeric_limits<uInt>::max()));
    if (inflateInit2(&stream, 15 + 16) != Z_OK) {
        error = "无法初始化 gzip 解压器";
        return std::nullopt;
    }
    struct EndInflate { z_stream* stream; ~EndInflate() { inflateEnd(stream); } } end{&stream};
    std::string output;
    std::array<char, 256 * 1024> chunk{};
    int result = Z_OK;
    while (result == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(chunk.data());
        stream.avail_out = static_cast<uInt>(chunk.size());
        result = inflate(&stream, Z_NO_FLUSH);
        auto const produced = chunk.size() - stream.avail_out;
        if (output.size() + produced > kMaximumInflatedFileSize) {
            error = "解压后的 Litematic 超过 1 GiB 限制";
            return std::nullopt;
        }
        output.append(chunk.data(), produced);
    }
    if (result != Z_STREAM_END) {
        error = std::string{"Litematic gzip 解压失败: "} + (stream.msg ? stream.msg : "未知错误");
        return std::nullopt;
    }
    return output;
}

ResolvedJavaBlock resolveJavaBlock(JavaNbtTag const& paletteEntry, int javaDataVersion) {
    auto const* compound = std::get_if<JavaNbtTag::Compound>(&paletteEntry.value);
    if (!compound) return {};
    auto const* name = javaValue<std::string>(*compound, "Name");
    if (!name || name->empty()) return {};
    // Carry the Java `Properties` (all string values) into the mapper so that
    // orientation and other stored states survive the Java -> Bedrock crossing.
    std::vector<std::pair<std::string, std::string>> properties;
    if (auto const* props = javaValue<JavaNbtTag::Compound>(*compound, "Properties")) {
        properties.reserve(props->size());
        for (auto const& [key, value] : *props) {
            if (auto const* text = std::get_if<std::string>(&value.value)) {
                properties.emplace_back(key, *text);
            }
        }
    }
    return resolveJavaBlockState(*name, properties, javaDataVersion);
}

std::uint32_t packedPaletteIndex(
    JavaNbtTag::LongArray const& values,
    std::uint64_t index,
    unsigned bits
) {
    auto const bitOffset = index * bits;
    auto const arrayIndex = static_cast<std::size_t>(bitOffset >> 6);
    auto const shift = static_cast<unsigned>(bitOffset & 63);
    if (arrayIndex >= values.size()) throw std::runtime_error("BlockStates 长度不足");
    auto packed = static_cast<std::uint64_t>(values[arrayIndex]) >> shift;
    if (shift + bits > 64) {
        if (arrayIndex + 1 >= values.size()) throw std::runtime_error("BlockStates 跨界数据不完整");
        packed |= static_cast<std::uint64_t>(values[arrayIndex + 1]) << (64 - shift);
    }
    auto const mask = bits == 32 ? 0xffffffffull : ((1ull << bits) - 1ull);
    return static_cast<std::uint32_t>(packed & mask);
}

std::shared_ptr<LoadedStructure> loadMcstructure(std::filesystem::path const& path, std::string& error) {
    auto bytes = readFile(path, error);
    if (!bytes) return nullptr;

    auto root = CompoundTag::fromBinaryNbt(*bytes, true);
    if (!root) {
        error = "不是有效的 Bedrock little-endian NBT: " + root.error().message();
        return nullptr;
    }

    auto const* size = findList(*root, "size");
    auto const* structure = findCompound(*root, "structure");
    if (!size || !structure) {
        error = "缺少 size 或 structure 标签";
        return nullptr;
    }

    auto loaded = std::make_shared<LoadedStructure>();
    if (!readThreeInts(*size, loaded->sizeX, loaded->sizeY, loaded->sizeZ)
        || loaded->sizeX <= 0 || loaded->sizeY <= 0 || loaded->sizeZ <= 0) {
        error = "结构尺寸无效";
        return nullptr;
    }
    loaded->volume = static_cast<std::uint64_t>(loaded->sizeX)
        * static_cast<std::uint64_t>(loaded->sizeY) * static_cast<std::uint64_t>(loaded->sizeZ);
    loaded->regions.push_back({0, 0, 0, loaded->sizeX, loaded->sizeY, loaded->sizeZ});

    auto const* blockIndices = findList(*structure, "block_indices");
    if (!blockIndices || blockIndices->size() < 2
        || !(*blockIndices)[0].hold<ListTag>() || !(*blockIndices)[1].hold<ListTag>()) {
        error = "block_indices 不是有效的双层索引";
        return nullptr;
    }
    if (!inspectBlockLayer((*blockIndices)[0].get<ListTag>(), loaded->volume, loaded->primaryBlocks)
        || !inspectBlockLayer((*blockIndices)[1].get<ListTag>(), loaded->volume, loaded->secondaryBlocks)) {
        error = "方块索引数量或类型与结构尺寸不匹配";
        return nullptr;
    }

    auto const* palette = findCompound(*structure, "palette");
    auto const* defaultPalette = palette ? findCompound(*palette, "default") : nullptr;
    auto const* blockPalette = defaultPalette ? findList(*defaultPalette, "block_palette") : nullptr;
    auto const* blockPositionData = defaultPalette
        ? findCompound(*defaultPalette, "block_position_data") : nullptr;
    if (!blockPalette) {
        error = "缺少 palette.default.block_palette";
        return nullptr;
    }
    loaded->paletteEntries = static_cast<std::uint64_t>(blockPalette->size());
    // Use the game's own StructureTemplate loader. Besides reading both index
    // layers, it performs the format-version block-state upgrade and resolves
    // blocks through the active world's palette/unknown-block registry. Calling
    // Block::tryGetFromRegistry() directly skips that official load pipeline and
    // can turn valid legacy states (notably water and doors) into unknown blocks.
    // StructureTemplate::create() internally uses ll::service::getLevel(),
    // which only exists for the integrated (local) server and returns null on
    // remote server connections. Build the template against the client
    // multiplayer Level instead; it exists in local worlds and on servers.
    auto const clientLevel = ll::service::getMultiPlayerLevel();
    if (!clientLevel) {
        error = "尚未进入世界，无法解析结构";
        return nullptr;
    }
    auto nativeStructure = std::make_unique<StructureTemplate>(
        "lholo:projection",
        clientLevel->getUnknownBlockTypeRegistry()
    );
    if (!nativeStructure->load(*root)) {
        error = "原版 StructureTemplate 无法加载该结构";
        return nullptr;
    }
    auto const nativeSize = nativeStructure->getSize();
    if (nativeSize.x != loaded->sizeX || nativeSize.y != loaded->sizeY || nativeSize.z != loaded->sizeZ) {
        error = "原版 StructureTemplate 返回的尺寸与文件不一致";
        return nullptr;
    }
    auto const& nativeData = nativeStructure->mStructureTemplateData.get();
    auto const* nativePalette = nativeData.getPalette(StructureTemplateData::DEFAULT_PALETTE_NAME());
    if (!nativePalette) {
        error = "原版 StructureTemplate 缺少 default palette";
        return nullptr;
    }
    auto const& nativePrimary = nativeData.getBlockIndices();
    auto const& nativeSecondary = nativeData.getExtraBlockIndices();
    if (nativePrimary.size() != loaded->volume || nativeSecondary.size() != loaded->volume) {
        error = "原版 StructureTemplate 的方块索引数量与结构体积不一致";
        return nullptr;
    }
    auto const unknownRegistry = nativeStructure->mUnknownBlockRegistry.get();
    auto const resolveNative = [&](int paletteIndex) -> Block const* {
        if (paletteIndex < 0) return nullptr;
        auto const* resolved = nativePalette->tryGetBlock(
            static_cast<std::uint64_t>(paletteIndex), unknownRegistry
        );
        return resolved && !resolved->isAir() ? resolved : nullptr;
    };
    loaded->renderBlocks.reserve(static_cast<std::size_t>(loaded->primaryBlocks + loaded->secondaryBlocks));
    auto const yz = static_cast<std::uint64_t>(loaded->sizeY) * static_cast<std::uint64_t>(loaded->sizeZ);
    for (std::uint64_t index = 0; index < loaded->volume; ++index) {
        auto const* primary = resolveNative(nativePrimary[static_cast<std::size_t>(index)]);
        auto const* secondary = resolveNative(nativeSecondary[static_cast<std::size_t>(index)]);
        Block const* block{};
        Block const* liquid{};
        auto const assign = [&](Block const* value) {
            if (!value) return;
            if (value->getMaterial().isLiquid()) liquid = value;
            else if (!block) block = value;
        };
        assign(primary);
        assign(secondary);
        if (!block && !liquid) continue;
        auto const x = index / yz;
        auto const remainder = index % yz;
        auto const y = remainder / static_cast<std::uint64_t>(loaded->sizeZ);
        auto const z = remainder % static_cast<std::uint64_t>(loaded->sizeZ);
        std::shared_ptr<CompoundTag const> blockEntityNbt;
        if (blockPositionData) {
            auto const* positionData = findCompound(*blockPositionData, std::to_string(index));
            auto const* entityData = positionData
                ? findCompound(*positionData, "block_entity_data") : nullptr;
            if (entityData) blockEntityNbt = std::make_shared<CompoundTag const>(*entityData);
        }
        loaded->renderBlocks.push_back({
            static_cast<int>(x), static_cast<int>(y), static_cast<int>(z), block, liquid,
            std::move(blockEntityNbt)
        });
    }
    assignMaterialIndices(*loaded);
    loaded->generation = gGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    loaded->sourcePath = path;
    return loaded;
}

std::shared_ptr<LoadedStructure> loadLitematic(std::filesystem::path const& path, std::string& error) try {
    if (!ll::service::getMultiPlayerLevel()) {
        error = "尚未进入世界，无法解析结构";
        return nullptr;
    }

    // The Java-to-Bedrock mapper caches non-owning Block pointers while resolving one
    // palette. Minecraft rebuilds those registry-owned permutations across a
    // world teardown, so a cache from the previous world must never be reused.
    resetJavaBlockMappingCache();

    auto compressed = readFile(path, error);
    if (!compressed) return nullptr;
    auto bytes = inflateGzip(*compressed, error);
    if (!bytes) return nullptr;

    auto root = JavaNbtReader{*bytes}.readRoot();
    auto const* storedFormatVersion = javaValue<std::int32_t>(root, "Version");
    int const litematicVersion = storedFormatVersion ? *storedFormatVersion : 0;
    auto const* storedDataVersion = javaValue<std::int32_t>(root, "MinecraftDataVersion");
    int const javaDataVersion = storedDataVersion ? *storedDataVersion : 0;
    auto const* regions = javaValue<JavaNbtTag::Compound>(root, "Regions");
    if (!regions || regions->empty()) {
        error = "Litematic 缺少 Regions 或没有区域";
        return nullptr;
    }

    struct Region {
        int posX{}, posY{}, posZ{};
        int signedX{}, signedY{}, signedZ{};
        int sizeX{}, sizeY{}, sizeZ{};
        std::vector<ResolvedJavaBlock> palette;
        JavaNbtTag::LongArray const* states{};
        std::unordered_map<std::uint64_t, JavaNbtTag::Compound const*> blockEntities;
    };
    std::vector<Region> parsedRegions;
    parsedRegions.reserve(regions->size());
    std::int64_t minX = std::numeric_limits<std::int64_t>::max();
    std::int64_t minY = minX;
    std::int64_t minZ = minX;
    std::int64_t maxX = std::numeric_limits<std::int64_t>::min();
    std::int64_t maxY = maxX;
    std::int64_t maxZ = maxX;
    std::uint64_t paletteEntries{};

    for (auto const& [regionName, regionTag] : *regions) {
        auto const* compound = std::get_if<JavaNbtTag::Compound>(&regionTag.value);
        if (!compound) continue;
        Region region;
        if (!readJavaVec3(*compound, "Position", region.posX, region.posY, region.posZ)
            || !readJavaVec3(*compound, "Size", region.signedX, region.signedY, region.signedZ)
            || region.signedX == 0 || region.signedY == 0 || region.signedZ == 0
            || region.signedX == std::numeric_limits<int>::min()
            || region.signedY == std::numeric_limits<int>::min()
            || region.signedZ == std::numeric_limits<int>::min()) {
            error = "Litematic 区域 " + regionName + " 的 Position/Size 无效";
            return nullptr;
        }
        region.sizeX = std::abs(region.signedX);
        region.sizeY = std::abs(region.signedY);
        region.sizeZ = std::abs(region.signedZ);
        auto const* storedRegionDataVersion = javaValue<std::int32_t>(*compound, "DataVersion");
        int const regionDataVersion = storedRegionDataVersion ? *storedRegionDataVersion : javaDataVersion;
        auto const* palette = javaValue<JavaNbtTag::List>(*compound, "BlockStatePalette");
        region.states = javaValue<JavaNbtTag::LongArray>(*compound, "BlockStates");
        if (!palette || palette->empty() || !region.states || region.states->empty()) {
            error = "Litematic 区域 " + regionName + " 缺少方块调色板或 BlockStates";
            return nullptr;
        }
        region.palette.reserve(palette->size());
        for (auto const& entry : *palette) {
            region.palette.push_back(resolveJavaBlock(entry, regionDataVersion));
        }
        paletteEntries += palette->size();

        if (auto const* blockEntities = javaValue<JavaNbtTag::List>(*compound, "TileEntities")) {
            region.blockEntities.reserve(blockEntities->size());
            for (auto const& entry : *blockEntities) {
                auto const* wrapper = std::get_if<JavaNbtTag::Compound>(&entry.value);
                if (!wrapper) continue;
                auto const* x = javaValue<std::int32_t>(*wrapper, "x");
                auto const* y = javaValue<std::int32_t>(*wrapper, "y");
                auto const* z = javaValue<std::int32_t>(*wrapper, "z");
                if (!x || !y || !z || *x < 0 || *y < 0 || *z < 0
                    || *x >= region.sizeX || *y >= region.sizeY || *z >= region.sizeZ) {
                    continue;
                }
                auto const* entity = litematicVersion == 1
                    ? javaValue<JavaNbtTag::Compound>(*wrapper, "TileNBT") : wrapper;
                if (!entity || entity->empty()) continue;
                region.blockEntities.insert_or_assign(
                    localCellIndex(*x, *y, *z, region.sizeY, region.sizeZ), entity
                );
            }
        }

        auto const endX = static_cast<std::int64_t>(region.posX)
            + (region.signedX < 0 ? -(static_cast<std::int64_t>(region.sizeX) - 1) : region.sizeX - 1);
        auto const endY = static_cast<std::int64_t>(region.posY)
            + (region.signedY < 0 ? -(static_cast<std::int64_t>(region.sizeY) - 1) : region.sizeY - 1);
        auto const endZ = static_cast<std::int64_t>(region.posZ)
            + (region.signedZ < 0 ? -(static_cast<std::int64_t>(region.sizeZ) - 1) : region.sizeZ - 1);
        minX = std::min({minX, static_cast<std::int64_t>(region.posX), endX});
        minY = std::min({minY, static_cast<std::int64_t>(region.posY), endY});
        minZ = std::min({minZ, static_cast<std::int64_t>(region.posZ), endZ});
        maxX = std::max({maxX, static_cast<std::int64_t>(region.posX), endX});
        maxY = std::max({maxY, static_cast<std::int64_t>(region.posY), endY});
        maxZ = std::max({maxZ, static_cast<std::int64_t>(region.posZ), endZ});
        parsedRegions.push_back(std::move(region));
    }
    if (parsedRegions.empty()) {
        error = "Litematic 没有可加载的区域";
        return nullptr;
    }

    auto const extentX = maxX - minX + 1;
    auto const extentY = maxY - minY + 1;
    auto const extentZ = maxZ - minZ + 1;
    if (extentX <= 0 || extentY <= 0 || extentZ <= 0
        || extentX > std::numeric_limits<int>::max()
        || extentY > std::numeric_limits<int>::max()
        || extentZ > std::numeric_limits<int>::max()) {
        error = "Litematic 合并后的结构尺寸无效或过大";
        return nullptr;
    }

    auto loaded = std::make_shared<LoadedStructure>();
    loaded->sizeX = static_cast<int>(extentX);
    loaded->sizeY = static_cast<int>(extentY);
    loaded->sizeZ = static_cast<int>(extentZ);
    loaded->volume = static_cast<std::uint64_t>(extentX)
        * static_cast<std::uint64_t>(extentY) * static_cast<std::uint64_t>(extentZ);
    loaded->paletteEntries = paletteEntries;
    loaded->regions.reserve(parsedRegions.size());
    struct MergedJavaCell {
        ResolvedJavaBlock                 block;
        std::shared_ptr<CompoundTag const> blockEntityNbt;
    };
    std::unordered_map<std::uint64_t, MergedJavaCell> mergedBlocks;

    for (auto const& region : parsedRegions) {
        auto const regionVolume = static_cast<std::uint64_t>(region.sizeX)
            * static_cast<std::uint64_t>(region.sizeY) * static_cast<std::uint64_t>(region.sizeZ);
        // Litematica stores BlockStates from the region's minimum corner even
        // when Size is negative. The sign only records which selection corner
        // is Position; it must not mirror the block data.
        auto const regionMinX = static_cast<std::int64_t>(region.posX)
            - (region.signedX < 0 ? static_cast<std::int64_t>(region.sizeX) - 1 : 0);
        auto const regionMinY = static_cast<std::int64_t>(region.posY)
            - (region.signedY < 0 ? static_cast<std::int64_t>(region.sizeY) - 1 : 0);
        auto const regionMinZ = static_cast<std::int64_t>(region.posZ)
            - (region.signedZ < 0 ? static_cast<std::int64_t>(region.sizeZ) - 1 : 0);
        loaded->regions.push_back({
            static_cast<int>(regionMinX - minX),
            static_cast<int>(regionMinY - minY),
            static_cast<int>(regionMinZ - minZ),
            region.sizeX,
            region.sizeY,
            region.sizeZ,
        });
        auto const bits = std::max<unsigned>(
            2u,
            static_cast<unsigned>(std::bit_width(static_cast<unsigned>(region.palette.size() - 1)))
        );
        auto const requiredLongs = (regionVolume * bits + 63) / 64;
        if (requiredLongs > region.states->size()) {
            error = "Litematic 的 BlockStates 数量与区域尺寸不匹配";
            return nullptr;
        }
        for (std::uint64_t index = 0; index < regionVolume; ++index) {
            auto const paletteIndex = packedPaletteIndex(*region.states, index, bits);
            if (paletteIndex >= region.palette.size()) continue;
            auto const& resolved = region.palette[paletteIndex];
            if (!resolved.block && !resolved.liquid) continue;
            auto const layer = static_cast<std::uint64_t>(region.sizeX) * region.sizeZ;
            auto const localY = index / layer;
            auto const remainder = index % layer;
            auto const localZ = remainder / static_cast<std::uint64_t>(region.sizeX);
            auto const localX = remainder % static_cast<std::uint64_t>(region.sizeX);
            auto const worldX = regionMinX + static_cast<std::int64_t>(localX);
            auto const worldY = regionMinY + static_cast<std::int64_t>(localY);
            auto const worldZ = regionMinZ + static_cast<std::int64_t>(localZ);
            auto const x = static_cast<std::uint64_t>(worldX - minX);
            auto const y = static_cast<std::uint64_t>(worldY - minY);
            auto const z = static_cast<std::uint64_t>(worldZ - minZ);
            auto const mergedIndex = (x * static_cast<std::uint64_t>(loaded->sizeY) + y)
                * static_cast<std::uint64_t>(loaded->sizeZ) + z;
            std::shared_ptr<CompoundTag const> blockEntityNbt;
            auto const blockEntity = region.blockEntities.find(
                localCellIndex(
                    static_cast<int>(localX), static_cast<int>(localY), static_cast<int>(localZ),
                    region.sizeY, region.sizeZ
                )
            );
            if (resolved.block && blockEntity != region.blockEntities.end()) {
                auto const actorType = resolved.block->getBlockEntityType();
                if (actorType == BlockActorType::Sign || actorType == BlockActorType::HangingSign) {
                    if (auto const signData = readJavaSignBlockEntity(*blockEntity->second)) {
                        blockEntityNbt = convertJavaSignBlockEntity(
                            *signData,
                            actorType == BlockActorType::HangingSign
                                ? JavaSignBlockEntityKind::HangingSign : JavaSignBlockEntityKind::Sign,
                            static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)
                        );
                    }
                }
            }
            mergedBlocks.insert_or_assign(
                mergedIndex, MergedJavaCell{resolved, std::move(blockEntityNbt)}
            );
        }
    }
    loaded->renderBlocks.reserve(mergedBlocks.size());
    auto const yz = static_cast<std::uint64_t>(loaded->sizeY) * loaded->sizeZ;
    for (auto& [index, cell] : mergedBlocks) {
        auto const x = index / yz;
        auto const remainder = index % yz;
        auto const y = remainder / static_cast<std::uint64_t>(loaded->sizeZ);
        auto const z = remainder % static_cast<std::uint64_t>(loaded->sizeZ);
        // The Java -> Bedrock mapper already split each cell into its body and
        // liquid layers (pure liquids, and the water that waterlogged blocks
        // carry), matching the two-layer semantics of the .mcstructure path.
        loaded->renderBlocks.push_back({
            static_cast<int>(x), static_cast<int>(y), static_cast<int>(z),
            cell.block.block, cell.block.liquid, std::move(cell.blockEntityNbt)
        });
    }
    std::sort(loaded->renderBlocks.begin(), loaded->renderBlocks.end(), [](auto const& left, auto const& right) {
        return std::tie(left.x, left.y, left.z) < std::tie(right.x, right.y, right.z);
    });
    loaded->primaryBlocks = loaded->renderBlocks.size();
    assignMaterialIndices(*loaded);
    loaded->generation = gGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    loaded->sourcePath = path;
    return loaded;
} catch (std::exception const& exception) {
    error = exception.what();
    return nullptr;
}

} // namespace

std::shared_ptr<LoadedStructure> loadStructureFile(std::filesystem::path const& path, std::string& error) {
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    std::shared_ptr<LoadedStructure> loaded;
    if (extension == L".litematic") loaded = loadLitematic(path, error);
    else if (extension == L".mcstructure") loaded = loadMcstructure(path, error);
    else {
        error = "不支持的文件格式，请选择 .mcstructure 或 .litematic";
        return nullptr;
    }
    if (loaded && loaded->renderBlocks.empty()) {
        error = "结构中没有可投影方块";
        return nullptr;
    }
    return loaded;
}

std::string makeStructureStatus(LoadedStructure const& loaded) {
    std::ostringstream output;
    output << "已加载: " << loaded.sourcePath.filename().string() << "\n"
           << "尺寸 " << loaded.sizeX << " x " << loaded.sizeY << " x " << loaded.sizeZ
           << "  |  方块 " << loaded.renderBlocks.size()
           << "  |  Palette " << loaded.paletteEntries;
    return output.str();
}

} // namespace lholo::structure::detail
