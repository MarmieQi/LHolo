#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

class Block;
class CompoundTag;

namespace lholo::structure {

struct StructureBlock {
    int x{}, y{}, z{};
    Block const* block{};
    Block const* liquid{};
    std::shared_ptr<CompoundTag const> blockEntityNbt;
    int materialIndex{-1}, liquidMaterialIndex{-1}, sourceIndex{-1};
};

// Published structure cells are values, not stable addresses. Large sources
// retain one packed coordinate/palette ID per occupied cell instead of one
// RenderBlock (including two pointers and a shared_ptr) per cell. XYZ ordering
// and global cell indices are identical in both representations.
class StructureBlocks {
public:
    using value_type = StructureBlock;
    StructureBlocks() = default;
    StructureBlocks(std::initializer_list<value_type> values) : mValues(values) {}
    StructureBlocks& operator=(std::initializer_list<value_type> values) {
        *this = StructureBlocks(values);
        return *this;
    }
    std::size_t size() const { return mCompact ? mCells.size() : mValues.size(); }
    bool empty() const { return size() == 0; }
    void reserve(std::size_t count) { mutableValues().reserve(count); }
    void push_back(value_type value) { mutableValues().push_back(std::move(value)); }
    std::vector<value_type>& mutableValues() {
        if (mCompact) throw std::logic_error("Cannot mutate published compact structure cells");
        return mValues;
    }
    value_type front() const { return (*this)[0]; }
    value_type back() const { return (*this)[size() - 1]; }
    value_type operator[](std::size_t index) const {
        if (!mCompact) return mValues[index];
        auto cell = mCells[index];
        auto result = mPalette[static_cast<std::size_t>(cell & mPaletteMask)];
        auto coordinate = cell >> mPaletteBits;
        result.z = static_cast<int>(coordinate % mSizeZ);
        coordinate /= mSizeZ;
        result.y = static_cast<int>(coordinate % mSizeY);
        result.x = static_cast<int>(coordinate / mSizeY);
        return result;
    }
    class const_iterator {
        StructureBlocks const* mOwner{};
        std::size_t mIndex{};
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = StructureBlock;
        using difference_type = std::ptrdiff_t;
        using reference = value_type;
        using pointer = void;
        const_iterator() = default;
        const_iterator(StructureBlocks const* owner, std::size_t index) : mOwner(owner), mIndex(index) {}
        value_type operator*() const { return (*mOwner)[mIndex]; }
        const_iterator& operator++() { ++mIndex; return *this; }
        const_iterator operator++(int) { auto old = *this; ++*this; return old; }
        bool operator==(const_iterator const&) const = default;
    };
    const_iterator begin() const { return {this, 0}; }
    const_iterator end() const { return {this, size()}; }

