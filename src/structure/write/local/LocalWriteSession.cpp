#include "structure/write/local/LocalWriteSession.h"
#include "structure/StructureTransform.h"
#include "structure/write/StructureWritePlan.h"
#include <algorithm>
#include <climits>
#include <map>
#include <unordered_map>
#include <utility>

namespace lholo::structure::write {
namespace {

std::array<int, 3> transformWorld(std::array<int, 3> const& local, StructureWritePlan const& plan) {
    auto p = structure::transformCell(
        local[0], local[1], local[2], plan.size[0], plan.size[2], plan.target.mirror, plan.target.rotation);
    for (int axis = 0; axis < 3; ++axis) p[axis] += plan.target.position[axis];
    return p;
}

void boxWorldBounds(
    std::array<int, 3> const& localMin,
    std::array<int, 3> const& localMax,
    StructureWritePlan const& plan,
    std::array<int, 3>& worldMin,
    std::array<int, 3>& worldMax
) {
    auto const a = transformWorld(localMin, plan);
    auto const b = transformWorld(localMax, plan);
    for (int axis = 0; axis < 3; ++axis) {
        worldMin[axis] = std::min(a[axis], b[axis]);
        worldMax[axis] = std::max(a[axis], b[axis]);
    }
}

void boxLocalBounds(
    std::array<int, 3> const& worldMin,
    std::array<int, 3> const& worldMax,
    StructureWritePlan const& plan,
    std::array<int, 3>& localMin,
    std::array<int, 3>& localMax
) {
    auto const a = structure::inverseTransformCell(
        worldMin[0] - plan.target.position[0],
        worldMin[1] - plan.target.position[1],
        worldMin[2] - plan.target.position[2],
        plan.size[0], plan.size[2], plan.target.mirror, plan.target.rotation);
    auto const b = structure::inverseTransformCell(
        worldMax[0] - plan.target.position[0],
        worldMax[1] - plan.target.position[1],
        worldMax[2] - plan.target.position[2],
        plan.size[0], plan.size[2], plan.target.mirror, plan.target.rotation);
    for (int axis = 0; axis < 3; ++axis) {
        localMin[axis] = std::min(a[axis], b[axis]);
        localMax[axis] = std::max(a[axis], b[axis]);
    }
}

} // namespace

LocalWriteSession::LocalWriteSession(StructureWritePlan plan)
    : mSource(std::move(plan)) {
    std::array<int, 3> worldMax{};
    if (!writeBounds(mSource.target, mSource.size, worldMax)) {
        mSource = {};
        return;
    }
    auto const& worldMin = mSource.target.position;

    struct Accumulator {
        std::vector<std::size_t> cellIndices;
        int minY{INT_MAX};
        int maxY{INT_MIN};
    };
    std::map<std::array<int, 2>, Accumulator> grouped;

    for (std::size_t i = 0; i < mSource.cells.size(); ++i) {
        auto const wp = transformWorld(mSource.cells[i].position, mSource);
        auto& acc = grouped[{chunkCoordinate(wp[0]), chunkCoordinate(wp[2])}];
        acc.cellIndices.push_back(i);
        acc.minY = std::min(acc.minY, wp[1]);
        acc.maxY = std::max(acc.maxY, wp[1]);
    }

    if (!mSource.skipAir) {
        for (auto const& region : mSource.regions) {
            if (region.size[0] <= 0 || region.size[1] <= 0 || region.size[2] <= 0) continue;
            std::array<int, 3> const regMax{
                region.minimum[0] + region.size[0] - 1,
                region.minimum[1] + region.size[1] - 1,
                region.minimum[2] + region.size[2] - 1
            };
            std::array<int, 3> rMin{}, rMax{};
            boxWorldBounds(region.minimum, regMax, mSource, rMin, rMax);
            int const cx0 = chunkCoordinate(rMin[0]), cx1 = chunkCoordinate(rMax[0]);
            int const cz0 = chunkCoordinate(rMin[2]), cz1 = chunkCoordinate(rMax[2]);
            for (int cx = cx0; cx <= cx1; ++cx) {
                for (int cz = cz0; cz <= cz1; ++cz) {
                    auto& acc = grouped[{cx, cz}];
                    acc.minY = std::min(acc.minY, rMin[1]);
                    acc.maxY = std::max(acc.maxY, rMax[1]);
                }
            }
        }
    }

    if (grouped.empty()) {
        mStatus.state = WriteState::Complete;
        mSource = {};
        return;
    }

    mMinY = INT_MAX;
    mMaxY = INT_MIN;
    mChunks.reserve(grouped.size());
    for (auto& [pos, acc] : grouped) {
        ChunkSlice slice;
        slice.cx = pos[0];
        slice.cz = pos[1];
        slice.worldMin = {std::max(worldMin[0], pos[0] * 16), acc.minY, std::max(worldMin[2], pos[1] * 16)};
        slice.worldMax = {std::min(worldMax[0], pos[0] * 16 + 15), acc.maxY, std::min(worldMax[2], pos[1] * 16 + 15)};
        boxLocalBounds(slice.worldMin, slice.worldMax, mSource, slice.localMin, slice.localMax);
        slice.cellIndices = std::move(acc.cellIndices);
        mMinY = std::min(mMinY, slice.worldMin[1]);
        mMaxY = std::max(mMaxY, slice.worldMax[1]);
        mChunks.push_back(std::move(slice));
    }

    mStatus.total = mChunks.size();
    mStatus.active = true;
    mStatus.state = WriteState::Running;
}

void LocalWriteSession::fillPendingOrder(
    std::optional<std::array<int, 3>> const& playerFeet,
    std::vector<LocalChunkCandidate>& out
) const {
    out.clear();
    for (std::size_t i = 0; i < mChunks.size(); ++i) {
        if (mChunks[i].done) continue;
        auto const& c = mChunks[i];
        out.push_back({i, c.cx, c.cz, c.worldMin, c.worldMax});
    }
    if (!playerFeet || out.size() <= 1) return;
    int const pcx = chunkCoordinate((*playerFeet)[0]);
    int const pcz = chunkCoordinate((*playerFeet)[2]);
    std::sort(out.begin(), out.end(), [=](LocalChunkCandidate const& a, LocalChunkCandidate const& b) {
        auto const dist = [=](LocalChunkCandidate const& c) {
            auto const dx = static_cast<long long>(c.cx) - pcx;
            auto const dz = static_cast<long long>(c.cz) - pcz;
            return dx * dx + dz * dz;
        };
        auto const da = dist(a), db = dist(b);
        return da != db ? da < db : a.index < b.index;
    });
}

std::optional<StructureWritePlan> LocalWriteSession::makeChunkPlan(std::size_t chunkIndex) const {
    auto const& chunk = mChunks[chunkIndex];
    auto const& lMin = chunk.localMin;
    auto const& lMax = chunk.localMax;
    int const subX = lMax[0] - lMin[0] + 1;
    int const subY = lMax[1] - lMin[1] + 1;
    int const subZ = lMax[2] - lMin[2] + 1;

    StructureWritePlan sub;
    sub.target = {chunk.worldMin, mSource.target.dimension, mSource.target.rotation, mSource.target.mirror};
    sub.size = {subX, subY, subZ};
    sub.sourceWorldOrigin = mSource.sourceWorldOrigin;
    sub.skipAir = mSource.skipAir;

    std::unordered_map<int, int> paletteRemap;
    auto remapPalette = [&](int index) -> int {
        if (index < 0) return -1;
        auto [it, added] = paletteRemap.try_emplace(index, static_cast<int>(sub.palette.size()));
        if (added) sub.palette.push_back(mSource.palette[index]);
        return it->second;
    };

    sub.cells.reserve(chunk.cellIndices.size());
    for (std::size_t cellIdx : chunk.cellIndices) {
        auto const& cell = mSource.cells[cellIdx];
        sub.cells.push_back({
            {cell.position[0] - lMin[0], cell.position[1] - lMin[1], cell.position[2] - lMin[2]},
            remapPalette(cell.block),
            remapPalette(cell.liquid),
            cell.blockEntityNbt
        });
    }

    std::uint64_t regionVolume = 0;
    if (!mSource.skipAir) {
        for (auto const& region : mSource.regions) {
            int const ix0 = std::max(lMin[0], region.minimum[0]);
            int const iy0 = std::max(lMin[1], region.minimum[1]);
            int const iz0 = std::max(lMin[2], region.minimum[2]);
            int const ix1 = std::min(lMax[0], region.minimum[0] + region.size[0] - 1);
            int const iy1 = std::min(lMax[1], region.minimum[1] + region.size[1] - 1);
            int const iz1 = std::min(lMax[2], region.minimum[2] + region.size[2] - 1);
            if (ix0 > ix1 || iy0 > iy1 || iz0 > iz1) continue;
            int const sx = ix1 - ix0 + 1, sy = iy1 - iy0 + 1, sz = iz1 - iz0 + 1;
            sub.regions.push_back({{ix0 - lMin[0], iy0 - lMin[1], iz0 - lMin[2]}, {sx, sy, sz}});
            regionVolume += static_cast<std::uint64_t>(sx) * sy * sz;
        }
    }

    if (!mSource.omittedCells.empty()) {
        auto const sy = static_cast<std::uint64_t>(mSource.size[1]);
        auto const sz = static_cast<std::uint64_t>(mSource.size[2]);
        auto const subSy = static_cast<std::uint64_t>(subY);
        auto const subSz = static_cast<std::uint64_t>(subZ);
        for (int lx = lMin[0]; lx <= lMax[0]; ++lx) {
            for (int ly = lMin[1]; ly <= lMax[1]; ++ly) {
                std::uint64_t const rowBegin = (static_cast<std::uint64_t>(lx) * sy + ly) * sz + lMin[2];
                std::uint64_t const rowEnd = rowBegin + subZ;
                auto it = std::lower_bound(mSource.omittedCells.begin(), mSource.omittedCells.end(), rowBegin);
                for (; it != mSource.omittedCells.end() && *it < rowEnd; ++it) {
                    auto const lzOffset = *it - rowBegin;
                    auto const subIndex =
                        (static_cast<std::uint64_t>(lx - lMin[0]) * subSy + (ly - lMin[1])) * subSz + lzOffset;
                    sub.omittedCells.push_back(subIndex);
                }
            }
        }
    }

    bool const hasAirToPlace = !mSource.skipAir && !sub.regions.empty() && sub.omittedCells.size() < regionVolume;
    if (sub.cells.empty() && !hasAirToPlace) return std::nullopt;
    return sub;
}

void LocalWriteSession::completeChunk(std::size_t chunkIndex) {
    if (!mStatus.active || chunkIndex >= mChunks.size() || mChunks[chunkIndex].done) return;
    mChunks[chunkIndex].done = true;
    ++mStatus.done;
    if (mStatus.done == mStatus.total) {
        mStatus.state = WriteState::Complete;
        mStatus.active = false;
        mStatus.canResume = false;
        mStatus.error.clear();
        mSource = {};
        mChunks.clear();
    } else {
        mStatus.state = WriteState::Running;
        mStatus.canResume = false;
        mStatus.error.clear();
    }
}

void LocalWriteSession::setWaitingChunks() {
    if (!mStatus.active) return;
    mStatus.state = WriteState::WaitingChunks;
    mStatus.canResume = true;
    mStatus.error.clear();
}

void LocalWriteSession::setWaitingDimension() {
    if (!mStatus.active) return;
    mStatus.state = WriteState::WaitingDimension;
    mStatus.canResume = false;
    mStatus.error.clear();
}

void LocalWriteSession::resume() {
    if (!mStatus.active || !mStatus.canResume) return;
    mStatus.state = WriteState::Running;
    mStatus.canResume = false;
    mStatus.error.clear();
}

void LocalWriteSession::cancel() {
    mSource = {};
    mChunks.clear();
    mStatus.active = false;
    mStatus.canResume = false;
    mStatus.state = WriteState::Cancelled;
    mStatus.error.clear();
}

} // namespace lholo::structure::write
