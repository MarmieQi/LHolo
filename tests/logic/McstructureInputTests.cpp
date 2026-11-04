#include "structure/formats/McstructureInput.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace {
std::string fixture(char const* name) {
    // xmake run starts in the target directory; direct execution may use the
    // project root. Find the checked-in fixtures from either working directory.
    auto directory = std::filesystem::current_path();
    for (;;) {
        auto const path = directory / "tests" / "fixtures" / "mcstructure" / name;
        std::ifstream input(path, std::ios::binary);
        if (input) return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
        auto parent = directory.parent_path();
        if (parent == directory) throw std::runtime_error("Missing mcstructure fixture: " + std::string{name});
        directory = std::move(parent);
    }
}

std::string compress(std::string const& input) {
    std::string output(compressBound(static_cast<uLong>(input.size())), '\0');
    auto size = static_cast<uLongf>(output.size());
    if (compress2(reinterpret_cast<Bytef*>(output.data()), &size,
            reinterpret_cast<Bytef const*>(input.data()), static_cast<uLong>(input.size()), Z_BEST_COMPRESSION) != Z_OK)
        throw std::runtime_error("Cannot compress test input");
    output.resize(size);
    return output;
}
}

int runMcstructureInputTests() try {
    using lholo::structure::detail::inflateMcstructureTag;
    int failures{};
    auto check = [&](bool result, char const* message) {
        if (!result) { ++failures; std::fprintf(stderr, "McstructureInput: %s\n", message); }
    };
    std::string error;
    // Real 26.60 native export: a 3x3x3 structure with air and smooth stone.
    auto const compressed = fixture("native-format-2.zlib");
    auto const expected = fixture("native-format-2.nbt");
    auto decoded = inflateMcstructureTag(compressed, expected.size(), error);
    check(decoded && *decoded == expected, "decode native structure tag byte-for-byte at exact size limit");
    check(expected.size() == 322 && expected.starts_with(std::string_view{"\x0a\0\0", 3}),
        "fixture contains a complete little-endian compound root");
    check(expected.find("minecraft:smooth_stone") != std::string::npos, "native palette survives decoding");

    auto reject = [&](std::string const& bytes, std::size_t limit, char const* message) {
        error.clear();
        check(!inflateMcstructureTag(bytes, limit, error) && !error.empty(), message);
    };
    reject(compressed, expected.size() - 1, "reject output beyond configured limit");
    reject(compressed, 0, "reject output when no decoded bytes are permitted");
    reject({}, 1024, "reject missing compressed data");
    reject(expected, 1024, "reject uncompressed NBT incorrectly labeled as zlib");
    for (auto count : {std::size_t{1}, compressed.size() / 2, compressed.size() - 1})
        reject(compressed.substr(0, count), 1024, "reject truncated native export");
    auto corrupt = compressed;
    corrupt.back() ^= 1;
    reject(corrupt, 1024, "reject failed zlib checksum");
    reject(compressed + "extra", 1024, "reject trailing bytes");
    reject(compressed + compressed, 1024, "reject concatenated streams");

    std::string const large(2 * 1024 * 1024, 'x');
    auto const packed = compress(large);
    decoded = inflateMcstructureTag(packed, large.size(), error);
    check(decoded && *decoded == large, "decode across multiple output buffers");
    reject(packed, 64 * 1024, "stop high-ratio input at decoded size limit");
    std::printf("McstructureInputTests: %d failures\n", failures);
    return failures;
} catch (std::exception const& error) {
    std::fprintf(stderr, "McstructureInputTests: %s\n", error.what());
    return 1;
}
