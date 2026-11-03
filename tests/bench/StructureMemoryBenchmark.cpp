// Real-file storage benchmark, without Minecraft or native block conversion.
// Uses the production gzip/NBT reader and compact cell store. It deliberately
// does not construct the old multi-gigabyte maps just to measure an OOM.
#include "structure/formats/LitematicInput.h"
#include "structure/StructureBlocks.h"
#include <Windows.h>
#include <Psapi.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>

using namespace lholo::structure;
using namespace lholo::structure::detail;
template<class T> T const& field(JavaNbtTag::Compound const& object, char const* name) {
    return std::get<T>(object.at(name).value);
}
std::array<int,3> vectorField(JavaNbtTag::Compound const& object, char const* name) {
    auto const& v = field<JavaNbtTag::Compound>(object,name);
    return {field<std::int32_t>(v,"x"),field<std::int32_t>(v,"y"),field<std::int32_t>(v,"z")};
}
int wmain(int argc, wchar_t** argv) try {
    if (argc != 2) { std::fprintf(stderr,"Pass a .litematic file path\n"); return 2; }
    auto started = std::chrono::steady_clock::now();
    auto seconds = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count(); };
    std::ifstream input{std::filesystem::path{argv[1]},std::ios::binary};
    if (!input) throw std::runtime_error("Cannot open input file");
    std::string compressed{std::istreambuf_iterator<char>{input},std::istreambuf_iterator<char>{}};
    auto compressedSize = compressed.size();
    std::string error;
    auto bytes = inflateGzip(compressed,error);
    if (!bytes) throw std::runtime_error(error);
    std::string{}.swap(compressed);
    auto inflatedSize = bytes->size();
    auto root = JavaNbtReader{*bytes}.readRoot();
    auto const& regions = field<JavaNbtTag::Compound>(root,"Regions");
    struct Region {
        std::array<int,3> minimum, size;
        JavaNbtTag::LongArray const* states;
        std::vector<bool> occupied;
        unsigned bits;
    };
    std::vector<Region> sources;
    std::array<int,3> minimum{INT_MAX,INT_MAX,INT_MAX}, maximum{INT_MIN,INT_MIN,INT_MIN};
    std::size_t palettes{}, largestRegion{};
    for (auto const& [name, tag] : regions) {
        (void)name;
        auto const& object = std::get<JavaNbtTag::Compound>(tag.value);
        auto position = vectorField(object,"Position"), size = vectorField(object,"Size");
        for (int a = 0; a < 3; ++a) {
            if (size[a] < 0) position[a] += size[a] + 1;
            size[a] = std::abs(size[a]);
            minimum[a] = std::min(minimum[a],position[a]);
            maximum[a] = std::max(maximum[a],position[a]+size[a]);
        }
        auto const& palette = field<JavaNbtTag::List>(object,"BlockStatePalette");
        Region region{position,size,&field<JavaNbtTag::LongArray>(object,"BlockStates"),{},
            std::max(2u,static_cast<unsigned>(std::bit_width(palette.size()-1)))};
        for (auto const& item : palette) {
            auto const& block = std::get<JavaNbtTag::Compound>(item.value);
            auto const& id = field<std::string>(block,"Name");
            region.occupied.push_back(id != "minecraft:air" && id != "minecraft:cave_air" && id != "minecraft:void_air");
        }
        palettes += palette.size();
        auto volume = static_cast<std::uint64_t>(size[0])*size[1]*size[2];
        std::size_t count{};
        for (std::uint64_t i = 0; i < volume; ++i) {
            auto id = packedPaletteIndex(*region.states,i,region.bits);
            if (id >= region.occupied.size()) throw std::runtime_error("Invalid palette index");
            count += region.occupied[id];
        }
        largestRegion = std::max(largestRegion,count);
        sources.push_back(std::move(region));
    }
    auto parseSeconds = seconds();
    std::array<int,3> size{maximum[0]-minimum[0],maximum[1]-minimum[1],maximum[2]-minimum[2]};
    StructureBlocks storage;
    auto volume = static_cast<std::uint64_t>(size[0])*size[1]*size[2];
    if (!storage.beginCompact(size[1],size[2],volume-1,palettes)) throw std::runtime_error("Unpackable structure");
    storage.reserveCompact(largestRegion);
    for (auto const& region : sources) {
        std::vector<std::uint32_t> ids;
        for (std::size_t i = 0; i < region.occupied.size(); ++i)
            ids.push_back(storage.addPalette({0,0,0,reinterpret_cast<Block const*>(i+1)}));
        auto v = static_cast<std::uint64_t>(region.size[0])*region.size[1]*region.size[2];
        auto layer = static_cast<std::uint64_t>(region.size[0])*region.size[2];
        for (std::uint64_t i = 0; i < v; ++i) {
            auto id = packedPaletteIndex(*region.states,i,region.bits);
            if (!region.occupied[id]) continue;
            auto x = region.minimum[0]-minimum[0]+i%region.size[0];
            auto y = region.minimum[1]-minimum[1]+i/layer;
            auto z = region.minimum[2]-minimum[2]+(i%layer)/region.size[0];
            storage.appendCompact((x*size[1]+y)*size[2]+z,ids[id]);
        }
        if (sources.size() > 1) storage.mergeCompactRegion();
    }
    sources.clear(); root.clear(); bytes.reset();
    storage.finishCompact();
    auto loadSeconds = seconds();
    std::mt19937_64 random{417};
    std::size_t checksum{};
    constexpr std::size_t queries = 1000000;
    for (std::size_t i = 0; i < queries; ++i) {
        auto index = random()%storage.size();
        auto value = storage[index];
        auto found = storage.find(value.x,value.y,value.z);
        if (found != index) throw std::runtime_error("Coordinate query mismatch");
        checksum ^= *found;
    }
    auto querySeconds = seconds()-loadSeconds;
    PROCESS_MEMORY_COUNTERS_EX memory{};
    GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
    std::printf("{\n  \"compressedBytes\": %zu,\n  \"inflatedBytes\": %zu,\n  \"cells\": %zu,\n"
        "  \"compactStorageBytes\": %zu,\n  \"expandedCellBytesOnly\": %zu,\n"
        "  \"peakWorkingSetBytes\": %zu,\n  \"currentWorkingSetBytes\": %zu,\n"
        "  \"parseAndCountSeconds\": %.3f,\n  \"loadAndSortSeconds\": %.3f,\n"
        "  \"lookupCount\": %zu,\n  \"lookupSeconds\": %.3f,\n  \"checksum\": %zu\n}\n",
        compressedSize,inflatedSize,storage.size(),storage.storageBytes(),storage.size()*sizeof(StructureBlock),
        memory.PeakWorkingSetSize,memory.WorkingSetSize,parseSeconds,loadSeconds,queries,querySeconds,checksum);
    return 0;
} catch (std::exception const& error) { std::fprintf(stderr,"%s\n",error.what()); return 1; }
