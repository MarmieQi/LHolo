// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Counts available material items in the player's inventory, including items
// stored inside shulker boxes and bundles.

#pragma once

#include "structure/StructureUiState.h"

#include <vector>

class LocalPlayer;

namespace lholo::structure::detail {

[[nodiscard]] std::vector<int> collectInventoryAvailability(
    LocalPlayer&                            player,
    std::vector<MaterialRequirement> const& requirements
);

} // namespace lholo::structure::detail
