#include "projection/core/ProjectionRules.h"
#include "projection/core/ProjectionVisibilityRules.h"
#include "projection/correction/CorrectionAirScan.h"
#include "projection/correction/CorrectionMeshRules.h"
#include "structure/StructureLoader.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <numeric>
#include <set>
#include <tuple>
#include <vector>

using namespace lholo::projection::detail;

int runCorrectionAirScanTests() {
    int checks{}, failures{};
    auto check = [&](bool value) {
        ++checks;
        if (!value) {
            ++failures;
            if (failures < 10)
                std::fprintf(stderr, "Correction air check %d failed\n", checks);
        }
    };
    for (int count : {0, 1, 63, 64, 65, 512, 4096}) {
        std::vector<std::uint16_t> cells;
        for (int i = 0; i < 4096; ++i)
            if ((i * 73) % 4096 < count)
                cells.push_back(static_cast<std::uint16_t>(i));
        CorrectionSectionIndex index;
        index.initialize(cells);
        auto mask = index.mask();
        for (int i = 0; i < 4096; ++i) {
            auto found = std::lower_bound(cells.begin(), cells.end(), i);
            auto rank = index.rank(static_cast<std::uint16_t>(i));
            bool present = found != cells.end() && *found == i;
            check(rank.has_value() == present);
            check(mask.contains(static_cast<std::uint16_t>(i)) == present);
            if (rank)
                check(*rank == static_cast<std::size_t>(found - cells.begin()));
        }
    }
    check(correctionSectionCoordinate(-1) == -1);
    check(correctionSectionCoordinate(-16) == -1);
    check(correctionSectionCoordinate(-17) == -2);
    check(correctionSectionCoordinate(16) == 1);
    for (int x = 0; x < 16; ++x)
        for (int y = 0; y < 16; ++y)
            for (int z = 0; z < 16; ++z)
                check(correctionCellSlot(x, y, z) == (x * 256 + y * 16 + z));

    for (auto previous : {CorrectionState::Unknown, CorrectionState::Missing, CorrectionState::Correct,
                          CorrectionState::WrongType, CorrectionState::WrongState}) {
        for (auto next : {CorrectionState::Unknown, CorrectionState::Missing, CorrectionState::Correct,
                          CorrectionState::WrongType, CorrectionState::WrongState}) {
            if (!correctionNeedsModelUpdate(previous, next)) {
                auto model = [](CorrectionState state) {
                    return state == CorrectionState::Unknown || state == CorrectionState::Missing;
                };
                check(model(previous) == model(next));
                check((previous == CorrectionState::Missing) == (next == CorrectionState::Missing)); // Liquid proxy.
            }
        }
    }
    check(!correctionNeedsModelUpdate(CorrectionState::WrongType, CorrectionState::WrongState));
    check(!correctionNeedsModelUpdate(CorrectionState::Correct, CorrectionState::WrongType));
    check(correctionNeedsModelUpdate(CorrectionState::Missing, CorrectionState::Correct));
    check(correctionNeedsModelUpdate(CorrectionState::WrongState, CorrectionState::Missing));
    check(!canBuildCorrectionOnly(false, 0)); // First geometry must still be built.
    check(!canBuildCorrectionOnly(true, 100));
    check(canBuildCorrectionOnly(false, 100));
    ProjectionBounds oldBounds;
    oldBounds.include({-2, -1, -3});
    oldBounds.include({18, 19, 20});
    ProjectionBounds newMarkers;
    newMarkers.include({20, 21, 22});
    newMarkers.include({21, 22, 23});
    auto combined = mergeCorrectionBounds(oldBounds, newMarkers, true);
    check(combined.valid && combined.minimum.x == -2 && combined.maximum.z == 23);
    check(combined.maximum.x >= oldBounds.maximum.x && combined.minimum.z <= oldBounds.minimum.z);
    auto removed = mergeCorrectionBounds(combined, {}, true);
    check(removed.valid && removed.maximum.z == combined.maximum.z);
    check(!mergeCorrectionBounds({}, newMarkers, true).valid);
    check(!mergeCorrectionBounds(oldBounds, newMarkers, false).valid);

    // Sparse/dense rank addresses the existing per-section global indices.
    struct Cell {
        int x, y, z;
    };
    std::vector<Cell> cells;
    for (int x = -17; x < 35; ++x)
        for (int y = 0; y < 18; ++y)
            for (int z = -1; z < 20; ++z)
                if ((x + y + z) % 7 == 0)
                    cells.push_back({x, y, z});
    std::map<std::tuple<int, int, int>, std::size_t> sectionMap;
    std::vector<std::vector<std::size_t>> sections;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        auto [found, inserted] =
            sectionMap.try_emplace(correctionSectionKey(cells[i].x, cells[i].y, cells[i].z), sections.size());
        if (inserted)
            sections.emplace_back();
        sections[found->second].push_back(i);
    }
    std::vector<CorrectionSectionIndex> indices(sections.size());
    for (std::size_t section = 0; section < sections.size(); ++section) {
        std::vector<std::uint16_t> slots;
        for (auto i : sections[section])
            slots.push_back(correctionCellSlot(cells[i].x, cells[i].y, cells[i].z));
        indices[section].initialize(std::move(slots));
    }
    for (int x = -20; x < 37; ++x)
        for (int y = -1; y < 20; ++y)
            for (int z = -2; z < 22; ++z) {
                auto found = std::lower_bound(
                    cells.begin(), cells.end(), std::tuple{x, y, z},
                    [](auto const& cell, auto const& key) { return std::tie(cell.x, cell.y, cell.z) < key; });
                auto actual = findIndexedCorrectionCell(sectionMap, indices, sections, x, y, z);
                bool present = found != cells.end() && std::tie(found->x, found->y, found->z) == std::tuple{x, y, z};
                check(actual.has_value() == present);
                if (actual)
                    check(*actual == static_cast<std::size_t>(found - cells.begin()));
            }

    using Region = lholo::structure::LoadedStructure::RegionBox;
    using Position = std::array<int, 3>;
    std::vector<Region> regions{
        {-3, -2, -1, 20, 18, 19}, {15, 1, 17, 5, 3, 7}, {100, 0, 0, 2, 2, 2}, {0, 0, 0, 0, 5, 5}};
    std::set<Position> occupied;
    std::map<Position, int> reference;
    for (auto const& box : regions)
        for (int x = box.x; x < box.x + box.sizeX; ++x)
            for (int y = box.y; y < box.y + box.sizeY; ++y)
                for (int z = box.z; z < box.z + box.sizeZ; ++z) {
                    if ((x + y + z) % 5 == 0)
                        occupied.insert({x, y, z});
                    else
                        ++reference[{x, y, z}];
                }
    auto readMask = [&](Position const& key) {
        CorrectionCellMask mask;
        for (auto const& cell : occupied)
            if (Position{correctionSectionCoordinate(cell[0]), correctionSectionCoordinate(cell[1]),
                         correctionSectionCoordinate(cell[2])} == key)
                mask.set(correctionCellSlot(cell[0], cell[1], cell[2]));
        return mask;
    };
    CorrectionAirScan scan;
    std::map<Position, int> actual;
    int iterations{};
    while (!scan.complete() && ++iterations < 20000)
        if (auto cell = scan.step(regions, readMask))
            ++actual[*cell];
    check(scan.complete());
    check(actual == reference); // Includes overlaps and the gaps between source regions.
    scan.reset();
    check(!scan.complete());
    regions.clear();
    check(!scan.step(regions, readMask));
    check(scan.complete());

    // 512^3 empty volume: one bounded bitmap build per covered section, not
    // 134 million world reads. No allocation proportional to that volume.
    regions = {{0, 0, 0, 512, 512, 512}};
    scan.reset();
    std::size_t tiles{}, steps{};
    while (!scan.complete() && ++steps < 40000) {
        if (auto cell = scan.step(regions, [](Position const&) { return CorrectionCellMask{}; })) {
            ++tiles;
            auto lo = scan.tileOrigin();
            scan.eraseBox(lo, {lo[0] + 16, lo[1] + 16, lo[2] + 16});
        }
    }
    check(scan.complete());
    check(tiles == 32 * 32 * 32);
    check(steps == tiles + 1);

    // A local tile can span eight world subchunks. Prune only the proven-empty
    // world intersection after each of the production rotation/mirror pairs.
    lholo::structure::LoadedStructure loaded;
    loaded.sizeX = 19;
    loaded.sizeY = 18;
    loaded.sizeZ = 21;
    loaded.regions = {{0, 0, 0, 19, 18, 21}};
    for (int mirror = 0; mirror < 3; ++mirror)
        for (int rotation = 0; rotation < 4; ++rotation) {
            scan.reset();
            std::set<Position> provenEmpty;
            std::set<Position> mixedCells;
            BlockPos origin{-17, -31, 33};
            std::size_t steps{};
            while (!scan.complete() && ++steps < 20000) {
                auto cell = scan.step(loaded.regions, [](Position const&) { return CorrectionCellMask{}; });
                if (!cell)
                    continue;
                auto transformed =
                    transformStructurePosition(BlockPos{(*cell)[0], (*cell)[1], (*cell)[2]}, loaded, mirror, rotation);
                Position world{origin.x + transformed.x, origin.y + transformed.y, origin.z + transformed.z};
                Position key{correctionSectionCoordinate(world[0]), correctionSectionCoordinate(world[1]),
                             correctionSectionCoordinate(world[2])};
                if ((key[0] + key[1] + key[2]) % 2 == 0) {
                    auto box = projectionSectionLocalIntersection(key, loaded, origin, mirror, rotation);
                    check(box.has_value());
                    for (int x = box->minimum[0]; x < box->maximum[0]; ++x)
                        for (int y = box->minimum[1]; y < box->maximum[1]; ++y)
                            for (int z = box->minimum[2]; z < box->maximum[2]; ++z)
                                provenEmpty.insert({x, y, z});
                    scan.eraseBox(box->minimum, box->maximum);
                } else
                    mixedCells.insert(*cell);
            }
            check(scan.complete());
            for (int x = 0; x < loaded.sizeX; ++x)
                for (int y = 0; y < loaded.sizeY; ++y)
                    for (int z = 0; z < loaded.sizeZ; ++z) {
                        Position cell{x, y, z};
                        check(provenEmpty.contains(cell) != mixedCells.contains(cell));
                    }
            check(!projectionSectionLocalIntersection({1000000, 100, 1000000}, loaded, origin, mirror, rotation));
        }
    std::printf("CorrectionAirScanTests: %d checks, %d failures\n", checks, failures);
    return failures;
}
