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

#include "structure/StructureLoader.h"

#include "structure/JavaBlockMapping.h"
#include "place/PlaceHelper.h"
#include "plugin/LHolo.h"
#include "projection/Projection.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>
#include <nlohmann/json.hpp>
#include <zlib.h>

#include <Windows.h>
#include <commdlg.h>

#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/Bedrock.h"
#include "imgui.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/registry/BlockTypeRegistry.h"
#include "mc/world/level/material/Material.h"
#include "mc/world/level/levelgen/structure/StructureBlockPalette.h"
#include "mc/world/level/levelgen/structure/StructureTemplate.h"

namespace lholo::structure {
namespace {

constexpr std::uintmax_t kMaximumStructureFileSize = 512ull * 1024ull * 1024ull;
constexpr std::size_t    kMaximumInflatedFileSize  = 1024ull * 1024ull * 1024ull;
constexpr unsigned int   kHotkeyModifierControl    = 1u;
constexpr unsigned int   kHotkeyModifierAlt        = 2u;
constexpr unsigned int   kHotkeyModifierShift      = 4u;

std::atomic_bool                 gGuiVisible{false};
std::atomic_int                  gOpeningInputBlockFrames{0};
std::atomic_uint64_t             gBlockGameInputUntil{};
std::mutex                       gLoadedMutex;
std::shared_ptr<LoadedStructure> gLoaded;
std::atomic_uint64_t             gGeneration{0};
std::atomic_int                  gRotationQuarterTurns{0};
std::atomic_int                  gMirrorMode{0};
std::atomic_int                  gOffsetX{0};
std::atomic_int                  gOffsetY{0};
std::atomic_int                  gOffsetZ{0};
std::atomic_int                  gLayerDisplayMode{0};
std::atomic_int                  gDisplayLayer{0};
std::atomic_int                  gLayerAxis{0};
std::atomic_bool                 gHudEnabled{true};
std::atomic_bool                 gHudShowFileName{true};
std::atomic_bool                 gHudShowLayer{true};
std::atomic_bool                 gHudShowProgress{true};
std::atomic_bool                 gHudShowWrongState{true};
std::atomic_bool                 gHudShowWrongType{true};
std::atomic_bool                 gHudShowBlockEntity{true};
// 0: left-top, 1: left-bottom (default), 2: right-top, 3: right-bottom.
std::atomic_int                  gHudPosition{1};
std::atomic<float>              gUiScale{0.0f};
std::atomic_uint                 gGuiHotkey{'M'};
std::atomic_uint                 gGuiHotkeyModifiers{kHotkeyModifierAlt};
std::atomic_bool                 gCapturingGuiHotkey{false};
std::atomic_bool                 gGuiHotkeyHeld{false};
std::atomic_uint                 gLayerIncreaseHotkey{VK_UP};
std::atomic_uint                 gLayerDecreaseHotkey{VK_DOWN};
std::atomic_uint                 gLayerIncreaseHotkeyModifiers{kHotkeyModifierAlt};
std::atomic_uint                 gLayerDecreaseHotkeyModifiers{kHotkeyModifierAlt};
std::atomic_bool                 gCapturingLayerIncreaseHotkey{false};
std::atomic_bool                 gCapturingLayerDecreaseHotkey{false};
std::atomic_bool                 gLayerIncreaseHotkeyHeld{false};
std::atomic_bool                 gLayerDecreaseHotkeyHeld{false};
std::array<std::atomic_uint, 6>  gMoveHotkeys{VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_UP, VK_DOWN};
std::array<std::atomic_uint, 6>  gMoveHotkeyModifiers{
    kHotkeyModifierControl,
    kHotkeyModifierControl,
    kHotkeyModifierControl,
    kHotkeyModifierControl,
    kHotkeyModifierShift,
    kHotkeyModifierShift
};
std::array<std::atomic_bool, 6>  gCapturingMoveHotkey{};
std::array<std::atomic_bool, 6>  gMoveHotkeyHeld{};
std::atomic_bool                 gControlHeld{false};
std::atomic_bool                 gAltHeld{false};
std::atomic_bool                 gShiftHeld{false};
std::array<std::atomic_uint64_t, 256> gConsumeKeyReleaseUntil{};
std::atomic_int                  gPendingOffsetX{0};
std::atomic_int                  gPendingOffsetY{0};
std::atomic_int                  gPendingOffsetZ{0};
std::atomic_int                  gPendingLayerDelta{0};
std::atomic_bool                 gPendingSettingsSave{false};
std::atomic_uint64_t             gIgnoreHotkeyUntil{0};
std::atomic_bool                 gHasSavedProjection{false};
std::atomic_int                  gSavedAnchorX{0};
std::atomic_int                  gSavedAnchorY{0};
std::atomic_int                  gSavedAnchorZ{0};
std::atomic_int                  gSavedRotation{0};
std::atomic_int                  gSavedMirror{0};
std::atomic_int                  gSavedOffsetX{0};
std::atomic_int                  gSavedOffsetY{0};
std::atomic_int                  gSavedOffsetZ{0};
std::atomic_int                  gSavedLayerDisplayMode{0};
std::atomic_int                  gSavedDisplayLayer{0};
std::atomic_int                  gSavedLayerAxis{0};
std::string                      gSavedStructurePath;
std::string                      gLastPath;
std::string                      gStatus = "尚未加载结构文件";

int maxLayerFor(LoadedStructure const& structure, int axis) {
    return std::max(0, (axis == 1 ? structure.sizeX : structure.sizeY) - 1);
}

auto& logger() {
    return LHolo::getInstance().getSelf().getLogger();
}

std::filesystem::path settingsPath() {
    return LHolo::getInstance().getSelf().getConfigDir() / "config.json";
}

std::filesystem::path pathFromUtf8(std::string_view value) {
    if (value.empty()) return {};
    auto const wideSize = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0
    );
    if (wideSize <= 0) return std::filesystem::path{value};

    std::wstring wide(static_cast<std::size_t>(wideSize), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            wide.data(),
            wideSize
        ) <= 0) {
        return std::filesystem::path{value};
    }
    return std::filesystem::path{wide};
}

std::string pathToUtf8(std::filesystem::path const& path) {
    auto const& wide = path.native();
    if (wide.empty()) return {};
    auto const size = WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr
    );
    if (size <= 0) return path.string();
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), size, nullptr, nullptr
    );
    return result;
}

std::string wideToUtf8(std::wstring_view value) {
    if (value.empty()) return {};
    auto const size = WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr
    );
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr
    );
    return result;
}

bool isModifierKey(unsigned int key) {
    return key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU
        || key == VK_LSHIFT || key == VK_RSHIFT
        || key == VK_LCONTROL || key == VK_RCONTROL
        || key == VK_LMENU || key == VK_RMENU
        || key == VK_LWIN || key == VK_RWIN;
}

std::string hotkeyName(unsigned int key) {
    if (key == 0) return "未设置";
    switch (key) {
    case VK_BACK: return "Backspace";
    case VK_DELETE: return "Delete";
    case VK_ESCAPE: return "Esc";
    case VK_RETURN: return "Enter";
    case VK_SPACE: return "Space";
    case VK_TAB: return "Tab";
    case VK_LEFT: return "Left";
    case VK_RIGHT: return "Right";
    case VK_UP: return "Up";
    case VK_DOWN: return "Down";
    default: break;
    }
    auto scanCode = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
    if (key == VK_LEFT || key == VK_UP || key == VK_RIGHT || key == VK_DOWN
        || key == VK_PRIOR || key == VK_NEXT || key == VK_END || key == VK_HOME
        || key == VK_INSERT || key == VK_DELETE || key == VK_DIVIDE || key == VK_NUMLOCK) {
        scanCode |= 1u << 24;
    }
    wchar_t name[128]{};
    auto const length = GetKeyNameTextW(static_cast<LONG>(scanCode << 16), name, static_cast<int>(std::size(name)));
    if (length > 0) return wideToUtf8(std::wstring_view{name, static_cast<std::size_t>(length)});
    char fallback[24]{};
    std::snprintf(fallback, sizeof(fallback), "VK 0x%02X", key);
    return fallback;
}

unsigned int currentHotkeyModifiers() {
    unsigned int modifiers{};
    if (gControlHeld.load(std::memory_order_acquire)) modifiers |= kHotkeyModifierControl;
    if (gAltHeld.load(std::memory_order_acquire)) modifiers |= kHotkeyModifierAlt;
    if (gShiftHeld.load(std::memory_order_acquire)) modifiers |= kHotkeyModifierShift;
    return modifiers;
}

std::string hotkeyChordName(unsigned int modifiers, unsigned int key) {
    if (key == 0) return "未设置";
    std::string result;
    if ((modifiers & kHotkeyModifierControl) != 0) result += "Ctrl + ";
    if ((modifiers & kHotkeyModifierAlt) != 0) result += "Alt + ";
    if ((modifiers & kHotkeyModifierShift) != 0) result += "Shift + ";
    result += hotkeyName(key);
    return result;
}

