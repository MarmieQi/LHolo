// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace lholo::structure {

inline std::string formatShulkerBoxCount(
    std::uint64_t count,
    int stackSize,
    std::string_view boxUnit,
    std::string_view stackUnit
) {
    auto const stack = static_cast<std::uint64_t>(stackSize > 0 ? stackSize : 64);
    auto const boxSize = stack * 27;
    auto const boxes = count / boxSize;
    auto const remainder = count % boxSize;
    auto const stacks = remainder / stack;
    auto const items = remainder % stack;

    std::string result;
    auto append = [&result](std::uint64_t amount, std::string_view unit) {
        if (!result.empty()) result += " + ";
        result += std::to_string(amount);
        result += unit;
    };
    if (boxes > 0) append(boxes, boxUnit);
    if (stacks > 0) append(stacks, stackUnit);
    if (items > 0 || result.empty()) append(items, std::string_view{});
    return result;
}

} // namespace lholo::structure
