#pragma once

#include <array>
#include <cstddef>
#include <optional>

class BlockSource;
class BlockPos;

namespace lholo::projection::detail {

// One correction quantum only. Caches values, never native chunk/storage
// pointers. An uncertain/unavailable chunk always uses ordinary comparison.
class CorrectionWorldReader {
public:
    explicit CorrectionWorldReader(BlockSource& source) : mSource(source) {}
    bool empty(BlockPos const& position);
    std::array<int, 3> emptySection() const { return *mSection; }
    std::size_t probes() const { return mProbes; }

private:
    BlockSource& mSource;
    std::optional<std::array<int, 3>> mSection;
    bool mEmpty{};
    std::size_t mProbes{};
};

} // namespace lholo::projection::detail
