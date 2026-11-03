#pragma once
#include "structure/formats/LitematicNbt.h"
#include <array>
#include <limits>
#include <optional>
#include <zlib.h>
namespace lholo::structure::detail {
inline std::optional<std::string> inflateGzip(std::string_view compressed, std::string& error) {
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
        if (output.size() + produced > kMaximumJavaNbtSize) {
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

} // namespace lholo::structure::detail
