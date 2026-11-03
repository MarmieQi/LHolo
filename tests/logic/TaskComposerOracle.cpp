// Deliberately slow former implementation: small-fixture semantic oracle only.
#include "TaskComposerOracle.h"
#include "structure/StructureGeneration.h"
#include "structure/StructureTransform.h"
#include <algorithm>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>
namespace lholo::structure {
int coveringSource(LoadedStructure const &loaded, int x, int y, int z) {
    for (auto const &box : loaded.regions)
        if (x >= box.x && static_cast<long long>(x) < static_cast<long long>(box.x) + box.sizeX && y >= box.y &&
            static_cast<long long>(y) < static_cast<long long>(box.y) + box.sizeY && z >= box.z &&
            static_cast<long long>(z) < static_cast<long long>(box.z) + box.sizeZ)
            return box.sourceIndex;
    return -1;
}
namespace {
bool included(int index, LayerSettings const &slice) {
    switch (slice.mode) {
    case LayerDisplayMode::All:
        return true;
    case LayerDisplayMode::Single:
        return index == slice.layer;
    case LayerDisplayMode::UpToCurrent:
        return index <= slice.layer;
    case LayerDisplayMode::FromCurrent:
        return index >= slice.layer;
    }
    return false;
}
int coordinate(long long value) {
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
        throw std::runtime_error("Task coordinates exceed supported range");
    return static_cast<int>(value);
}
} // namespace
OracleComposition composeTask(std::span<CompositionFile const> files, BlockTransform const &transformBlock) {
    OracleComposition result{std::make_shared<OracleLoadedStructure>(), {}};
    auto &output = *result.loaded;
    output.composed = true;
    output.generation = nextStructureGeneration();
    std::map<std::tuple<int, int, int>, LoadedStructure::RenderBlock> winners;
    std::array<int, 3> minimum{}, maximum{};
    bool hasBounds{};
    auto include = [&](int x, int y, int z) {
        std::array<int, 3> p{x, y, z};
        if (!hasBounds) {
            minimum = maximum = p;
            hasBounds = true;
        } else
            for (int i = 0; i < 3; ++i) {
                minimum[i] = std::min(minimum[i], p[i]);
                maximum[i] = std::max(maximum[i], p[i]);
            }
    };
    for (std::size_t source = 0; source < files.size(); ++source) {
        auto const &file = files[source];
        output.sourceFiles.push_back(file.id);
        if (!file.visible || !file.loaded)
            continue;
        auto const &loaded = *file.loaded;
        if (loaded.sizeX > 0 && loaded.sizeY > 0 && loaded.sizeZ > 0) {
            bool const rotated = (file.rotation & 1) != 0;
            std::array<long long, 3> const minimum{file.x, file.y, file.z};
            output.sourceBounds.push_back(
                {static_cast<int>(source),
                 minimum,
                 {minimum[0] + (rotated ? loaded.sizeZ : loaded.sizeX), minimum[1] + loaded.sizeY,
                  minimum[2] + (rotated ? loaded.sizeX : loaded.sizeZ)}});
        }
        auto position = [&](int x, int y, int z) {
            auto p = transformCell(x, y, z, loaded.sizeX, loaded.sizeZ, file.mirror, file.rotation);
            p[0] = coordinate(static_cast<long long>(p[0]) + file.x);
            p[1] = coordinate(static_cast<long long>(p[1]) + file.y);
            p[2] = coordinate(static_cast<long long>(p[2]) + file.z);
            return p;
        };
        for (auto const &original : loaded.regions) {
            auto box = original;
            if (file.slice.mode != LayerDisplayMode::All) {
                if (file.slice.axis == LayerAxis::Material)
                    continue;
                int &start = file.slice.axis == LayerAxis::X ? box.x : box.y;
                int &size = file.slice.axis == LayerAxis::X ? box.sizeX : box.sizeY;
                int end = coordinate(static_cast<long long>(start) + size);
                if (file.slice.mode == LayerDisplayMode::Single) {
                    start = std::max(start, file.slice.layer);
                    end = std::min<long long>(end, static_cast<long long>(file.slice.layer) + 1);
                }
                if (file.slice.mode == LayerDisplayMode::UpToCurrent)
                    end = static_cast<int>(std::min<long long>(end, static_cast<long long>(file.slice.layer) + 1));
                if (file.slice.mode == LayerDisplayMode::FromCurrent)
                    start = std::max(start, file.slice.layer);
                size = end - start;
            }
            if (box.sizeX <= 0 || box.sizeY <= 0 || box.sizeZ <= 0)
                continue;
            auto a = position(box.x, box.y, box.z),
                 b = position(box.x + box.sizeX - 1, box.y + box.sizeY - 1, box.z + box.sizeZ - 1);
            LoadedStructure::RegionBox region;
            region.x = std::min(a[0], b[0]);
            region.y = std::min(a[1], b[1]);
            region.z = std::min(a[2], b[2]);
            region.sizeX = coordinate(std::abs(static_cast<long long>(b[0]) - a[0]) + 1);
            region.sizeY = coordinate(std::abs(static_cast<long long>(b[1]) - a[1]) + 1);
            region.sizeZ = coordinate(std::abs(static_cast<long long>(b[2]) - a[2]) + 1);
            region.sourceIndex = static_cast<int>(source);
            output.regions.push_back(region);
            include(region.x, region.y, region.z);
            include(std::max(a[0], b[0]), std::max(a[1], b[1]), std::max(a[2], b[2]));
        }
        for (auto const &entry : loaded.renderBlocks) {
            if (!entry.block && !entry.liquid)
                continue;
            int layer = file.slice.axis == LayerAxis::X ? entry.x : entry.y;
            if (file.slice.axis == LayerAxis::Material) {
                if (file.slice.mode != LayerDisplayMode::All &&
                    !((entry.materialIndex >= 0 && included(entry.materialIndex, file.slice)) ||
                      (entry.liquidMaterialIndex >= 0 && included(entry.liquidMaterialIndex, file.slice))))
                    continue;
            } else if (!included(layer, file.slice))
                continue;
            auto p = position(entry.x, entry.y, entry.z);
            auto [it, inserted] = winners.try_emplace({p[0], p[1], p[2]}, entry);
            if (!inserted)
                continue;
            auto &winner = it->second;
            winner.x = p[0];
            winner.y = p[1];
            winner.z = p[2];
            winner.sourceIndex = static_cast<int>(source);
            winner.block = transformBlock(entry.block, file.mirror, file.rotation);
            winner.liquid = transformBlock(entry.liquid, file.mirror, file.rotation);
            include(p[0], p[1], p[2]);
        }
    }
    result.origin = minimum;
    for (auto &bounds : output.sourceBounds)
        for (int axis = 0; axis < 3; ++axis) {
            bounds.minimum[axis] -= minimum[axis];
            bounds.maximum[axis] -= minimum[axis];
        }
    output.sizeX = hasBounds ? coordinate(static_cast<long long>(maximum[0]) - minimum[0] + 1) : 0;
    output.sizeY = hasBounds ? coordinate(static_cast<long long>(maximum[1]) - minimum[1] + 1) : 0;
    output.sizeZ = hasBounds ? coordinate(static_cast<long long>(maximum[2]) - minimum[2] + 1) : 0;
    output.volume = static_cast<std::uint64_t>(output.sizeX) * output.sizeY * output.sizeZ;
    for (auto &box : output.regions) {
        box.x = coordinate(static_cast<long long>(box.x) - minimum[0]);
        box.y = coordinate(static_cast<long long>(box.y) - minimum[1]);
        box.z = coordinate(static_cast<long long>(box.z) - minimum[2]);
    }
    std::map<Block const *, int> materials;
    auto material = [&](Block const *block) {
        if (!block)
            return -1;
        auto [it, _] = materials.try_emplace(block, static_cast<int>(materials.size()));
        return it->second;
    };
    output.renderBlocks.reserve(winners.size());
    for (auto &[key, entry] : winners) {
        entry.x = coordinate(static_cast<long long>(entry.x) - minimum[0]);
        entry.y = coordinate(static_cast<long long>(entry.y) - minimum[1]);
        entry.z = coordinate(static_cast<long long>(entry.z) - minimum[2]);
        entry.materialIndex = material(entry.block);
        entry.liquidMaterialIndex = material(entry.liquid);
        output.primaryBlocks += entry.block != nullptr;
        output.secondaryBlocks += entry.liquid != nullptr;
        output.renderBlocks.push_back(std::move(entry));
    }
    output.materialCount = materials.size();
    output.paletteEntries = materials.size();
    return result;
}
} // namespace lholo::structure
