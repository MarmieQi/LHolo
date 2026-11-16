// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#pragma once

#include <cstdint>

namespace lholo::input {

bool installProjectionInputGuard();
void uninstallProjectionInputGuard();
std::uint64_t takeGameplayRightPress();

} // namespace lholo::input
