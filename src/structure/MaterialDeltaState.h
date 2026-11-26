// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Incremental O(1) material count tracker for total and missing blocks. Built
// once from a baseline progress snapshot and updated per cell flip; verified
// against collectRawMaterials in LHoloLogicTests.

#pragma once

#include "structure/MaterialCounts.h"
#include "structure/MaterialUnits.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

class Block;
class CompoundTag;

namespace lholo::structure::detail {

class MaterialDeltaState {
    struct SourceKey {
        Block const* block;
        CompoundTag const* nbt;
        bool operator==(SourceKey const&) const = default;
    };
    struct SourceHash {
        std::size_t operator()(SourceKey const& key) const {
            auto seed = std::hash<Block const*>{}(key.block);
            return seed ^ (std::hash<CompoundTag const*>{}(key.nbt) + 0x9e3779b9 + (seed << 6) + (seed >> 2));
        }
    };
    struct Bucket {
        Block const* block{};
        std::shared_ptr<CompoundTag const> nbt;
        std::uint64_t total{};
        std::uint64_t missing{};
    };
    struct UnitState {
        std::uint32_t bodyBucket{kNoBucket};
        std::uint32_t missingCells{};
    };

    static constexpr std::uint32_t kNoBucket = std::numeric_limits<std::uint32_t>::max();
    static constexpr std::uint32_t kNoUnit = std::numeric_limits<std::uint32_t>::max();

    std::vector<unsigned char> mIncluded;
    std::vector<unsigned char> mCorrect;
    std::vector<std::uint32_t> mBodyBucket;
    std::vector<std::uint32_t> mLiquidBucket;
    std::vector<std::uint32_t> mUnitOfCell;
    std::vector<UnitState> mUnits;
    std::vector<Bucket> mBodyBuckets;
    std::vector<Bucket> mLiquidBuckets;

    static std::uint32_t internBucket(
        std::unordered_map<SourceKey, std::uint32_t, SourceHash>& map,
        std::vector<Bucket>& buckets,
        Block const* block,
        std::shared_ptr<CompoundTag const> nbt
    ) {
        if (!block) return kNoBucket;
        auto const [it, inserted] = map.try_emplace(SourceKey{block, nbt.get()}, static_cast<std::uint32_t>(buckets.size()));
        if (inserted) buckets.push_back({block, std::move(nbt), 0, 0});
        return it->second;
    }

public:
    template <class Blocks, class IsIncluded>
    void build(
        Blocks const& blocks,
        MaterialUnitOwners const& owners,
        std::vector<unsigned char> const& initialCorrect,
        IsIncluded included
    ) {
        auto const n = blocks.size();
        mIncluded.assign(n, 0);
        mCorrect.assign(n, 0);
        mBodyBucket.assign(n, kNoBucket);
        mLiquidBucket.assign(n, kNoBucket);
        mUnitOfCell.assign(n, kNoUnit);
        mUnits.clear();
        mBodyBuckets.clear();
        mLiquidBuckets.clear();

        std::unordered_map<SourceKey, std::uint32_t, SourceHash> bodyMap;
        std::unordered_map<SourceKey, std::uint32_t, SourceHash> liquidMap;
        std::unordered_map<std::size_t, std::uint32_t> ownerToUnit;
        bool const hasGroups = !owners.empty();

        for (std::size_t i = 0; i < n; ++i) {
            if (!included(i)) continue;
            mIncluded[i] = 1;
            bool const correct = i < initialCorrect.size() && initialCorrect[i] != 0;
            mCorrect[i] = correct ? 1 : 0;

            auto const owner = hasGroups ? owners.representative(i) : std::nullopt;
            if (owner) {
                auto const [it, inserted] = ownerToUnit.try_emplace(*owner, static_cast<std::uint32_t>(mUnits.size()));
                if (inserted) {
                    auto const& source = blocks[*owner];
                    auto const bucket = internBucket(bodyMap, mBodyBuckets, source.block, source.blockEntityNbt);
                    if (bucket != kNoBucket) ++mBodyBuckets[bucket].total;
                    mUnits.push_back({bucket, 0});
                }
                auto const unitId = it->second;
                mUnitOfCell[i] = unitId;
                if (!correct) {
                    auto& unit = mUnits[unitId];
                    if (unit.missingCells == 0 && unit.bodyBucket != kNoBucket) {
                        ++mBodyBuckets[unit.bodyBucket].missing;
                    }
                    ++unit.missingCells;
                }
            } else {
                auto const& source = blocks[i];
                auto const bucket = internBucket(bodyMap, mBodyBuckets, source.block, source.blockEntityNbt);
                mBodyBucket[i] = bucket;
                if (bucket != kNoBucket) {
                    ++mBodyBuckets[bucket].total;
                    if (!correct) ++mBodyBuckets[bucket].missing;
                }
            }

            if (blocks[i].liquid) {
                auto const lBucket = internBucket(liquidMap, mLiquidBuckets, blocks[i].liquid, {});
                mLiquidBucket[i] = lBucket;
                if (lBucket != kNoBucket) {
                    ++mLiquidBuckets[lBucket].total;
                    if (!correct) ++mLiquidBuckets[lBucket].missing;
                }
            }
        }
    }

