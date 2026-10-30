#pragma once
#include "app/StructureWriteActions.h"
#include <optional>
#include <string_view>
namespace lholo::oreui {
std::optional<app::StructureWriteIntent> parseStructureWriteIntent(std::string_view);
}
