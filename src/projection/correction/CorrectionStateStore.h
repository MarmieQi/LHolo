#pragma once

#include "projection/correction/CorrectionState.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace lholo::projection::detail {

// Layout is immutable for one structure. Snapshots retain only selected pages,
// never a copy of the whole state array or the whole page-pointer table.
class CorrectionStateStore {
    struct Location {
        std::uint32_t section{};
        std::uint32_t slot{};
    };
    struct Page {
        std::uint64_t epoch{};
        std::vector<CorrectionState> states;
        std::vector<std::uint64_t> visible;
        std::size_t visibleCount{};
        std::size_t nonMissingCount{};

        bool isVisible(std::size_t slot) const { return (visible[slot / 64] & (std::uint64_t{1} << (slot % 64))) != 0; }
        std::size_t byteSize() const {
            return states.size() * sizeof(CorrectionState) + visible.size() * sizeof(std::uint64_t);
        }
    };
    using Layout = std::vector<Location>;

public:
    CorrectionStateStore() = default;
    CorrectionStateStore(CorrectionStateStore const&) = delete;
    CorrectionStateStore& operator=(CorrectionStateStore const&) = delete;
    CorrectionStateStore(CorrectionStateStore&&) = default;
    CorrectionStateStore& operator=(CorrectionStateStore&&) = default;

    class Snapshot {
        friend class CorrectionStateStore;
        struct Entry {
            std::size_t section;
            std::shared_ptr<Page const> page;
        };
        std::shared_ptr<Layout const> mLayout;
        std::vector<Entry> mPages;
        std::uint64_t mEpoch{};
        CorrectionState mDefault{CorrectionState::Missing};

      public:
        CorrectionState rawState(std::size_t index) const {
            auto const location = mLayout->at(index);
            auto const found =
                std::lower_bound(mPages.begin(), mPages.end(), location.section,
                                 [](Entry const &entry, std::size_t section) { return entry.section < section; });
            if (found == mPages.end() || found->section != location.section)
                throw std::logic_error("Projection correction snapshot does not contain the requested section");
            return found->page->epoch == mEpoch ? found->page->states[location.slot] : mDefault;
        }
        std::size_t retainedBytes() const {
            std::size_t bytes{};
            for (auto const& entry : mPages)
                bytes += entry.page->byteSize();
            return bytes;
        }
    };

    void initialize(std::span<std::vector<std::size_t> const> sections, std::size_t blockCount) {
        assert(sections.size() <= std::numeric_limits<std::uint32_t>::max());
        auto layout = std::make_shared<Layout>(blockCount);
        mPages.clear();
        mPages.reserve(sections.size());
        for (std::size_t section = 0; section < sections.size(); ++section) {
            auto const& indices = sections[section];
            assert(indices.size() <= 4096);
            auto page = std::make_shared<Page>();
            page->states.assign(indices.size(), CorrectionState::Missing);
            page->visibleCount = indices.size();
            page->visible.assign((indices.size() + 63) / 64, ~std::uint64_t{});
            for (std::size_t slot = 0; slot < indices.size(); ++slot) {
                assert(indices[slot] < blockCount);
                (*layout)[indices[slot]] = {static_cast<std::uint32_t>(section), static_cast<std::uint32_t>(slot)};
            }
            mPages.push_back(std::move(page));
        }
        mLayout = std::move(layout);
        mCopiedBytes = 0;
        mEpoch = 0;
        mDefault = CorrectionState::Missing;
    }

    CorrectionState get(std::size_t index) const {
        auto const location = (*mLayout)[index];
        auto const& page = *mPages[location.section];
        return page.epoch == mEpoch ? page.states[location.slot] : mDefault;
    }
    bool visible(std::size_t index) const {
        auto p = (*mLayout)[index];
        return mPages[p.section]->isVisible(p.slot);
    }
    std::size_t section(std::size_t index) const { return (*mLayout)[index].section; }
    bool sectionVisible(std::size_t section) const { return mPages[section]->visibleCount != 0; }
    bool sectionHasResolvedCells(std::size_t section) const {
        if (section >= mPages.size())
            return false; // extra-only Sections have no state page
        auto const &page = *mPages[section];
        return page.epoch == mEpoch ? page.nonMissingCount != 0 : mDefault != CorrectionState::Missing;
    }

    bool set(std::size_t index, CorrectionState value) {
        auto const location = (*mLayout)[index];
        auto previous = get(index);
        if (previous == value)
            return false;
        auto &page = writablePage(location.section);
        if (previous != CorrectionState::Missing)
            --page.nonMissingCount;
        if (value != CorrectionState::Missing)
            ++page.nonMissingCount;
        page.states[location.slot] = value;
        return true;
    }
    void setVisible(std::size_t index, bool visible) {
        auto const location = (*mLayout)[index];
        if (mPages[location.section]->isVisible(location.slot) == visible)
            return;
        auto& page = writablePage(location.section);
        if (visible) ++page.visibleCount;
        else --page.visibleCount;
        auto& word = page.visible[location.slot / 64];
        auto const mask = std::uint64_t{1} << (location.slot % 64);
        if (visible)
            word |= mask;
        else
            word &= ~mask;
    }
    void fill(CorrectionState value) {
        // Placement resets are O(1); old worker pages retain their own epoch.
        ++mEpoch;
        mDefault = value;
    }
    Snapshot capture(std::span<std::size_t const> sections) const {
        Snapshot snapshot;
        snapshot.mLayout = mLayout;
        snapshot.mEpoch = mEpoch;
        snapshot.mDefault = mDefault;
        snapshot.mPages.reserve(sections.size());
        for (auto section : sections) {
            // Extra-only correction sections have no expected-block state page.
            if (section < mPages.size())
                snapshot.mPages.push_back({section, mPages[section]});
        }
        std::sort(snapshot.mPages.begin(), snapshot.mPages.end(),
                  [](auto const& lhs, auto const& rhs) { return lhs.section < rhs.section; });
        auto const uniqueEnd = std::unique(snapshot.mPages.begin(), snapshot.mPages.end(),
                                           [](auto const& lhs, auto const& rhs) { return lhs.section == rhs.section; });
        snapshot.mPages.erase(uniqueEnd, snapshot.mPages.end());
        return snapshot;
    }
    std::uint64_t copiedBytes() const { return mCopiedBytes; }

private:
    Page& writablePage(std::size_t section) {
        auto& page = mPages[section];
        if (page.use_count() > 1) {
            mCopiedBytes += page->byteSize();
            page = std::make_shared<Page>(*page);
        }
        if (page->epoch != mEpoch) {
            std::fill(page->states.begin(), page->states.end(), mDefault);
            page->epoch = mEpoch;
            page->nonMissingCount = mDefault == CorrectionState::Missing ? 0 : page->states.size();
        }
        return *page;
    }
    std::shared_ptr<Layout const> mLayout;
    std::vector<std::shared_ptr<Page>> mPages;
    std::uint64_t mCopiedBytes{};
    std::uint64_t mEpoch{};
    CorrectionState mDefault{CorrectionState::Missing};
};

} // namespace lholo::projection::detail
