#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

namespace lholo::projection::detail {

inline int correctionSectionCoordinate(int coordinate) { return coordinate / 16 - (coordinate % 16 < 0 ? 1 : 0); }
inline auto correctionSectionKey(int x, int y, int z) {
    return std::tuple{correctionSectionCoordinate(x), correctionSectionCoordinate(y), correctionSectionCoordinate(z)};
}
inline std::uint16_t correctionCellSlot(int x, int y, int z) {
    // XYZ order matches LoadedStructure's sorted render-block sequence.
    return static_cast<std::uint16_t>(((x & 15) << 8) | ((y & 15) << 4) | (z & 15));
}

struct CorrectionCellMask {
    std::array<std::uint64_t, 64> words{};
    void set(std::uint16_t cell) { words[cell >> 6] |= std::uint64_t{1} << (cell & 63); }
    bool contains(std::uint16_t cell) const { return (words[cell >> 6] & (std::uint64_t{1} << (cell & 63))) != 0; }
    // Coordinates are tile-local, maximum exclusive. Each Z row fits one word.
    void box(std::array<int, 3> lo, std::array<int, 3> hi, bool value) {
        for (int axis = 0; axis < 3; ++axis) {
            lo[axis] = std::clamp(lo[axis], 0, 16);
            hi[axis] = std::clamp(hi[axis], 0, 16);
            if (lo[axis] >= hi[axis])
                return;
        }
        auto bits = ((std::uint64_t{1} << (hi[2] - lo[2])) - 1) << lo[2];
        for (int x = lo[0]; x < hi[0]; ++x)
            for (int y = lo[1]; y < hi[1]; ++y) {
                auto index = (x << 8) | (y << 4);
                auto mask = bits << (index & 63);
                if (value)
                    words[index >> 6] |= mask;
                else
                    words[index >> 6] &= ~mask;
            }
    }
    void subtract(CorrectionCellMask const& other) {
        for (std::size_t i = 0; i < words.size(); ++i)
            words[i] &= ~other.words[i];
    }
    std::optional<std::uint16_t> take(std::size_t& word) {
        while (word < words.size() && !words[word])
            ++word;
        if (word == words.size())
            return std::nullopt;
        auto bit = std::countr_zero(words[word]);
        words[word] &= words[word] - 1;
        return static_cast<std::uint16_t>(word * 64 + bit);
    }
};

// Sparse sections keep at most 64 local slots; dense sections use a bitmap and
// 64 small prefix counts. Rank indexes the existing sectionBlockIndices array.
class CorrectionSectionIndex {
    struct Dense {
        CorrectionCellMask mask;
        std::array<std::uint16_t, 64> prefix{};
    };

public:
    void initialize(std::vector<std::uint16_t> slots) {
        assert(std::is_sorted(slots.begin(), slots.end()));
        mSparse.clear();
        mDense.reset();
        if (slots.size() <= 64) {
            mSparse = std::move(slots);
            return;
        }
        mDense = std::make_unique<Dense>();
        for (auto cell : slots)
            mDense->mask.set(cell);
        std::uint16_t count{};
        for (std::size_t word = 0; word < 64; ++word) {
            mDense->prefix[word] = count;
            count = static_cast<std::uint16_t>(count + std::popcount(mDense->mask.words[word]));
        }
    }
    std::optional<std::size_t> rank(std::uint16_t cell) const {
        if (!mDense) {
            auto found = std::lower_bound(mSparse.begin(), mSparse.end(), cell);
            if (found == mSparse.end() || *found != cell)
                return std::nullopt;
            return static_cast<std::size_t>(found - mSparse.begin());
        }
        auto bit = std::uint64_t{1} << (cell & 63);
        auto word = mDense->mask.words[cell >> 6];
        if (!(word & bit))
            return std::nullopt;
        return mDense->prefix[cell >> 6] + std::popcount(word & (bit - 1));
    }
    CorrectionCellMask mask() const {
        if (mDense)
            return mDense->mask;
        CorrectionCellMask result;
        for (auto cell : mSparse)
            result.set(cell);
        return result;
    }

private:
    std::vector<std::uint16_t> mSparse;
    std::unique_ptr<Dense> mDense;
};

template <class SectionMap, class Sections>
std::optional<std::size_t> findIndexedCorrectionCell(SectionMap const& map,
                                                     std::span<CorrectionSectionIndex const> masks,
                                                     Sections const& indices, int x, int y, int z) {
    auto found = map.find(correctionSectionKey(x, y, z));
    if (found == map.end() || found->second >= masks.size())
        return std::nullopt;
    auto rank = masks[found->second].rank(correctionCellSlot(x, y, z));
    return rank ? std::optional<std::size_t>{indices[found->second][*rank]} : std::nullopt;
}

} // namespace lholo::projection::detail
