#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace lholo::structure::write {
enum class WriteMode { LocalWorld, ServerCommands };
enum class WriteResult { None, Complete, NoLocalWorld, ContextChanged, NoSelection, UnmappedBlocks, RegionNotLoaded, HeightLimit, CoordinateLimit, TemplateFailed, Failed };
struct WriteCompletion { WriteResult result; std::string detail; };
struct WriteTarget {
    std::array<int, 3> position{};
    int dimension{}, rotation{}, mirror{};
};
// Transport data owns its NBT bytes. No client registry/Level pointers cross threads.
struct WriteCell {
    std::array<int, 3> position{};
    int block{-1}, liquid{-1};
    std::string blockEntityNbt;
};
struct WriteRegion { std::array<int, 3> minimum{}, size{}; };
struct StructureWritePlan {
    WriteTarget target;
    std::array<int, 3> size{};
    std::array<int, 3> sourceWorldOrigin{};
    std::vector<WriteRegion> regions;
    std::vector<std::uint64_t> omittedCells;
    std::vector<std::string> palette;
    std::vector<WriteCell> cells;
    bool skipAir{};
};
}
