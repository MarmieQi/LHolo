#include "ui/hud/HudContent.h"
#include "structure/StructureUiState.h"
#include "ui/MaterialFormat.h"
#include "i18n/Translator.h"
#include <algorithm>
#include <cstdio>
#include <utility>

namespace lholo::ui::hud {
namespace {
using i18n::TextKey;
template <class... Args>
std::string text(TextKey key, Args... args) {
    auto const* format = i18n::tr(key);
    auto const length = std::snprintf(nullptr, 0, format, args...);
    if (length <= 0) return {};
    std::string result(static_cast<std::size_t>(length) + 1, '\0');
    std::snprintf(result.data(), result.size(), format, args...);
    result.resize(static_cast<std::size_t>(length));
    return result;
}
}

HudPanel makeWriteHud(WriteHudData const& data, int position, std::uint64_t now) {
    HudPanel panel{position, {}};
    if (!data.operation || (!data.active && (!data.finishedAt || now - data.finishedAt >= 2500))) return panel;
    panel.lines.push_back({text(TextKey::HudWriteFile, data.fileName.c_str())});
    if (data.progressKnown) {
        auto const percent = data.total ? static_cast<unsigned>(data.done * 100 / data.total) : 100u;
        panel.lines.push_back({text(TextKey::HudWriteProgress, static_cast<unsigned long long>(data.done),
            static_cast<unsigned long long>(data.total), percent)});
    }
    if (data.phase != TextKey::Count) panel.lines.push_back({i18n::tr(data.phase)});
    return panel;
}

HudPanel makeProjectionHud(structure::detail::HudStateSnapshot const& s, ProjectionHudData const& d) {
    HudPanel panel{std::clamp(s.position, 0, 3), {}};
    if (!s.enabled || (!s.showFileName && !s.showLayer && !s.showOverallProgress && !s.showProgress
        && !s.showWrongState && !s.showWrongType && !s.showExtraBlocks && !s.showProjectedBlockName)) return panel;
    auto add = [&](std::string value, TextTone tone = TextTone::Normal) {
        if (!value.empty()) panel.lines.push_back({std::move(value), tone});
    };
    if (s.showFileName) add(text(TextKey::HudFileName, d.fileName.c_str()));
    auto const maxLayer = std::max(0, d.maxLayer);
    auto const layer = std::clamp(d.layer, 0, maxLayer);
    using structure::LayerAxis;
    using structure::LayerDisplayMode;
    if (s.showLayer && d.axis == LayerAxis::Material) {
        switch (d.mode) {
        case LayerDisplayMode::All: add(i18n::tr(TextKey::HudMaterialFilterAll)); break;
        case LayerDisplayMode::Single: add(text(TextKey::HudMaterialFilterSingle, layer + 1, maxLayer + 1)); break;
        case LayerDisplayMode::UpToCurrent: add(text(TextKey::HudMaterialFilterUpTo, layer + 1)); break;
        default: add(text(TextKey::HudMaterialFilterRange, layer + 1, maxLayer + 1)); break;
        }
    } else if (s.showLayer) {
        auto const* axis = d.axis == LayerAxis::X ? "X" : "Y";
        switch (d.mode) {
        case LayerDisplayMode::All: add(i18n::tr(TextKey::HudRangeAll)); break;
        case LayerDisplayMode::Single: add(text(TextKey::HudCurrentLayer, layer, maxLayer, axis)); break;
        case LayerDisplayMode::UpToCurrent: add(text(TextKey::HudRangeFromZero, layer, axis)); break;
        default: add(text(TextKey::HudRangeBetween, layer, maxLayer, axis)); break;
        }
    }
    auto const& p = d.progress;
    if (s.showOverallProgress) add(text(TextKey::HudOverallProgress,
        static_cast<unsigned long long>(p.placed), static_cast<unsigned long long>(p.total)));
    if (s.showProgress) add(text(TextKey::HudBuildProgress,
        static_cast<unsigned long long>(p.visiblePlaced), static_cast<unsigned long long>(p.visibleTotal)));
    if (s.showWrongState && p.wrongState) add(text(TextKey::HudWrongState, static_cast<unsigned long long>(p.wrongState)), TextTone::WrongState);
    if (s.showWrongType && p.wrongType) add(text(TextKey::HudWrongType, static_cast<unsigned long long>(p.wrongType)), TextTone::WrongType);
    if (s.showExtraBlocks && p.extra) add(text(TextKey::HudExtraBlocks, static_cast<unsigned long long>(p.extra)), TextTone::Extra);
    if (s.showProjectedBlockName && !d.aimedBlock.empty()) {
        HudLine line{text(TextKey::HudProjectedBlock, d.aimedBlock.c_str())};
        // Temporarily disabled pending layout review; keep the icon implementation for later.
        /*
        std::string const format = i18n::tr(TextKey::HudProjectedBlock);
        auto const marker = format.find("%s");
        if (!d.aimedBlockIconId.empty() && marker != std::string::npos) {
            // Keep the icon at the localized value placeholder, not at a
            // hard-coded colon/byte offset that only works in one language.
            line.iconPrefix = format.substr(0, marker);
            line.text = d.aimedBlock + format.substr(marker + 2);
            line.iconId = d.aimedBlockIconId;
            line.iconItem = d.aimedBlockIconItem;
        }
        */
        panel.lines.push_back(std::move(line));
    }
    if (!d.placementMode.empty()) add(text(TextKey::HudPlacementMode, d.placementMode.c_str()), TextTone::Placement);
    return panel;
}

HudPanel makeMaterialHud(structure::detail::MaterialHudSnapshot const& snapshot, int position, bool convertStacks, int maxItems, MaterialHudPaging* paging, int pageDelta) {
    MaterialHudPaging localPage;
    auto& page = paging ? *paging : localPage;
    HudPanel panel{std::clamp(position, 0, 3), {{i18n::tr(TextKey::MaterialHudTitle), TextTone::Normal, std::nullopt,
        text(TextKey::MaterialHudPage, std::size_t{1}, std::size_t{1})}}, true};
    if (!snapshot.ready) {
        page.update(0, 1);
        panel.lines.push_back({i18n::tr(TextKey::MaterialHudScanning), TextTone::Muted});
        return panel;
    }
    struct Row { std::size_t index; std::uint64_t missing; };
    std::vector<Row> rows;
    rows.reserve(snapshot.requirements.size());
    for (std::size_t index = 0; index < snapshot.requirements.size(); ++index) {
        auto const need = snapshot.requirements[index].count;
        auto const have = index < snapshot.available.size() ? std::max(0, snapshot.available[index]) : 0;
        if (need > static_cast<std::uint64_t>(have)) rows.push_back({index, need - static_cast<std::uint64_t>(have)});
    }
    auto const maxRows = static_cast<std::size_t>(std::clamp(maxItems, settings::kMinMaterialHudItems, settings::kMaxMaterialHudItems));
    page.update(rows.size(), maxRows, pageDelta);
    panel.lines.front().quantity = text(TextKey::MaterialHudPage, page.index + 1, page.count);
    auto const first = page.index * maxRows;
    auto const last = first + std::min(maxRows, rows.size() - first);
    auto const less = [](Row const& a, Row const& b) {
        return a.missing != b.missing ? a.missing > b.missing : a.index < b.index;
    };
    // Partition off earlier pages, then sort only this page. Equal deficits
    // keep the material tracker's original order across page boundaries.
    if (first != 0) std::nth_element(rows.begin(), rows.begin() + first, rows.end(), less);
    std::partial_sort(rows.begin() + first, rows.begin() + last, rows.end(), less);
    if (rows.empty()) panel.lines.push_back({i18n::tr(TextKey::MaterialHudComplete), TextTone::Success});
    for (std::size_t index = first; index < last; ++index) {
        auto const& row = rows[index];
        auto const& material = snapshot.requirements[row.index];
        panel.lines.push_back({std::string(materialDisplayName(material.displayName, material.nameKey)), TextTone::Normal,
            std::string(materialIconId(material.itemId, material.typeName)),
            formatMaterialHudCount(row.missing, material.stackSize, convertStacks), material.iconItem});
    }
    return panel;
}
}
