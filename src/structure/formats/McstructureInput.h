#pragma once

#include <array>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <zlib.h>

namespace lholo::structure::detail {

// Only the structure ByteArray is compressed; the outer NBT stays readable.
inline std::optional<std::string> inflateMcstructureTag(
    std::string_view compressed, std::size_t maximumSize, std::string& error
) {
    if (compressed.empty() || compressed.size() > std::numeric_limits<uInt>::max()) {
        error = "mcstructure 压缩结构数据为空或过大";
        return std::nullopt;
    }
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    if (inflateInit(&stream) != Z_OK) {
        error = "无法初始化 mcstructure zlib 解压器";
        return std::nullopt;
    }
    struct EndInflate {
        z_stream* stream;
        ~EndInflate() { inflateEnd(stream); }
    } end{&stream};
    std::string output;
    std::array<char, 64 * 1024> chunk{};
    int result = Z_OK;
    while (result == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(chunk.data());
        stream.avail_out = static_cast<uInt>(chunk.size());
        result = inflate(&stream, Z_NO_FLUSH);
        auto const produced = chunk.size() - stream.avail_out;
        if (produced > maximumSize - output.size()) {
            error = "mcstructure 解压后的结构数据超过大小限制";
            return std::nullopt;
        }
        output.append(chunk.data(), produced);
    }
    if (result != Z_STREAM_END || stream.avail_in != 0) {
        error = "mcstructure zlib 结构数据损坏、不完整或包含多余数据";
        return std::nullopt;
    }
    return output;
}

} // namespace lholo::structure::detail
