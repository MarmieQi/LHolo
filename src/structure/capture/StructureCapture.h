#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace lholo::structure::capture {

enum class CaptureMode : std::uint8_t { Client, Singleplayer };
enum class PointSlot : std::uint8_t { First, Second };

struct Point {
    int x{};
    int y{};
    int z{};

    bool operator==(Point const&) const = default;
};

struct Draft {
    CaptureMode         mode{CaptureMode::Client};
    std::optional<Point> first;
    std::optional<Point> second;
    bool                includeEntities{};

    bool operator==(Draft const&) const = default;
};

struct Snapshot {
    Draft       draft;
    bool        worldAvailable{};
    std::string status;
    std::uint64_t revision{};

    bool operator==(Snapshot const&) const = default;
};

struct Bounds {
    Point min;
    Point max;
};

Snapshot              getSnapshot();
std::optional<Bounds> getBounds();
void                  updateDraft(Draft const& draft);
void                  setPointFromPlayer(PointSlot slot);
void                  exportStructure(Draft const& draft, std::filesystem::path const& output);
void                  clear();

} // namespace lholo::structure::capture
