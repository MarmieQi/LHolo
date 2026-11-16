#pragma once

#include "place/local/PlacementRestock.h"

class ItemStackBase;
class Player;

namespace lholo::place::local {

std::optional<PlacementRestock> prepareShulkerRestock(Player& player, ItemStackBase const& material, int destination);

} // namespace lholo::place::local