std::optional<std::filesystem::path> browseStructureFile(std::filesystem::path const& current) {
    std::vector<wchar_t> buffer(32768, L'\0');
    if (!current.empty()) {
        auto const value = current.native();
        std::copy_n(value.data(), std::min(value.size(), buffer.size() - 1), buffer.data());
    }

    wchar_t const filter[] =
        L"投影结构 (*.mcstructure;*.litematic)\0*.mcstructure;*.litematic\0"
        L"Bedrock 结构 (*.mcstructure)\0*.mcstructure\0"
        L"Litematica 结构 (*.litematic)\0*.litematic\0"
        L"所有文件 (*.*)\0*.*\0\0";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrFilter = filter;
    dialog.nFilterIndex = 1;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    dialog.lpstrDefExt = L"mcstructure";
    if (!GetOpenFileNameW(&dialog)) return std::nullopt;
    return std::filesystem::path{buffer.data()};
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

ResolvedJavaBlock resolveJavaBlock(JavaNbtTag const& paletteEntry) {
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
    return resolveJavaBlockState(*name, properties);
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
        loaded->renderBlocks.push_back({
            static_cast<int>(x), static_cast<int>(y), static_cast<int>(z), block, liquid
        });
    }
    loaded->generation = gGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    loaded->sourcePath = path;
    loaded->rootTag = std::make_unique<CompoundTag>(std::move(*root));
    return loaded;
}

std::shared_ptr<LoadedStructure> loadLitematic(std::filesystem::path const& path, std::string& error) try {
    auto compressed = readFile(path, error);
    if (!compressed) return nullptr;
    auto bytes = inflateGzip(*compressed, error);
    if (!bytes) return nullptr;

    auto root = JavaNbtReader{*bytes}.readRoot();
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
        auto const* palette = javaValue<JavaNbtTag::List>(*compound, "BlockStatePalette");
        region.states = javaValue<JavaNbtTag::LongArray>(*compound, "BlockStates");
        if (!palette || palette->empty() || !region.states || region.states->empty()) {
            error = "Litematic 区域 " + regionName + " 缺少方块调色板或 BlockStates";
            return nullptr;
        }
        region.palette.reserve(palette->size());
        for (auto const& entry : *palette) region.palette.push_back(resolveJavaBlock(entry));
        paletteEntries += palette->size();

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
    std::unordered_map<std::uint64_t, ResolvedJavaBlock> mergedBlocks;

    for (auto const& region : parsedRegions) {
        auto const regionVolume = static_cast<std::uint64_t>(region.sizeX)
            * static_cast<std::uint64_t>(region.sizeY) * static_cast<std::uint64_t>(region.sizeZ);
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
            auto const worldX = static_cast<std::int64_t>(region.posX)
                + (region.signedX < 0 ? -static_cast<std::int64_t>(localX) : static_cast<std::int64_t>(localX));
            auto const worldY = static_cast<std::int64_t>(region.posY)
                + (region.signedY < 0 ? -static_cast<std::int64_t>(localY) : static_cast<std::int64_t>(localY));
            auto const worldZ = static_cast<std::int64_t>(region.posZ)
                + (region.signedZ < 0 ? -static_cast<std::int64_t>(localZ) : static_cast<std::int64_t>(localZ));
            auto const x = static_cast<std::uint64_t>(worldX - minX);
            auto const y = static_cast<std::uint64_t>(worldY - minY);
            auto const z = static_cast<std::uint64_t>(worldZ - minZ);
            auto const mergedIndex = (x * static_cast<std::uint64_t>(loaded->sizeY) + y)
                * static_cast<std::uint64_t>(loaded->sizeZ) + z;
            mergedBlocks.insert_or_assign(mergedIndex, resolved);
        }
    }
    loaded->renderBlocks.reserve(mergedBlocks.size());
    auto const yz = static_cast<std::uint64_t>(loaded->sizeY) * loaded->sizeZ;
    for (auto const& [index, resolved] : mergedBlocks) {
        auto const x = index / yz;
        auto const remainder = index % yz;
        auto const y = remainder / static_cast<std::uint64_t>(loaded->sizeZ);
        auto const z = remainder % static_cast<std::uint64_t>(loaded->sizeZ);
        // The Java -> Bedrock mapper already split each cell into its body and
        // liquid layers (pure liquids, and the water that waterlogged blocks
        // carry), matching the two-layer semantics of the .mcstructure path.
        loaded->renderBlocks.push_back({
            static_cast<int>(x), static_cast<int>(y), static_cast<int>(z),
            resolved.block, resolved.liquid
        });
    }
    std::sort(loaded->renderBlocks.begin(), loaded->renderBlocks.end(), [](auto const& left, auto const& right) {
        return std::tie(left.x, left.y, left.z) < std::tie(right.x, right.y, right.z);
    });
    loaded->primaryBlocks = loaded->renderBlocks.size();
    loaded->generation = gGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    loaded->sourcePath = path;
    return loaded;
} catch (std::exception const& exception) {
    error = exception.what();
    return nullptr;
}

std::shared_ptr<LoadedStructure> loadStructure(std::filesystem::path const& path, std::string& error) {
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    if (extension == L".litematic") return loadLitematic(path, error);
    if (extension == L".mcstructure") return loadMcstructure(path, error);
    error = "不支持的文件格式，请选择 .mcstructure 或 .litematic";
    return nullptr;
}

std::string makeStatus(LoadedStructure const& loaded) {
    std::ostringstream output;
    output << "已加载: " << loaded.sourcePath.filename().string() << "\n"
           << "尺寸 " << loaded.sizeX << " x " << loaded.sizeY << " x " << loaded.sizeZ
           << "  |  方块 " << loaded.renderBlocks.size()
           << "  |  Palette " << loaded.paletteEntries;
    return output.str();
}

} // namespace

void requestOpenGui() {
    auto const opening = !gGuiVisible.load(std::memory_order_acquire);
    gGuiVisible.store(opening, std::memory_order_release);
    if (opening) {
        gOpeningInputBlockFrames.store(3, std::memory_order_release);
    } else {
        // Consume the release half of the key/click that closed the menu.
        // Without this, Minecraft receives an unmatched Esc or mouse-up after
        // the ImGui window has already disappeared.
        gBlockGameInputUntil.store(GetTickCount64() + 180, std::memory_order_release);
    }
}

bool isGuiVisible() { return gGuiVisible.load(std::memory_order_acquire); }

bool isInputTransitionBlocked() {
    return GetTickCount64() <= gBlockGameInputUntil.load(std::memory_order_acquire);
}

