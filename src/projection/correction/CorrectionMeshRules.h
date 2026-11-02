#pragma once

#include "projection/correction/CorrectionState.h"
#include <cstdint>

namespace lholo::projection::detail {

inline bool correctionNeedsModelUpdate(CorrectionState previous, CorrectionState next) {
    if (previous == next)
        return false;
    return previous == CorrectionState::Missing || next == CorrectionState::Missing ||
           previous == CorrectionState::Unknown || next == CorrectionState::Unknown;
}
inline bool canBuildCorrectionOnly(bool modelDirty, std::uint64_t uploadedRevision) {
    return !modelDirty && uploadedRevision != 0;
}

} // namespace lholo::projection::detail
