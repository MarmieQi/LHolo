#pragma once
// Big-endian NBT and packed states; no game objects or UI dependencies.
// LongArray views borrow the inflated source, which must outlive this tree.
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>
namespace lholo::structure::detail {
inline constexpr std::size_t kMaximumJavaNbtSize = 1024ULL * 1024 * 1024;
struct JavaNbtTag {
    using ByteArray = std::vector<std::uint8_t>;
    using List = std::vector<JavaNbtTag>;
    using Compound = std::unordered_map<std::string, JavaNbtTag>;
    using IntArray = std::vector<std::int32_t>;
    struct LongArray {
        std::string_view bytes;
        std::size_t size() const { return bytes.size() / 8; }
        bool empty() const { return bytes.empty(); }
        std::uint64_t operator[](std::size_t index) const {
            std::uint64_t value;
            std::memcpy(&value, bytes.data() + index * 8, 8);
            if constexpr (std::endian::native == std::endian::little) {
                value = ((value & 0x00ff00ff00ff00ffULL) << 8) | ((value >> 8) & 0x00ff00ff00ff00ffULL);
                value = ((value & 0x0000ffff0000ffffULL) << 16) | ((value >> 16) & 0x0000ffff0000ffffULL);
                value = (value << 32) | (value >> 32);
            }
            return value;
        }
    };
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
        return std::move(std::get<JavaNbtTag::Compound>(root.value));
    }

private:
    std::string_view mBytes;
    std::size_t mOffset{};
    unsigned mDepth{};

    struct Depth {
        unsigned& value;
        explicit Depth(unsigned& depth) : value(depth) {
            if (value >= 512) throw std::runtime_error("NBT nesting exceeds limit");
            ++value;
        }
        ~Depth() { --value; }
    };

    void require(std::size_t count) const {
        if (count > mBytes.size() - mOffset) {
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
        Depth depth{mDepth};
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
            if (count > kMaximumJavaNbtSize) throw std::runtime_error("Litematic NBT 列表过大");
            if (!elementType && count) throw std::runtime_error("Invalid NBT list type");
            require(count);
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
                // Ordinary entities have never been projected or written by
                // this loader. Skip their region payload without constructing
                // a potentially enormous, unused tree of maps and strings.
                if (mDepth == 3 && name == "Entities") {
                    skipPayload(childType);
                    continue;
                }
                values.insert_or_assign(std::move(name), readPayload(childType));
            }
            tag.value = std::move(values);
            break;
        }
        case 11: {
            auto const count = readArrayLength();
            if (count > kMaximumJavaNbtSize / sizeof(std::int32_t)) throw std::runtime_error("Litematic IntArray 过大");
            require(count * sizeof(std::int32_t));
            JavaNbtTag::IntArray values(count);
            for (auto& value : values) value = readBigEndian<std::int32_t>();
            tag.value = std::move(values);
            break;
        }
        case 12: {
            auto const count = readArrayLength();
            if (count > kMaximumJavaNbtSize / sizeof(std::int64_t)) throw std::runtime_error("Litematic LongArray 过大");
            require(count * sizeof(std::int64_t));
            tag.value = JavaNbtTag::LongArray{mBytes.substr(mOffset, count * sizeof(std::int64_t))};
            mOffset += count * sizeof(std::int64_t);
            break;
        }
        default: throw std::runtime_error("Litematic 包含不支持的 NBT 标签类型");
        }
        return tag;
    }

    void skipPayload(std::uint8_t type) {
        Depth depth{mDepth};
        auto skip = [&](std::size_t count) { require(count); mOffset += count; };
        switch (type) {
        case 1: skip(1); break;
        case 2: skip(2); break;
        case 3: case 5: skip(4); break;
        case 4: case 6: skip(8); break;
        case 8: { auto size = readBigEndian<std::uint16_t>(); skip(size); break; }
        case 7: case 11: case 12: {
            auto size = readArrayLength();
            auto width = type == 7 ? 1u : type == 11 ? 4u : 8u;
            if (size > kMaximumJavaNbtSize / width) throw std::runtime_error("NBT array exceeds limit");
            skip(size * width); break;
        }
        case 9: {
            auto element = readU8(); auto size = readArrayLength();
            if (!element && size) throw std::runtime_error("Invalid NBT list type");
            require(size);
            for (std::size_t i = 0; i < size; ++i) skipPayload(element);
            break;
        }
        case 10:
            while (auto child = readU8()) {
                auto size = readBigEndian<std::uint16_t>(); skip(size);
                skipPayload(child);
            }
            break;
        default: throw std::runtime_error("Invalid NBT tag type");
        }
    }
};

inline std::uint32_t packedPaletteIndex(
    JavaNbtTag::LongArray const& values,
    std::uint64_t index,
    unsigned bits
) {
    if (!bits || bits > 32 || index > std::numeric_limits<std::uint64_t>::max() / bits)
        throw std::runtime_error("Invalid packed palette offset");
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

} // namespace lholo::structure::detail
