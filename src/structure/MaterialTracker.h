// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Material requirement aggregation and game-thread inventory snapshots.

#pragma once

class LocalPlayer;

namespace lholo::structure::detail {

void requestMaterialListRefresh(bool renderRange = false);
void invalidateMaterialList();
void tickMaterialTracker(LocalPlayer& player);
void shutdownMaterialTracker();

} // namespace lholo::structure::detail