    // The loader determines a conservative palette bound before allocating any
    // cells. Never truncate wide coordinates or silently wrap a packed key.
    bool beginCompact(int sizeY, int sizeZ, std::uint64_t maximumCoordinate,
                      std::uint64_t maximumPaletteEntries) {
        auto bits = std::max(1u, static_cast<unsigned>(std::bit_width(maximumPaletteEntries)));
        if (sizeY <= 0 || sizeZ <= 0 || bits > 32 ||
            maximumCoordinate > (std::numeric_limits<std::uint64_t>::max() >> bits)) return false;
        if (!empty()) throw std::logic_error("Compact builder requires empty storage");
        mCompact = true;
        mSizeY = static_cast<std::uint64_t>(sizeY);
        mSizeZ = static_cast<std::uint64_t>(sizeZ);
        mPaletteBits = bits;
        mPaletteMask = (std::uint64_t{1} << bits) - 1;
        mMaximumCoordinate = maximumCoordinate;
        return true;
    }
    std::uint32_t addPalette(value_type value) {
        if (!mCompact || mPalette.size() > mPaletteMask)
            throw std::length_error("Structure palette exceeds packed capacity");
        mHasNbt = mHasNbt || bool(value.blockEntityNbt);
        mPalette.push_back(std::move(value));
        return static_cast<std::uint32_t>(mPalette.size() - 1);
    }
    void reserveCompact(std::size_t count) { mCells.reserve(count); }
    void appendCompact(std::uint64_t coordinate, std::uint32_t palette) {
        if (!mCompact || palette >= mPalette.size() || coordinate > mMaximumCoordinate)
            throw std::out_of_range("Invalid packed structure cell");
        mCells.push_back((coordinate << mPaletteBits) | palette);
    }
    void mergeCompactRegion() {
        // Keep only current winners between regions. Holding every duplicate
        // from all overlapping regions would make memory grow with their sum.
        auto middle = mCells.begin() + mSortedSize;
        std::sort(middle, mCells.end());
        if (mSortedSize) std::inplace_merge(mCells.begin(),middle,mCells.end());
        // Region palettes are appended in source order. A higher palette ID
        // therefore preserves the previous later-non-air-region winner rule.
        std::size_t output{};
        for (std::size_t i = 0; i < mCells.size();) {
            auto end = i + 1;
            while (end < mCells.size() && (mCells[end] >> mPaletteBits) == (mCells[i] >> mPaletteBits)) ++end;
            mCells[output++] = mCells[end - 1];
            i = end;
        }
        mCells.resize(output);
        mSortedSize = output;
        if (mHasNbt) {
            std::vector<bool> used(mPalette.size());
            for (auto cell : mCells) used[static_cast<std::size_t>(cell & mPaletteMask)] = true;
            for (std::size_t i = 0; i < used.size(); ++i)
                if (!used[i]) mPalette[i].blockEntityNbt.reset();
        }
    }
    void finishCompact() {
        if (mSortedSize != mCells.size()) mergeCompactRegion();
        if (mCells.empty()) return;
        auto rows = mMaximumCoordinate / mSizeZ + 1;
        // A bounded row directory keeps hot neighbor queries within one Z row
        // (instead of binary-searching tens of millions of distant cells).
        if (rows <= 2 * 1024 * 1024) {
            mRowOffsets.assign(static_cast<std::size_t>(rows + 1), 0);
            for (auto cell : mCells) ++mRowOffsets[static_cast<std::size_t>((cell >> mPaletteBits) / mSizeZ) + 1];
            for (std::size_t i = 1; i < mRowOffsets.size(); ++i) mRowOffsets[i] += mRowOffsets[i - 1];
        }
    }
    std::optional<std::size_t> find(int x, int y, int z) const {
        if (x < 0 || y < 0 || z < 0) return {};
        if (mCompact) {
            if (static_cast<std::uint64_t>(y) >= mSizeY || static_cast<std::uint64_t>(z) >= mSizeZ) return {};
            if (static_cast<std::uint64_t>(x) > mMaximumCoordinate / mSizeY / mSizeZ) return {};
            auto coordinate = (static_cast<std::uint64_t>(x) * mSizeY + y) * mSizeZ + z;
            if (coordinate > mMaximumCoordinate) return {};
            auto first = mCells.begin(), last = mCells.end();
            if (!mRowOffsets.empty()) {
                auto row = static_cast<std::size_t>(coordinate / mSizeZ);
                first += mRowOffsets[row]; last = mCells.begin() + mRowOffsets[row + 1];
            }
            auto found = std::lower_bound(first, last, coordinate << mPaletteBits);
            if (found != last && (*found >> mPaletteBits) == coordinate)
                return static_cast<std::size_t>(found - mCells.begin());
            return {};
        }
        auto found = std::lower_bound(mValues.begin(), mValues.end(), std::array{x, y, z},
            [](value_type const& a, auto const& b) { return std::array{a.x, a.y, a.z} < b; });
        if (found != mValues.end() && found->x == x && found->y == y && found->z == z)
            return static_cast<std::size_t>(found - mValues.begin());
        return {};
    }
    template<class F> void forEachValue(F visit) {
        for (auto& value : mCompact ? mPalette : mValues) visit(value);
    }
    template<class F> void forEachStoredValue(F visit) const {
        for (auto const& value : mCompact ? mPalette : mValues) visit(value);
    }
    template<class F> void forEachCountedValue(F visit) const {
        if (!mCompact) { for (auto const& value : mValues) visit(value, std::uint64_t{1}); return; }
        std::vector<std::uint64_t> counts(mPalette.size());
        for (auto cell : mCells) ++counts[static_cast<std::size_t>(cell & mPaletteMask)];
        for (std::size_t i = 0; i < counts.size(); ++i) if (counts[i]) visit(mPalette[i], counts[i]);
    }
    std::size_t storageBytes() const {
        return mValues.capacity() * sizeof(value_type) + mCells.capacity() * sizeof(std::uint64_t)
            + mPalette.capacity() * sizeof(value_type) + mRowOffsets.capacity() * sizeof(std::size_t);
    }
private:
    std::vector<value_type> mValues, mPalette;
    std::vector<std::uint64_t> mCells;
    std::vector<std::size_t> mRowOffsets;
    std::uint64_t mSizeY{}, mSizeZ{}, mPaletteMask{}, mMaximumCoordinate{};
    unsigned mPaletteBits{};
    std::size_t mSortedSize{};
    bool mCompact{};
    bool mHasNbt{};
};
} // namespace lholo::structure
