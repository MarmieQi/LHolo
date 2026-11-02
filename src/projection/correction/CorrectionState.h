#pragma once

#include <cstdint>

namespace lholo::projection::detail {

enum class CorrectionState : std::uint8_t { Unknown, Missing, Correct, WrongType, WrongState };

} // namespace lholo::projection::detail