bool handleGuiHotkeyKeyDown(unsigned int virtualKey) {
    auto const modifierKey = isModifierKey(virtualKey);
    if (virtualKey == VK_CONTROL || virtualKey == VK_LCONTROL || virtualKey == VK_RCONTROL) {
        gControlHeld.store(true, std::memory_order_release);
    } else if (virtualKey == VK_MENU || virtualKey == VK_LMENU || virtualKey == VK_RMENU) {
        gAltHeld.store(true, std::memory_order_release);
    } else if (virtualKey == VK_SHIFT || virtualKey == VK_LSHIFT || virtualKey == VK_RSHIFT) {
        gShiftHeld.store(true, std::memory_order_release);
    }

    std::atomic_uint* captureKey{};
    std::atomic_uint* captureModifiers{};
    if (gCapturingGuiHotkey.load(std::memory_order_acquire)) {
        captureKey = &gGuiHotkey;
        captureModifiers = &gGuiHotkeyModifiers;
    } else if (gCapturingLayerIncreaseHotkey.load(std::memory_order_acquire)) {
        captureKey = &gLayerIncreaseHotkey;
        captureModifiers = &gLayerIncreaseHotkeyModifiers;
    } else if (gCapturingLayerDecreaseHotkey.load(std::memory_order_acquire)) {
        captureKey = &gLayerDecreaseHotkey;
        captureModifiers = &gLayerDecreaseHotkeyModifiers;
    } else {
        for (std::size_t index = 0; index < gCapturingMoveHotkey.size(); ++index) {
            if (gCapturingMoveHotkey[index].load(std::memory_order_acquire)) {
                captureKey = &gMoveHotkeys[index];
                captureModifiers = &gMoveHotkeyModifiers[index];
                break;
            }
        }
    }
    if (captureKey) {
        // F11 belongs to Minecraft's fullscreen toggle. Never capture or
        // consume it as a mod shortcut, including while rebinding controls.
        if (virtualKey == VK_F11) return false;
        auto stopCapturing = [] {
            gCapturingGuiHotkey.store(false, std::memory_order_release);
            gCapturingLayerIncreaseHotkey.store(false, std::memory_order_release);
            gCapturingLayerDecreaseHotkey.store(false, std::memory_order_release);
            for (auto& capturing : gCapturingMoveHotkey) {
                capturing.store(false, std::memory_order_release);
            }
        };
        if (virtualKey == VK_ESCAPE) {
            stopCapturing();
        } else if (virtualKey == VK_DELETE || virtualKey == VK_BACK) {
            captureKey->store(0, std::memory_order_release);
            captureModifiers->store(0, std::memory_order_release);
            stopCapturing();
            gPendingSettingsSave.store(true, std::memory_order_release);
        } else if (!modifierKey) {
            auto const modifiers = currentHotkeyModifiers();
            auto clearDuplicate = [captureKey, captureModifiers, virtualKey, modifiers](
                                      std::atomic_uint& key,
                                      std::atomic_uint& keyModifiers
                                  ) {
                if (&key == captureKey && &keyModifiers == captureModifiers) return;
                if (key.load(std::memory_order_relaxed) == virtualKey
                    && keyModifiers.load(std::memory_order_relaxed) == modifiers) {
                    key.store(0, std::memory_order_relaxed);
                    keyModifiers.store(0, std::memory_order_relaxed);
                }
            };
            clearDuplicate(gGuiHotkey, gGuiHotkeyModifiers);
            clearDuplicate(gLayerIncreaseHotkey, gLayerIncreaseHotkeyModifiers);
            clearDuplicate(gLayerDecreaseHotkey, gLayerDecreaseHotkeyModifiers);
            for (std::size_t index = 0; index < gMoveHotkeys.size(); ++index) {
                clearDuplicate(gMoveHotkeys[index], gMoveHotkeyModifiers[index]);
            }
            captureKey->store(virtualKey, std::memory_order_release);
            captureModifiers->store(modifiers, std::memory_order_release);
            stopCapturing();
            gIgnoreHotkeyUntil.store(GetTickCount64() + 250, std::memory_order_release);
            gPendingSettingsSave.store(true, std::memory_order_release);
        }
        return true;
    }

    if (modifierKey) return false;

    auto const modifiers = currentHotkeyModifiers();
    auto const hotkey = gGuiHotkey.load(std::memory_order_acquire);
    if (hotkey != 0 && virtualKey == hotkey
        && modifiers == gGuiHotkeyModifiers.load(std::memory_order_acquire)) {
        if (GetTickCount64() >= gIgnoreHotkeyUntil.load(std::memory_order_acquire)
            && !gGuiHotkeyHeld.exchange(true, std::memory_order_acq_rel)) {
            requestOpenGui();
        }
        return true;
    }
    if (isGuiVisible()) return false;

    for (std::size_t index = 0; index < gMoveHotkeys.size(); ++index) {
        if (gMoveHotkeys[index].load(std::memory_order_acquire) == virtualKey
            && gMoveHotkeyModifiers[index].load(std::memory_order_acquire) == modifiers) {
            if (GetTickCount64() >= gIgnoreHotkeyUntil.load(std::memory_order_acquire)
                && !gMoveHotkeyHeld[index].exchange(true, std::memory_order_acq_rel)) {
                switch (index) {
                case 0: gPendingOffsetX.fetch_sub(1, std::memory_order_relaxed); break;
                case 1: gPendingOffsetX.fetch_add(1, std::memory_order_relaxed); break;
                case 2: gPendingOffsetZ.fetch_sub(1, std::memory_order_relaxed); break;
                case 3: gPendingOffsetZ.fetch_add(1, std::memory_order_relaxed); break;
                case 4: gPendingOffsetY.fetch_add(1, std::memory_order_relaxed); break;
                case 5: gPendingOffsetY.fetch_sub(1, std::memory_order_relaxed); break;
                default: break;
                }
            }
            return true;
        }
    }

    auto const layerIncreaseHotkey = gLayerIncreaseHotkey.load(std::memory_order_acquire);
    if (layerIncreaseHotkey != 0 && virtualKey == layerIncreaseHotkey
        && modifiers == gLayerIncreaseHotkeyModifiers.load(std::memory_order_acquire)) {
        if (gLayerDisplayMode.load(std::memory_order_acquire) == 0) return false;
        if (GetTickCount64() >= gIgnoreHotkeyUntil.load(std::memory_order_acquire)
            && !gLayerIncreaseHotkeyHeld.exchange(true, std::memory_order_acq_rel)) {
            gPendingLayerDelta.fetch_add(1, std::memory_order_relaxed);
        }
        return true;
    }
    auto const layerDecreaseHotkey = gLayerDecreaseHotkey.load(std::memory_order_acquire);
    if (layerDecreaseHotkey != 0 && virtualKey == layerDecreaseHotkey
        && modifiers == gLayerDecreaseHotkeyModifiers.load(std::memory_order_acquire)) {
        if (gLayerDisplayMode.load(std::memory_order_acquire) == 0) return false;
        if (GetTickCount64() >= gIgnoreHotkeyUntil.load(std::memory_order_acquire)
            && !gLayerDecreaseHotkeyHeld.exchange(true, std::memory_order_acq_rel)) {
            gPendingLayerDelta.fetch_sub(1, std::memory_order_relaxed);
        }
        return true;
    }
    return false;
}

bool handleGuiHotkeyKeyUp(unsigned int virtualKey) {
    if (virtualKey == VK_CONTROL || virtualKey == VK_LCONTROL || virtualKey == VK_RCONTROL) {
        gControlHeld.store(false, std::memory_order_release);
        return false;
    }
    if (virtualKey == VK_MENU || virtualKey == VK_LMENU || virtualKey == VK_RMENU) {
        gAltHeld.store(false, std::memory_order_release);
        return false;
    }
    if (virtualKey == VK_SHIFT || virtualKey == VK_LSHIFT || virtualKey == VK_RSHIFT) {
        gShiftHeld.store(false, std::memory_order_release);
        return false;
    }

    bool consumed{};
    if (virtualKey == gGuiHotkey.load(std::memory_order_acquire)) {
        consumed = gGuiHotkeyHeld.exchange(false, std::memory_order_acq_rel) || consumed;
    }
    if (virtualKey == gLayerIncreaseHotkey.load(std::memory_order_acquire)) {
        consumed = gLayerIncreaseHotkeyHeld.exchange(false, std::memory_order_acq_rel) || consumed;
    }
    if (virtualKey == gLayerDecreaseHotkey.load(std::memory_order_acquire)) {
        consumed = gLayerDecreaseHotkeyHeld.exchange(false, std::memory_order_acq_rel) || consumed;
    }
    for (std::size_t index = 0; index < gMoveHotkeys.size(); ++index) {
        if (virtualKey == gMoveHotkeys[index].load(std::memory_order_acquire)) {
            consumed = gMoveHotkeyHeld[index].exchange(false, std::memory_order_acq_rel) || consumed;
        }
    }
    if (virtualKey < gConsumeKeyReleaseUntil.size()) {
        auto const now = GetTickCount64();
        auto& deadline = gConsumeKeyReleaseUntil[virtualKey];
        if (consumed) {
            deadline.store(now + 100, std::memory_order_release);
            return true;
        }
        if (now <= deadline.load(std::memory_order_acquire)) return true;
    }
    return false;
}

void resetHotkeyState() {
    gControlHeld.store(false, std::memory_order_release);
    gAltHeld.store(false, std::memory_order_release);
    gShiftHeld.store(false, std::memory_order_release);
    gGuiHotkeyHeld.store(false, std::memory_order_release);
    gLayerIncreaseHotkeyHeld.store(false, std::memory_order_release);
    gLayerDecreaseHotkeyHeld.store(false, std::memory_order_release);
    for (auto& held : gMoveHotkeyHeld) held.store(false, std::memory_order_release);
    for (auto& deadline : gConsumeKeyReleaseUntil) deadline.store(0, std::memory_order_release);
}

void processPendingHotkeyActions() {
    auto const offsetX = gPendingOffsetX.exchange(0, std::memory_order_acq_rel);
    auto const offsetY = gPendingOffsetY.exchange(0, std::memory_order_acq_rel);
    auto const offsetZ = gPendingOffsetZ.exchange(0, std::memory_order_acq_rel);
    auto const layerDelta = gPendingLayerDelta.exchange(0, std::memory_order_acq_rel);
    auto const layerActionEnabled = layerDelta != 0
        && gLayerDisplayMode.load(std::memory_order_relaxed) != 0;
    bool changed = offsetX != 0 || offsetY != 0 || offsetZ != 0 || layerActionEnabled;

    auto applyOffset = [](std::atomic_int& target, int delta) {
        if (delta == 0) return;
        auto const current = static_cast<long long>(target.load(std::memory_order_relaxed));
        auto const next = std::clamp(
            current + static_cast<long long>(delta),
            static_cast<long long>(std::numeric_limits<int>::min()),
            static_cast<long long>(std::numeric_limits<int>::max())
        );
        target.store(static_cast<int>(next), std::memory_order_relaxed);
    };
    applyOffset(gOffsetX, offsetX);
    applyOffset(gOffsetY, offsetY);
    applyOffset(gOffsetZ, offsetZ);

    if (layerActionEnabled) {
        auto maxLayer = 0;
        auto const layerAxis = gLayerAxis.load(std::memory_order_relaxed);
        {
            std::lock_guard lock(gLoadedMutex);
            if (gLoaded) maxLayer = maxLayerFor(*gLoaded, layerAxis);
        }
        auto const current = static_cast<long long>(gDisplayLayer.load(std::memory_order_relaxed));
        auto const next = std::clamp(current + static_cast<long long>(layerDelta), 0LL, static_cast<long long>(maxLayer));
        gDisplayLayer.store(static_cast<int>(next), std::memory_order_relaxed);
    }

    changed = gPendingSettingsSave.exchange(false, std::memory_order_acq_rel) || changed;
    if (changed) saveSettings();
}

