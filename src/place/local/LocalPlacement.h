#pragma once

#include "place/LocalPlacementMode.h"

class LocalPlayer;

namespace lholo::place::local {

void install();
void uninstall();
bool available();
LocalPlacementMode mode();
void setMode(LocalPlacementMode value);
void cancelPending();
void submitCandidates(LocalPlayer& player);

} // namespace lholo::place::local
