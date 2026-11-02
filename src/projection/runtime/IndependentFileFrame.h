#pragma once

#include <cstddef>

namespace lholo::projection::detail {
// Visit mesh candidates in rotating file order. Priority selection can inspect
// all files; the reserved fairness turn starts from a different file each time.
template <class Render>
void forEachIndependentProjectionFile(std::size_t count, std::size_t& cursor, Render&& render) {
    if (!count) { cursor = 0; return; }
    auto const first = cursor % count;
    cursor = (first + 1) % count;
    for (std::size_t offset = 0; offset < count; ++offset)
        render((first + offset) % count);
}
}
