#pragma once
#include "app/StructurePreviewActions.h"
#include <optional>
#include <string_view>
namespace lholo::oreui {
std::optional<app::StructurePreviewIntent> parsePreviewIntent(std::string_view);
}
