#pragma once

#include "settings/MaterialHudSettings.h"
#include "ui/hud/MaterialHudPaging.h"

#include "projection/ProjectionTypes.h"
#include "i18n/TextKeys.h"
#include <string>
#include <optional>
#include <memory>
#include <vector>

class ItemStack;

namespace lholo::structure::detail {
struct HudStateSnapshot;
struct MaterialHudSnapshot;
}

namespace lholo::ui::hud {
enum class TextTone { Normal, Muted, Success, WrongState, WrongType, Extra, Placement };
struct HudLine {
    std::string text;
    TextTone tone{TextTone::Normal};
    std::optional<std::string> iconId;
    std::string quantity; // Right-aligned value: material count or header page indicator.
    std::shared_ptr<ItemStack const> iconItem;
    std::string iconPrefix; // Optional localized text drawn before the icon.
};
struct HudPanel {
    int position{}; // 0 TL, 1 BL, 2 TR, 3 BR, 4 top center; matches persisted settings.
    std::vector<HudLine> lines;
    bool compactRows{};
};
struct HudContent {
    std::uint64_t context{}; // Loaded-structure generation for icon cache invalidation.
    HudPanel projection;
    HudPanel materials;
    HudPanel write;
};
struct ProjectionHudData {
    std::string fileName;
    structure::LayerAxis axis{structure::LayerAxis::Y};
    structure::LayerDisplayMode mode{structure::LayerDisplayMode::All};
    int layer{}, maxLayer{};
    projection::BuildProgress progress;
    std::string aimedBlock;
    std::string aimedBlockIconId;
    std::shared_ptr<ItemStack const> aimedBlockIconItem;
    std::string placementMode;
};
struct WriteHudData {
    std::uint64_t operation{}, finishedAt{}, done{}, total{};
    bool active{}, progressKnown{};
    std::string fileName;
    i18n::TextKey phase{i18n::TextKey::Count};
};
HudPanel makeWriteHud(WriteHudData const& data, int position, std::uint64_t now);

// Pure presentation rules, independent of native font/context and game objects.
HudPanel makeProjectionHud(structure::detail::HudStateSnapshot const& settings, ProjectionHudData const& data);
HudPanel makeMaterialHud(structure::detail::MaterialHudSnapshot const& snapshot, int position, bool convertStacks = false,
    int maxItems = settings::kDefaultMaterialHudItems, MaterialHudPaging* paging = nullptr, int pageDelta = 0);

// Read existing business snapshots only; no world/inventory scans or settings writes.
HudContent readHudContent();
}
