#pragma once
#include "structure/write/StructureWriteTypes.h"
#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace lholo::structure::write {

struct LocalChunkCandidate {
    std::size_t index{};
    int cx{}, cz{};
    std::array<int, 3> worldMin{};
    std::array<int, 3> worldMax{};
};

class LocalWriteSession {
public:
    explicit LocalWriteSession(StructureWritePlan plan);

    WriteProgress const& status() const { return mStatus; }
    int dimension() const { return mSource.target.dimension; }
    std::array<int, 2> yBounds() const { return {mMinY, mMaxY}; }

    void fillPendingOrder(std::optional<std::array<int, 3>> const& playerFeet, std::vector<LocalChunkCandidate>& out) const;
    std::optional<StructureWritePlan> makeChunkPlan(std::size_t chunkIndex) const;
    void completeChunk(std::size_t chunkIndex);
    void setWaitingChunks();
    void setWaitingDimension();
    void resume();
    void cancel();

private:
    struct ChunkSlice {
        int cx{}, cz{};
        std::array<int, 3> worldMin{};
        std::array<int, 3> worldMax{};
        std::array<int, 3> localMin{};
        std::array<int, 3> localMax{};
        std::vector<std::size_t> cellIndices;
        bool done{};
    };

    StructureWritePlan mSource;
    WriteProgress mStatus;
    std::vector<ChunkSlice> mChunks;
    int mMinY{}, mMaxY{};
};

} // namespace lholo::structure::write