    bool applyFlip(std::size_t index, bool nowCorrect) {
        if (index >= mIncluded.size() || !mIncluded[index]) return false;
        unsigned char const next = nowCorrect ? 1 : 0;
        if (mCorrect[index] == next) return false;
        mCorrect[index] = next;

        bool changed{};
        auto const unitId = mUnitOfCell[index];
        if (unitId != kNoUnit) {
            auto& unit = mUnits[unitId];
            if (nowCorrect) {
                if (unit.missingCells > 0) {
                    --unit.missingCells;
                    if (unit.missingCells == 0 && unit.bodyBucket != kNoBucket) {
                        --mBodyBuckets[unit.bodyBucket].missing;
                        changed = true;
                    }
                }
            } else {
                if (unit.missingCells == 0 && unit.bodyBucket != kNoBucket) {
                    ++mBodyBuckets[unit.bodyBucket].missing;
                    changed = true;
                }
                ++unit.missingCells;
            }
        } else if (auto const bucket = mBodyBucket[index]; bucket != kNoBucket) {
            if (nowCorrect) --mBodyBuckets[bucket].missing;
            else ++mBodyBuckets[bucket].missing;
            changed = true;
        }

        if (auto const lBucket = mLiquidBucket[index]; lBucket != kNoBucket) {
            if (nowCorrect) --mLiquidBuckets[lBucket].missing;
            else ++mLiquidBuckets[lBucket].missing;
            changed = true;
        }
        return changed;
    }

    [[nodiscard]] RawMaterialCounts exportCounts() const {
        RawMaterialCounts out;
        out.body.reserve(mBodyBuckets.size());
        out.liquid.reserve(mLiquidBuckets.size());
        for (auto const& b : mBodyBuckets) {
            if (b.missing > 0) out.body.addCount(b.block, b.nbt, b.missing);
        }
        for (auto const& b : mLiquidBuckets) {
            if (b.missing > 0) out.liquid.addCount(b.block, b.nbt, b.missing);
        }
        return out;
    }

    [[nodiscard]] RawMaterialCounts exportFullCounts() const {
        RawMaterialCounts out;
        out.body.reserve(mBodyBuckets.size());
        out.liquid.reserve(mLiquidBuckets.size());
        for (auto const& b : mBodyBuckets) {
            if (b.total > 0) out.body.addCount(b.block, b.nbt, b.total, b.missing);
        }
        for (auto const& b : mLiquidBuckets) {
            if (b.total > 0) out.liquid.addCount(b.block, b.nbt, b.total, b.missing);
        }
        return out;
    }
};

} // namespace lholo::structure::detail
