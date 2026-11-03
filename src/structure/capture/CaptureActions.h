#pragma once

#include "structure/capture/StructureCapture.h"

namespace lholo::structure::capture {
enum class Action { Apply, FirstFromPlayer, SecondFromPlayer, Clear, Export };
struct Request {
    Action action{};
    Draft draft;
    std::uint64_t revision{};
    std::uint64_t id{};
    std::filesystem::path output;
};
// Producers only enqueue data; the client logic update drains the queue.
// No UI/protocol dependencies.
bool submitAction(Request request);
// Client logic thread only: actions may read the player's BlockSource.
void processActions();
void resetActions();
bool actionPending();
std::uint64_t completedAction();
}
