#pragma once

#include "mc/client/renderer/BaseActorRenderContext.h"

namespace lholo::render {

inline Vec3 cameraPosition(BaseActorRenderContext const& context) {
    auto const* impl = context.mImpl.get();
    // The previous world-render origin was the camera target at byte 40.
    // 26.60 changes the preceding capture storage; follow the SDK member.
    return impl ? impl->mCameraTargetPosition.get() : Vec3{};
}

} // namespace lholo::render
