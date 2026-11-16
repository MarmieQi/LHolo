#pragma once

class Block;
class BlockPos;
class BlockSource;
class Player;

namespace lholo::place::local {

struct PlacementRestock;

bool installPlacementOverride();
void uninstallPlacementOverride();
enum class PlacementUseResult { NoFace, Rejected, FloatingRejected, UnmatchedWrite, Placed };
PlacementUseResult usePlacementItem(Player& player, BlockSource& region, BlockPos const& cell,
    Block const& target, Block const* upper, int slot, bool mergeSlab, PlacementRestock* restock);

} // namespace lholo::place::local
