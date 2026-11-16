#pragma once
#include "structure/capture/CaptureActions.h"
#include <optional>
#include <string_view>

namespace lholo::oreui {
inline constexpr std::size_t kCaptureIntentMaxBytes = 256 * 1024;
std::optional<structure::capture::Request> parseCaptureIntent(std::string_view payload);
}