bool hasHudInfo() {
    if (!gHudEnabled.load(std::memory_order_relaxed)) return false;
    if (!gHudShowFileName.load(std::memory_order_relaxed)
        && !gHudShowLayer.load(std::memory_order_relaxed)
        && !gHudShowProgress.load(std::memory_order_relaxed)
        && !gHudShowWrongState.load(std::memory_order_relaxed)
        && !gHudShowWrongType.load(std::memory_order_relaxed)
        && !gHudShowBlockEntity.load(std::memory_order_relaxed)) return false;
    std::lock_guard lock(gLoadedMutex);
    return static_cast<bool>(gLoaded);
}

void renderHud() {
    if (isGuiVisible()) return;
    if (!gHudEnabled.load(std::memory_order_relaxed)) return;
    auto const showFileName = gHudShowFileName.load(std::memory_order_relaxed);
    auto const showLayer = gHudShowLayer.load(std::memory_order_relaxed);
    auto const showProgress = gHudShowProgress.load(std::memory_order_relaxed);
    auto const showWrongState = gHudShowWrongState.load(std::memory_order_relaxed);
    auto const showWrongType = gHudShowWrongType.load(std::memory_order_relaxed);
    auto const showBlockEntity = gHudShowBlockEntity.load(std::memory_order_relaxed);
    if (!showFileName && !showLayer && !showProgress && !showWrongState && !showWrongType && !showBlockEntity) return;

    std::string fileName;
    int maxLayer{};
    auto const layerAxis = gLayerAxis.load(std::memory_order_relaxed);
    {
        std::lock_guard lock(gLoadedMutex);
        if (!gLoaded) return;
        fileName = pathToUtf8(gLoaded->sourcePath.filename());
        maxLayer = maxLayerFor(*gLoaded, layerAxis);
    }

    auto uiScale = gUiScale.load(std::memory_order_relaxed);
    if (uiScale <= 0.0f) {
        auto const displaySize = ImGui::GetIO().DisplaySize;
        uiScale = std::clamp(
            std::min(displaySize.x / 1920.0f, displaySize.y / 1080.0f),
            1.0f,
            5.0f
        );
    }
    auto const layerMode = gLayerDisplayMode.load(std::memory_order_relaxed);
    auto const currentLayer = std::clamp(
        gDisplayLayer.load(std::memory_order_relaxed),
        0,
        maxLayer
    );

    auto const displaySize = ImGui::GetIO().DisplaySize;
    auto const hudPosition = std::clamp(gHudPosition.load(std::memory_order_relaxed), 0, 3);
    auto const right = hudPosition >= 2;
    auto const bottom = (hudPosition & 1) != 0;
    auto const margin = 16.0f * uiScale;
    ImGui::SetNextWindowPos(
        ImVec2(right ? displaySize.x - margin : margin, bottom ? displaySize.y - margin : margin),
        ImGuiCond_Always,
        ImVec2(right ? 1.0f : 0.0f, bottom ? 1.0f : 0.0f)
    );
    ImGui::SetNextWindowBgAlpha(0.68f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * uiScale);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(12.0f * uiScale, 8.0f * uiScale)
    );
    constexpr auto flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoNavInputs
        | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##LHoloHud", nullptr, flags)) {
        ImGui::SetWindowFontScale(uiScale * 0.5f);
        if (showFileName) ImGui::Text("投影：%s", fileName.c_str());
        if (showLayer && layerMode == 0) {
            ImGui::TextUnformatted("显示范围：完整结构");
        } else if (showLayer && layerMode == 1) {
            ImGui::Text(
                "当前层：%d / %d（%s 轴）",
                currentLayer,
                maxLayer,
                layerAxis == 1 ? "X" : "Y"
            );
        } else if (showLayer && layerMode == 2) {
            ImGui::Text(
                "显示范围：第 0～%d 层（%s 轴）",
                currentLayer,
                layerAxis == 1 ? "X" : "Y"
            );
        } else if (showLayer) {
            ImGui::Text(
                "显示范围：第 %d～%d 层（%s 轴）",
                currentLayer,
                maxLayer,
                layerAxis == 1 ? "X" : "Y"
            );
        }
        if (showProgress || showWrongState || showWrongType) {
            auto const progress = projection::getBuildProgress();
            if (showProgress) {
            ImGui::Text(
                "建造进度：%llu / %llu",
                static_cast<unsigned long long>(progress.placed),
                static_cast<unsigned long long>(progress.total)
            );
            }
            if (showWrongState && progress.wrongState != 0) {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.62f, 0.18f, 1.0f),
                    "朝向错误：%llu",
                    static_cast<unsigned long long>(progress.wrongState)
                );
            }
            if (showWrongType && progress.wrongType != 0) {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.28f, 0.24f, 1.0f),
                    "放置错误：%llu",
                    static_cast<unsigned long long>(progress.wrongType)
                );
            }
        }
        auto const aimedBlockEntity = place::getAimedBlockEntityName();
        if (showBlockEntity && !aimedBlockEntity.empty()) {
            ImGui::TextColored(
                ImVec4(0.55f, 0.85f, 1.0f, 1.0f),
                "方块实体：%s",
                aimedBlockEntity.c_str()
            );
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void renderGui() {
    if (!isGuiVisible()) return;

    static char pathBuffer[2048]{};
    static bool pathInitialized = false;
    auto uiScale = gUiScale.load(std::memory_order_relaxed);
    auto const displaySize = ImGui::GetIO().DisplaySize;
    if (uiScale <= 0.0f) {
        auto const automaticScale = std::min(displaySize.x / 1920.0f, displaySize.y / 1080.0f);
        uiScale = std::clamp(automaticScale, 1.0f, 5.0f);
        gUiScale.store(uiScale, std::memory_order_relaxed);
    }
    if (!pathInitialized) {
        std::lock_guard lock(gLoadedMutex);
        std::snprintf(pathBuffer, sizeof(pathBuffer), "%s", gLastPath.c_str());
        pathInitialized = true;
    }

    bool open = true;
    // Full-screen ImGui canvas, matching ChiyanMap's proven layout. This only
    // fills Minecraft's current client area; it does not change the OS/window
    // fullscreen mode and therefore stays independent from the F11 lifecycle.
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(displaySize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    if (ImGui::Begin(
            "##LHoloFullscreen",
            &open,
            ImGuiWindowFlags_NoTitleBar
                | ImGuiWindowFlags_NoCollapse
                | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoBringToFrontOnFocus
                | ImGuiWindowFlags_NoNavFocus
                | ImGuiWindowFlags_NoSavedSettings
        )) {
        auto const panelWidth = ImGui::GetContentRegionAvail().x;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.075f, 0.075f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::BeginChild(
            "##LHoloFrame",
            ImVec2(0.0f, 0.0f),
            false,
            ImGuiWindowFlags_None
        );
        ImGui::SetWindowFontScale(uiScale * 0.5f);
        auto const closeButtonWidth = 110.0f * uiScale;
        auto const closeButtonHeight = 42.0f * uiScale;
        // Larger title, vertically centered against the taller close button.
        ImGui::SetWindowFontScale(uiScale * 0.9f);
        float const titleHeight = ImGui::GetTextLineHeight();
        float const titleOffset = std::max(0.0f, (closeButtonHeight - titleHeight) * 0.5f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + titleOffset);
        ImGui::TextUnformatted("LHolo");
        ImGui::SetWindowFontScale(uiScale * 0.5f);
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - titleOffset);
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), panelWidth - closeButtonWidth - 20.0f));
        if (ImGui::Button("关闭菜单", ImVec2(closeButtonWidth, closeButtonHeight))) open = false;
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        static int activePage = 0;
        static char const* pageNames[]{"投影", "结构变换", "渲染设置", "快捷键", "HUD"};
        auto const spacing = ImGui::GetStyle().ItemSpacing.x;
        auto const pageCount = static_cast<float>(std::size(pageNames));
        auto const tabWidth = std::max(
            80.0f,
            (ImGui::GetContentRegionAvail().x - spacing * (pageCount - 1.0f)) / pageCount
        );
        for (int page = 0; page < static_cast<int>(std::size(pageNames)); ++page) {
            auto const selected = activePage == page;
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.42f, 0.68f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.48f, 0.76f, 1.0f));
            }
            if (ImGui::Button(pageNames[page], ImVec2(tabWidth, 42.0f * uiScale))) activePage = page;
            if (selected) ImGui::PopStyleColor(2);
            if (page + 1 < static_cast<int>(std::size(pageNames))) ImGui::SameLine();
        }
        ImGui::Separator();
        ImGui::BeginChild(
            "##LHoloPage",
            ImVec2(0.0f, 0.0f),
            false,
            ImGuiWindowFlags_AlwaysVerticalScrollbar
        );
        ImGui::SetWindowFontScale(uiScale * 0.5f);

        if (activePage == 0) {
        ImGui::SeparatorText("投影文件");
        std::string status;
        {
            std::lock_guard lock(gLoadedMutex);
            status = gStatus;
        }
        ImGui::TextWrapped("%s", status.c_str());
        ImGui::Separator();
        ImGui::TextUnformatted("结构文件路径（.mcstructure / .litematic）");
        ImGui::SetNextItemWidth(-115.0f * uiScale);
        auto const blockOpeningInput = gOpeningInputBlockFrames.load(std::memory_order_acquire) > 0;
        ImGui::InputText(
            "##structure_path",
            pathBuffer,
            sizeof(pathBuffer),
            blockOpeningInput ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None
        );
        ImGui::SameLine();
        if (ImGui::Button("浏览", ImVec2(105.0f * uiScale, 0.0f))) {
            if (auto selected = browseStructureFile(pathFromUtf8(pathBuffer))) {
                auto const value = pathToUtf8(*selected);
                std::snprintf(pathBuffer, sizeof(pathBuffer), "%s", value.c_str());
            }
        }

        if (ImGui::Button("加载", ImVec2(130.0f * uiScale, 0.0f))) {
            std::string pathValue{pathBuffer};
            if (pathValue.empty()) {
                std::lock_guard lock(gLoadedMutex);
                gStatus = "请选择或输入 .mcstructure / .litematic 文件路径";
            } else {
                std::string error;
                auto loaded = loadStructure(pathFromUtf8(pathValue), error);
                if (loaded) {
                    std::string loadedStatus;
                    {
                        std::lock_guard lock(gLoadedMutex);
                        gLastPath = pathValue;
                        gStatus = makeStatus(*loaded);
                        loadedStatus = gStatus;
                        gLoaded = std::move(loaded);
                    }
                    saveSettings();
                    logger().info("{}", loadedStatus);
                } else {
                    std::lock_guard lock(gLoadedMutex);
                    gStatus = "加载失败: " + error;
                    logger().error("Could not load structure {}: {}", pathValue, error);
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("关闭投影", ImVec2(130.0f * uiScale, 0.0f))) {
            projection::disable();
            clear();
        }
        ImGui::Spacing();
        if (gHasSavedProjection.load(std::memory_order_acquire)) {
            auto const savedX = gSavedAnchorX.load(std::memory_order_relaxed);
            auto const savedY = gSavedAnchorY.load(std::memory_order_relaxed);
            auto const savedZ = gSavedAnchorZ.load(std::memory_order_relaxed);
            if (ImGui::Button("恢复上次投影", ImVec2(180.0f * uiScale, 0.0f))) {
                std::string savedPath;
                {
                    std::lock_guard lock(gLoadedMutex);
                    savedPath = gSavedStructurePath;
                }
                std::string error;
                auto loaded = loadStructure(pathFromUtf8(savedPath), error);
                if (loaded) {
                    gRotationQuarterTurns.store(gSavedRotation.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    gMirrorMode.store(gSavedMirror.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    gOffsetX.store(gSavedOffsetX.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    gOffsetY.store(gSavedOffsetY.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    gOffsetZ.store(gSavedOffsetZ.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    gLayerDisplayMode.store(gSavedLayerDisplayMode.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    gDisplayLayer.store(gSavedDisplayLayer.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    gLayerAxis.store(gSavedLayerAxis.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    projection::requestNextStructureAnchor(savedX, savedY, savedZ);
                    std::lock_guard lock(gLoadedMutex);
                    gLastPath = savedPath;
                    gStatus = "已恢复上次投影记录，等待进入渲染";
                    gLoaded = std::move(loaded);
                    logger().info(
                        "Restoring projection {} at ({}, {}, {})",
                        savedPath,
                        savedX,
                        savedY,
                        savedZ
                    );
                } else {
                    std::lock_guard lock(gLoadedMutex);
                    gStatus = "恢复失败: " + error;
                    logger().error("Could not restore structure {}: {}", savedPath, error);
                }
            }
            ImGui::TextDisabled(
                "上次投影原点：X %d  Y %d  Z %d",
                savedX,
                savedY,
                savedZ
            );
        } else {
            ImGui::TextDisabled("没有可恢复的上次投影记录");
        }
        ImGui::Separator();
        ImGui::SetNextItemWidth(260.0f * uiScale);
        if (ImGui::InputFloat("界面缩放（范围 1～5）", &uiScale, 0.0f, 0.0f, "%.1f")) {
            uiScale = std::clamp(uiScale, 1.0f, 5.0f);
            gUiScale.store(uiScale, std::memory_order_relaxed);
            saveSettings();
        }
        auto structureBoundsEnabled = projection::getStructureBoundsEnabled();
        if (ImGui::Checkbox("显示整体结构边框", &structureBoundsEnabled)) {
            projection::setStructureBoundsEnabled(structureBoundsEnabled);
            saveSettings();
        }
        // 轻松放置 / 手动放置 / 范围放置 are mutually exclusive placement modes:
        // enabling one clears the others so only a single mode is ever active.
        auto easyPlaceEnabled = place::isEnabled();
        if (ImGui::Checkbox("轻松放置（准心对准投影方块自动放置）", &easyPlaceEnabled)) {
            place::setEnabled(easyPlaceEnabled);
            if (easyPlaceEnabled) { place::setManualMode(false); place::setRangeEnabled(false); }
            saveSettings();
        }
        auto manualPlace = place::isManualMode();
        if (ImGui::Checkbox("手动放置（右键放置·按住连放）", &manualPlace)) {
            place::setManualMode(manualPlace);
            if (manualPlace) { place::setEnabled(false); place::setRangeEnabled(false); }
            saveSettings();
        }
        auto rangeEnabled = place::isRangeEnabled();
        if (ImGui::Checkbox("范围放置（自动放置周围投影缺块）", &rangeEnabled)) {
            place::setRangeEnabled(rangeEnabled);
            if (rangeEnabled) { place::setEnabled(false); place::setManualMode(false); }
            saveSettings();
        }
        auto placementRadius = place::getPlacementRadius();
        ImGui::SetNextItemWidth(260.0f * uiScale);
        if (ImGui::SliderInt("放置半径（范围 1～4）", &placementRadius, 1, 4)) {
            place::setPlacementRadius(placementRadius);
            saveSettings();
        }
        }

        if (activePage == 1) {
        ImGui::SeparatorText("结构变换");
        static char const* rotationNames[]{"0°", "90°", "180°", "270°"};
        auto rotation = gRotationQuarterTurns.load(std::memory_order_relaxed);
        ImGui::SetNextItemWidth(260.0f * uiScale);
        if (ImGui::Combo("结构旋转", &rotation, rotationNames, 4)) {
            gRotationQuarterTurns.store(rotation, std::memory_order_relaxed);
            saveSettings();
        }
        static char const* mirrorNames[]{"不镜像", "X 轴镜像", "Z 轴镜像", "X + Z 镜像"};
        auto mirror = gMirrorMode.load(std::memory_order_relaxed);
        ImGui::SetNextItemWidth(260.0f * uiScale);
        if (ImGui::Combo("结构镜像", &mirror, mirrorNames, 4)) {
            gMirrorMode.store(mirror, std::memory_order_relaxed);
            saveSettings();
        }
        }

        if (activePage == 2) {
        ImGui::SeparatorText("投影样式");
        auto opacityPercent = static_cast<int>(std::lround(projection::getOpacity() * 100.0f));
        ImGui::SetNextItemWidth(260.0f * uiScale);
        if (ImGui::InputInt("投影透明度（范围 0～100）", &opacityPercent, 0, 0)) {
            opacityPercent = std::clamp(opacityPercent, 0, 100);
            projection::setOpacity(static_cast<float>(opacityPercent) / 100.0f);
            saveSettings();
        }
        ImGui::SeparatorText("分层显示");
        static char const* layerAxisNames[]{"Y 轴（水平分层）", "X 轴（纵向切片）"};
        auto layerAxis = gLayerAxis.load(std::memory_order_relaxed);
        ImGui::SetNextItemWidth(260.0f * uiScale);
        if (ImGui::Combo("分层轴", &layerAxis, layerAxisNames, 2)) {
            gLayerAxis.store(layerAxis, std::memory_order_relaxed);
            saveSettings();
        }
        static char const* layerModeNames[]{"完整结构", "单层", "当前层及以下", "当前层及以上"};
        auto layerMode = gLayerDisplayMode.load(std::memory_order_relaxed);
        ImGui::SetNextItemWidth(260.0f * uiScale);
        if (ImGui::Combo("显示范围", &layerMode, layerModeNames, 4)) {
            gLayerDisplayMode.store(layerMode, std::memory_order_relaxed);
            saveSettings();
        }
        auto maxLayer = 0;
        {
            std::lock_guard lock(gLoadedMutex);
            if (gLoaded) maxLayer = maxLayerFor(*gLoaded, layerAxis);
        }
        auto displayLayer = std::clamp(gDisplayLayer.load(std::memory_order_relaxed), 0, maxLayer);
        if (displayLayer != gDisplayLayer.load(std::memory_order_relaxed)) {
            gDisplayLayer.store(displayLayer, std::memory_order_relaxed);
        }
        ImGui::TextUnformatted("当前层");
        ImGui::SameLine(90.0f * uiScale);
        bool layerChanged{};
        if (ImGui::Button("-##layer", ImVec2(48.0f * uiScale, 0.0f))) {
            displayLayer = std::max(0, displayLayer - 1);
            layerChanged = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f * uiScale);
        if (ImGui::InputInt("##display_layer", &displayLayer, 0, 0)) {
            displayLayer = std::clamp(displayLayer, 0, maxLayer);
            layerChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("+##layer", ImVec2(48.0f * uiScale, 0.0f))) {
            displayLayer = std::min(maxLayer, displayLayer + 1);
            layerChanged = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled(
            "0 - %d（结构 %s 轴起点为 0）",
            maxLayer,
            layerAxis == 1 ? "X" : "Y"
        );
        if (layerChanged) {
            gDisplayLayer.store(displayLayer, std::memory_order_relaxed);
            saveSettings();
        }
        ImGui::SeparatorText("纠错样式");
        auto correctionFillOpacityPercent = static_cast<int>(
            std::lround(projection::getCorrectionFillOpacity() * 100.0f)
        );
        ImGui::SetNextItemWidth(260.0f * uiScale);
        if (ImGui::InputInt("纠错提示透明度（范围 0～100）", &correctionFillOpacityPercent, 0, 0)) {
            correctionFillOpacityPercent = std::clamp(correctionFillOpacityPercent, 0, 100);
            projection::setCorrectionFillOpacity(static_cast<float>(correctionFillOpacityPercent) / 100.0f);
            saveSettings();
        }
        auto correctionOutlineOpacityPercent = static_cast<int>(
            std::lround(projection::getCorrectionOutlineOpacity() * 100.0f)
        );
        ImGui::SetNextItemWidth(260.0f * uiScale);
        if (ImGui::InputInt("描边透明度（范围 0～100）", &correctionOutlineOpacityPercent, 0, 0)) {
            correctionOutlineOpacityPercent = std::clamp(correctionOutlineOpacityPercent, 0, 100);
            projection::setCorrectionOutlineOpacity(
                static_cast<float>(correctionOutlineOpacityPercent) / 100.0f
            );
            saveSettings();
        }
        if (ImGui::Button("恢复默认纠错样式", ImVec2(210.0f * uiScale, 0.0f))) {
            projection::setCorrectionFillOpacity(0.15f);
            projection::setCorrectionOutlineOpacity(1.0f);
            saveSettings();
        }
        }

        if (activePage == 1) {
        auto offsetControl = [uiScale](char const* axis, std::atomic_int& value) {
            bool changed{};
            ImGui::PushID(axis);
            ImGui::TextUnformatted(axis);
            ImGui::SameLine(45.0f * uiScale);
            if (ImGui::Button("-", ImVec2(48.0f * uiScale, 0.0f))) {
                value.fetch_sub(1, std::memory_order_relaxed);
                changed = true;
            }
            ImGui::SameLine();
            auto current = value.load(std::memory_order_relaxed);
            ImGui::SetNextItemWidth(120.0f * uiScale);
            if (ImGui::InputInt("##value", &current, 0, 0)) {
                value.store(current, std::memory_order_relaxed);
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("+", ImVec2(48.0f * uiScale, 0.0f))) {
                value.fetch_add(1, std::memory_order_relaxed);
                changed = true;
            }
            ImGui::PopID();
            return changed;
        };
        ImGui::SeparatorText("结构偏移");
        auto offsetsChanged = offsetControl("X", gOffsetX);
        offsetsChanged = offsetControl("Y", gOffsetY) || offsetsChanged;
        offsetsChanged = offsetControl("Z", gOffsetZ) || offsetsChanged;
        if (offsetsChanged) saveSettings();
        }

        if (activePage == 3) {
        ImGui::SeparatorText("快捷键");
        auto hotkeyControl = [uiScale](
            char const* label,
            std::atomic_uint& key,
            std::atomic_uint& modifiers,
            std::atomic_bool& capturing,
            char const* id
        ) {
            ImGui::PushID(id);
            auto const isCapturing = capturing.load(std::memory_order_acquire);
            auto const buttonLabel = isCapturing
                ? std::string{"请按组合键（支持 Ctrl / Alt / Shift）"}
                : std::string{label} + "：" + hotkeyChordName(
                    modifiers.load(std::memory_order_relaxed),
                    key.load(std::memory_order_relaxed)
                );
            if (ImGui::Button(buttonLabel.c_str(), ImVec2(500.0f * uiScale, 0.0f)) && !isCapturing) {
                gCapturingGuiHotkey.store(false, std::memory_order_release);
                gCapturingLayerIncreaseHotkey.store(false, std::memory_order_release);
                gCapturingLayerDecreaseHotkey.store(false, std::memory_order_release);
                for (auto& other : gCapturingMoveHotkey) {
                    other.store(false, std::memory_order_release);
                }
                capturing.store(true, std::memory_order_release);
            }
            ImGui::SameLine();
            if (ImGui::Button("清除", ImVec2(90.0f * uiScale, 0.0f))) {
                key.store(0, std::memory_order_release);
                modifiers.store(0, std::memory_order_release);
                capturing.store(false, std::memory_order_release);
                saveSettings();
            }
            ImGui::PopID();
        };
        hotkeyControl("打开投影菜单", gGuiHotkey, gGuiHotkeyModifiers, gCapturingGuiHotkey, "gui_hotkey");
        hotkeyControl("结构偏移 X -1", gMoveHotkeys[0], gMoveHotkeyModifiers[0], gCapturingMoveHotkey[0], "move_x_minus");
        hotkeyControl("结构偏移 X +1", gMoveHotkeys[1], gMoveHotkeyModifiers[1], gCapturingMoveHotkey[1], "move_x_plus");
        hotkeyControl("结构偏移 Z -1", gMoveHotkeys[2], gMoveHotkeyModifiers[2], gCapturingMoveHotkey[2], "move_z_minus");
        hotkeyControl("结构偏移 Z +1", gMoveHotkeys[3], gMoveHotkeyModifiers[3], gCapturingMoveHotkey[3], "move_z_plus");
        hotkeyControl("结构偏移 Y +1", gMoveHotkeys[4], gMoveHotkeyModifiers[4], gCapturingMoveHotkey[4], "move_y_plus");
        hotkeyControl("结构偏移 Y -1", gMoveHotkeys[5], gMoveHotkeyModifiers[5], gCapturingMoveHotkey[5], "move_y_minus");
        hotkeyControl(
            "显示层 +1",
            gLayerIncreaseHotkey,
            gLayerIncreaseHotkeyModifiers,
            gCapturingLayerIncreaseHotkey,
            "layer_increase_hotkey"
        );
        hotkeyControl(
            "显示层 -1",
            gLayerDecreaseHotkey,
            gLayerDecreaseHotkeyModifiers,
            gCapturingLayerDecreaseHotkey,
            "layer_decrease_hotkey"
        );
        if (ImGui::Button("恢复默认快捷键", ImVec2(220.0f * uiScale, 0.0f))) {
            gGuiHotkey.store('M', std::memory_order_relaxed);
            gGuiHotkeyModifiers.store(kHotkeyModifierAlt, std::memory_order_relaxed);
            static unsigned int const moveKeys[]{VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_UP, VK_DOWN};
            static unsigned int const moveModifiers[]{
                kHotkeyModifierControl,
                kHotkeyModifierControl,
                kHotkeyModifierControl,
                kHotkeyModifierControl,
                kHotkeyModifierShift,
                kHotkeyModifierShift
            };
            for (std::size_t index = 0; index < gMoveHotkeys.size(); ++index) {
                gMoveHotkeys[index].store(moveKeys[index], std::memory_order_relaxed);
                gMoveHotkeyModifiers[index].store(moveModifiers[index], std::memory_order_relaxed);
                gCapturingMoveHotkey[index].store(false, std::memory_order_relaxed);
            }
            gLayerIncreaseHotkey.store(VK_UP, std::memory_order_relaxed);
            gLayerDecreaseHotkey.store(VK_DOWN, std::memory_order_relaxed);
            gLayerIncreaseHotkeyModifiers.store(kHotkeyModifierAlt, std::memory_order_relaxed);
            gLayerDecreaseHotkeyModifiers.store(kHotkeyModifierAlt, std::memory_order_relaxed);
            gCapturingGuiHotkey.store(false, std::memory_order_relaxed);
            gCapturingLayerIncreaseHotkey.store(false, std::memory_order_relaxed);
            gCapturingLayerDecreaseHotkey.store(false, std::memory_order_relaxed);
            resetHotkeyState();
            saveSettings();
        }
        ImGui::TextDisabled("“完整结构”模式下，显示层快捷键无效");
        ImGui::TextDisabled("可在聊天栏输入 LHolo 打开投影菜单");
        }

        if (activePage == 4) {
        ImGui::SeparatorText("HUD 信息显示");
        auto hudEnabled = gHudEnabled.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("启用 HUD", &hudEnabled)) {
            gHudEnabled.store(hudEnabled, std::memory_order_relaxed);
            saveSettings();
        }
        ImGui::BeginDisabled(!hudEnabled);
        static char const* hudPositionNames[]{"左上", "左下", "右上", "右下"};
        auto hudPosition = std::clamp(gHudPosition.load(std::memory_order_relaxed), 0, 3);
        ImGui::SetNextItemWidth(260.0f * uiScale);
        if (ImGui::Combo("HUD 位置", &hudPosition, hudPositionNames, 4)) {
            gHudPosition.store(hudPosition, std::memory_order_relaxed);
            saveSettings();
        }
        auto showFileName = gHudShowFileName.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("显示投影文件名", &showFileName)) {
            gHudShowFileName.store(showFileName, std::memory_order_relaxed);
            saveSettings();
        }
        auto showLayer = gHudShowLayer.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("显示渲染层信息", &showLayer)) {
            gHudShowLayer.store(showLayer, std::memory_order_relaxed);
            saveSettings();
        }
        auto showProgress = gHudShowProgress.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("显示建造进度", &showProgress)) {
            gHudShowProgress.store(showProgress, std::memory_order_relaxed);
            saveSettings();
        }
        auto showWrongState = gHudShowWrongState.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("显示朝向错误", &showWrongState)) {
            gHudShowWrongState.store(showWrongState, std::memory_order_relaxed);
            saveSettings();
        }
        auto showWrongType = gHudShowWrongType.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("显示放置错误", &showWrongType)) {
            gHudShowWrongType.store(showWrongType, std::memory_order_relaxed);
            saveSettings();
        }
        auto showBlockEntity = gHudShowBlockEntity.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("显示方块实体名称", &showBlockEntity)) {
            gHudShowBlockEntity.store(showBlockEntity, std::memory_order_relaxed);
            saveSettings();
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("HUD 仅在关闭投影菜单后显示");
        }
        ImGui::EndChild();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    if (gOpeningInputBlockFrames.load(std::memory_order_acquire) > 0) {
        gOpeningInputBlockFrames.fetch_sub(1, std::memory_order_acq_rel);
    }
    if (!open) {
        gGuiVisible.store(false, std::memory_order_release);
        gBlockGameInputUntil.store(GetTickCount64() + 180, std::memory_order_release);
    }
}

void loadSettings() {
    auto const path = settingsPath();
    try {
        if (!std::filesystem::exists(path)) {
            saveSettings();
            return;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("无法打开配置文件");
        auto const json = nlohmann::json::parse(input, nullptr, true, true);
        std::lock_guard lock(gLoadedMutex);
        gLastPath = json.value("lastStructurePath", gLastPath);
        gUiScale.store(std::clamp(json.value("uiScale", 0.0f), 0.0f, 5.0f), std::memory_order_relaxed);
        projection::setOpacity(json.value("opacity", 1.0f));
        projection::setCorrectionFillOpacity(json.value("correctionFillOpacity", 0.15f));
        projection::setCorrectionOutlineOpacity(json.value("correctionOutlineOpacity", 1.0f));
        projection::setStructureBoundsEnabled(json.value("structureBoundsEnabled", true));
        // Transform and layer state are session-local. Only the explicit
        // "restore last projection" record below is persisted.
        gRotationQuarterTurns.store(0, std::memory_order_relaxed);
        gMirrorMode.store(0, std::memory_order_relaxed);
        gOffsetX.store(0, std::memory_order_relaxed);
        gOffsetY.store(0, std::memory_order_relaxed);
        gOffsetZ.store(0, std::memory_order_relaxed);
        gLayerDisplayMode.store(0, std::memory_order_relaxed);
        gDisplayLayer.store(0, std::memory_order_relaxed);
        gLayerAxis.store(0, std::memory_order_relaxed);
        gHudEnabled.store(json.value("hudEnabled", true), std::memory_order_relaxed);
        gHudShowFileName.store(json.value("hudShowFileName", true), std::memory_order_relaxed);
        gHudShowLayer.store(json.value("hudShowLayer", true), std::memory_order_relaxed);
        gHudShowProgress.store(json.value("hudShowProgress", true), std::memory_order_relaxed);
        gHudShowWrongState.store(json.value("hudShowWrongState", true), std::memory_order_relaxed);
        gHudShowWrongType.store(json.value("hudShowWrongType", true), std::memory_order_relaxed);
        gHudShowBlockEntity.store(json.value("hudShowBlockEntity", true), std::memory_order_relaxed);
        gHudPosition.store(std::clamp(json.value("hudPosition", 1), 0, 3), std::memory_order_relaxed);
        place::setEnabled(json.value("easyPlaceEnabled", false));
        place::setManualMode(json.value("easyPlaceManual", false));
        place::setRangeEnabled(json.value("rangePlaceEnabled", false));
        place::setPlacementRadius(std::clamp(json.value("placementRadius", 4), 1, 4));
        gGuiHotkey.store(std::clamp(json.value("guiHotkey", static_cast<int>('M')), 0, 255), std::memory_order_relaxed);
        gGuiHotkeyModifiers.store(
            std::clamp(json.value("guiHotkeyModifiers", static_cast<int>(kHotkeyModifierAlt)), 0, 7),
            std::memory_order_relaxed
        );
        gLayerIncreaseHotkey.store(
            std::clamp(json.value("layerIncreaseHotkey", static_cast<int>(VK_UP)), 0, 255),
            std::memory_order_relaxed
        );
        gLayerDecreaseHotkey.store(
            std::clamp(json.value("layerDecreaseHotkey", static_cast<int>(VK_DOWN)), 0, 255),
            std::memory_order_relaxed
        );
        gLayerIncreaseHotkeyModifiers.store(
            std::clamp(json.value("layerIncreaseHotkeyModifiers", static_cast<int>(kHotkeyModifierAlt)), 0, 7),
            std::memory_order_relaxed
        );
        gLayerDecreaseHotkeyModifiers.store(
            std::clamp(json.value("layerDecreaseHotkeyModifiers", static_cast<int>(kHotkeyModifierAlt)), 0, 7),
            std::memory_order_relaxed
        );
        static char const* moveKeyNames[]{
            "moveXMinusHotkey",
            "moveXPlusHotkey",
            "moveZMinusHotkey",
            "moveZPlusHotkey",
            "moveYPlusHotkey",
            "moveYMinusHotkey"
        };
        static int const moveKeyDefaults[]{VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_UP, VK_DOWN};
        static char const* moveModifierNames[]{
            "moveXMinusHotkeyModifiers",
            "moveXPlusHotkeyModifiers",
            "moveZMinusHotkeyModifiers",
            "moveZPlusHotkeyModifiers",
            "moveYPlusHotkeyModifiers",
            "moveYMinusHotkeyModifiers"
        };
        static int const moveModifierDefaults[]{
            static_cast<int>(kHotkeyModifierControl),
            static_cast<int>(kHotkeyModifierControl),
            static_cast<int>(kHotkeyModifierControl),
            static_cast<int>(kHotkeyModifierControl),
            static_cast<int>(kHotkeyModifierShift),
            static_cast<int>(kHotkeyModifierShift)
        };
        for (std::size_t index = 0; index < gMoveHotkeys.size(); ++index) {
            gMoveHotkeys[index].store(
                std::clamp(json.value(moveKeyNames[index], moveKeyDefaults[index]), 0, 255),
                std::memory_order_relaxed
            );
            gMoveHotkeyModifiers[index].store(
                std::clamp(json.value(moveModifierNames[index], moveModifierDefaults[index]), 0, 7),
                std::memory_order_relaxed
            );
        }
        gHasSavedProjection.store(json.value("hasSavedProjection", false), std::memory_order_relaxed);
        gSavedAnchorX.store(json.value("savedAnchorX", 0), std::memory_order_relaxed);
        gSavedAnchorY.store(json.value("savedAnchorY", 0), std::memory_order_relaxed);
        gSavedAnchorZ.store(json.value("savedAnchorZ", 0), std::memory_order_relaxed);
        gSavedRotation.store(json.value("savedRotation", 0), std::memory_order_relaxed);
        gSavedMirror.store(json.value("savedMirror", 0), std::memory_order_relaxed);
        gSavedOffsetX.store(json.value("savedOffsetX", 0), std::memory_order_relaxed);
        gSavedOffsetY.store(json.value("savedOffsetY", 0), std::memory_order_relaxed);
        gSavedOffsetZ.store(json.value("savedOffsetZ", 0), std::memory_order_relaxed);
        gSavedLayerDisplayMode.store(json.value("savedLayerDisplayMode", 0), std::memory_order_relaxed);
        gSavedDisplayLayer.store(json.value("savedDisplayLayer", 0), std::memory_order_relaxed);
        gSavedLayerAxis.store(std::clamp(json.value("savedLayerAxis", 0), 0, 1), std::memory_order_relaxed);
        gSavedStructurePath = json.value("savedStructurePath", std::string{});
        logger().info("Loaded projection settings from {}", path.string());
    } catch (std::exception const& exception) {
        logger().error("Could not load projection settings {}: {}", path.string(), exception.what());
    }
}

void saveSettings() {
    auto const path = settingsPath();
    try {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) throw std::runtime_error(error.message());
        if (gHasSavedProjection.load(std::memory_order_acquire)) {
            // Keep the saved session parameters current while retaining the
            // original structure path and projection anchor.
            gSavedRotation.store(gRotationQuarterTurns.load(std::memory_order_relaxed), std::memory_order_relaxed);
            gSavedMirror.store(gMirrorMode.load(std::memory_order_relaxed), std::memory_order_relaxed);
            gSavedOffsetX.store(gOffsetX.load(std::memory_order_relaxed), std::memory_order_relaxed);
            gSavedOffsetY.store(gOffsetY.load(std::memory_order_relaxed), std::memory_order_relaxed);
            gSavedOffsetZ.store(gOffsetZ.load(std::memory_order_relaxed), std::memory_order_relaxed);
            gSavedLayerDisplayMode.store(gLayerDisplayMode.load(std::memory_order_relaxed), std::memory_order_relaxed);
            gSavedDisplayLayer.store(gDisplayLayer.load(std::memory_order_relaxed), std::memory_order_relaxed);
            gSavedLayerAxis.store(gLayerAxis.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        std::string lastPath;
        std::string savedStructurePath;
        {
            std::lock_guard lock(gLoadedMutex);
            lastPath = gLastPath;
            savedStructurePath = gSavedStructurePath;
        }
        nlohmann::ordered_json const json{
            {"version", 7},
            {"lastStructurePath", lastPath},
            {"uiScale", gUiScale.load(std::memory_order_relaxed)},
            {"opacity", projection::getOpacity()},
            {"correctionFillOpacity", projection::getCorrectionFillOpacity()},
            {"correctionOutlineOpacity", projection::getCorrectionOutlineOpacity()},
            {"structureBoundsEnabled", projection::getStructureBoundsEnabled()},
            {"easyPlaceEnabled", place::isEnabled()},
            {"easyPlaceManual", place::isManualMode()},
            {"rangePlaceEnabled", place::isRangeEnabled()},
            {"placementRadius", place::getPlacementRadius()},
            {"hudEnabled", gHudEnabled.load(std::memory_order_relaxed)},
            {"hudShowFileName", gHudShowFileName.load(std::memory_order_relaxed)},
            {"hudShowLayer", gHudShowLayer.load(std::memory_order_relaxed)},
            {"hudShowProgress", gHudShowProgress.load(std::memory_order_relaxed)},
            {"hudShowWrongState", gHudShowWrongState.load(std::memory_order_relaxed)},
            {"hudShowWrongType", gHudShowWrongType.load(std::memory_order_relaxed)},
            {"hudShowBlockEntity", gHudShowBlockEntity.load(std::memory_order_relaxed)},
            {"hudPosition", gHudPosition.load(std::memory_order_relaxed)},
            {"guiHotkey", gGuiHotkey.load(std::memory_order_relaxed)},
            {"guiHotkeyModifiers", gGuiHotkeyModifiers.load(std::memory_order_relaxed)},
            {"layerIncreaseHotkey", gLayerIncreaseHotkey.load(std::memory_order_relaxed)},
            {"layerDecreaseHotkey", gLayerDecreaseHotkey.load(std::memory_order_relaxed)},
            {"layerIncreaseHotkeyModifiers", gLayerIncreaseHotkeyModifiers.load(std::memory_order_relaxed)},
            {"layerDecreaseHotkeyModifiers", gLayerDecreaseHotkeyModifiers.load(std::memory_order_relaxed)},
            {"moveXMinusHotkey", gMoveHotkeys[0].load(std::memory_order_relaxed)},
            {"moveXPlusHotkey", gMoveHotkeys[1].load(std::memory_order_relaxed)},
            {"moveZMinusHotkey", gMoveHotkeys[2].load(std::memory_order_relaxed)},
            {"moveZPlusHotkey", gMoveHotkeys[3].load(std::memory_order_relaxed)},
            {"moveYPlusHotkey", gMoveHotkeys[4].load(std::memory_order_relaxed)},
            {"moveYMinusHotkey", gMoveHotkeys[5].load(std::memory_order_relaxed)},
            {"moveXMinusHotkeyModifiers", gMoveHotkeyModifiers[0].load(std::memory_order_relaxed)},
            {"moveXPlusHotkeyModifiers", gMoveHotkeyModifiers[1].load(std::memory_order_relaxed)},
            {"moveZMinusHotkeyModifiers", gMoveHotkeyModifiers[2].load(std::memory_order_relaxed)},
            {"moveZPlusHotkeyModifiers", gMoveHotkeyModifiers[3].load(std::memory_order_relaxed)},
            {"moveYPlusHotkeyModifiers", gMoveHotkeyModifiers[4].load(std::memory_order_relaxed)},
            {"moveYMinusHotkeyModifiers", gMoveHotkeyModifiers[5].load(std::memory_order_relaxed)},
            {"hasSavedProjection", gHasSavedProjection.load(std::memory_order_relaxed)},
            {"savedAnchorX", gSavedAnchorX.load(std::memory_order_relaxed)},
            {"savedAnchorY", gSavedAnchorY.load(std::memory_order_relaxed)},
            {"savedAnchorZ", gSavedAnchorZ.load(std::memory_order_relaxed)},
            {"savedStructurePath", savedStructurePath},
            {"savedRotation", gSavedRotation.load(std::memory_order_relaxed)},
            {"savedMirror", gSavedMirror.load(std::memory_order_relaxed)},
            {"savedOffsetX", gSavedOffsetX.load(std::memory_order_relaxed)},
            {"savedOffsetY", gSavedOffsetY.load(std::memory_order_relaxed)},
            {"savedOffsetZ", gSavedOffsetZ.load(std::memory_order_relaxed)},
            {"savedLayerDisplayMode", gSavedLayerDisplayMode.load(std::memory_order_relaxed)},
            {"savedDisplayLayer", gSavedDisplayLayer.load(std::memory_order_relaxed)},
            {"savedLayerAxis", gSavedLayerAxis.load(std::memory_order_relaxed)}
        };
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("无法写入配置文件");
        output << json.dump(2);
    } catch (std::exception const& exception) {
        logger().error("Could not save projection settings {}: {}", path.string(), exception.what());
    }
}

std::shared_ptr<LoadedStructure const> getLoaded() {
    std::lock_guard lock(gLoadedMutex);
    return gLoaded;
}

int getRotationQuarterTurns() {
    return gRotationQuarterTurns.load(std::memory_order_relaxed);
}

int getMirrorMode() {
    return gMirrorMode.load(std::memory_order_relaxed);
}

int getOffsetX() { return gOffsetX.load(std::memory_order_relaxed); }
int getOffsetY() { return gOffsetY.load(std::memory_order_relaxed); }
int getOffsetZ() { return gOffsetZ.load(std::memory_order_relaxed); }
int getLayerDisplayMode() { return gLayerDisplayMode.load(std::memory_order_relaxed); }
int getDisplayLayer() { return gDisplayLayer.load(std::memory_order_relaxed); }
int getLayerAxis() { return gLayerAxis.load(std::memory_order_relaxed); }

void recordProjectionAnchor(int x, int y, int z) {
    gSavedAnchorX.store(x, std::memory_order_relaxed);
    gSavedAnchorY.store(y, std::memory_order_relaxed);
    gSavedAnchorZ.store(z, std::memory_order_relaxed);
    gSavedRotation.store(gRotationQuarterTurns.load(std::memory_order_relaxed), std::memory_order_relaxed);
    gSavedMirror.store(gMirrorMode.load(std::memory_order_relaxed), std::memory_order_relaxed);
    gSavedOffsetX.store(gOffsetX.load(std::memory_order_relaxed), std::memory_order_relaxed);
    gSavedOffsetY.store(gOffsetY.load(std::memory_order_relaxed), std::memory_order_relaxed);
    gSavedOffsetZ.store(gOffsetZ.load(std::memory_order_relaxed), std::memory_order_relaxed);
    gSavedLayerDisplayMode.store(gLayerDisplayMode.load(std::memory_order_relaxed), std::memory_order_relaxed);
    gSavedDisplayLayer.store(gDisplayLayer.load(std::memory_order_relaxed), std::memory_order_relaxed);
    gSavedLayerAxis.store(gLayerAxis.load(std::memory_order_relaxed), std::memory_order_relaxed);
    {
        std::lock_guard lock(gLoadedMutex);
        gSavedStructurePath = gLastPath;
    }
    gHasSavedProjection.store(true, std::memory_order_release);
    saveSettings();
}

void clear() {
    std::lock_guard lock(gLoadedMutex);
    gLoaded.reset();
    gStatus = "尚未加载结构文件";
}

} // namespace lholo::structure
