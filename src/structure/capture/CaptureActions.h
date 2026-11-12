#pragma once

#include "structure/capture/StructureCapture.h"
#include <array>

namespace lholo::structure::capture {
enum class Action { Apply, FirstFromPlayer, SecondFromPlayer, Clear, Export, ExportEmpty };
struct Request {
    Action action{};
    Draft draft;
    std::uint64_t revision{};
    std::uint64_t id{};
    std::filesystem::path output;
    std::array<int, 3> size{};
};
// Producers only enqueue data; the client logic update drains the queue.
// No UI/protocol dependencies.
bool submitAction(Request request);
// Client logic thread only: actions may read the player's BlockSource.
void processActions();
void resetActions();
void resetWorldActions();
bool actionPending();
std::uint64_t completedAction();
bool emptyExportSucceeded();
}
