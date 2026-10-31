#include "app/SettingsActions.h"
#include "structure/StructureUiState.h"
#include "structure/StructureSession.h"
#include "place/PlaceHelper.h"
#include "i18n/Translator.h"
#include <mutex>
#include <optional>
#include <utility>

namespace lholo::app {
namespace {
std::mutex mutex;
std::optional<SettingsIntent> pending;
std::uint64_t ack{}, hotkeysRevision{};
std::array<SettingsHotkey, input::kHotkeyCount> observedHotkeys{};
bool accepted{}, cancelCapture{}, hudLayoutEditRequested{};
auto& ui() { return structure::detail::StructureUiState::getInstance(); }
int activeMode() { return place::isManualMode() ? 1 : place::isRangeEnabled() ? 3 : place::isEnabled() ? 2 : 0; }
void setMode(int mode) {
    // These are live session controls, deliberately excluded from config persistence.
    place::setManualMode(false); place::setEnabled(false); place::setRangeEnabled(false);
    if (mode == 1) place::setManualMode(true);
    if (mode == 2) place::setEnabled(true);
    if (mode == 3) place::setRangeEnabled(true);
}
SettingsSnapshot snapshot() {
    auto h = ui().hud();
    SettingsSnapshot s{{h.enabled, h.position, h.showFileName, h.showLayer, h.showOverallProgress,
        h.showProgress, h.showProjectedBlockName, h.showWrongState, h.showWrongType, h.showExtraBlocks,
        ui().materialHudEnabled(), ui().materialHudStackConversion(), ui().materialHudMaxItems(), ui().materialHudPosition(), ui().altWheelOffsetEnabled(),
        place::getPlacementRadius(), place::getAutoPlacementBreakCooldownSeconds(), activeMode(),
        std::string{i18n::languageCode(i18n::language())}, ui().writeHudEnabled()}, {}, 0,
        structure::detail::StructureSession::getInstance().transformEditorState().context, ack,
        ui().experimentalConsentGiven(), accepted};
    for (std::size_t n = 0; n < s.hotkeys.size(); ++n) {
        auto binding = ui().hotkey(n);
        s.hotkeys[n] = {binding.key, binding.modifiers, binding.capturing};
    }
    if (s.hotkeys != observedHotkeys) { observedHotkeys = s.hotkeys; ++hotkeysRevision; }
    s.hotkeysRevision = hotkeysRevision;
    return s;
}
bool applyField(SettingsField field, SettingsValue const& value) {
    using F = SettingsField;
    using H = structure::detail::HudFlag;
    switch (field) {
    case F::WriteHudEnabled: ui().setWriteHudEnabled(std::get<bool>(value)); return true;
    case F::HudEnabled: return ui().setHudFlag(H::Enabled, std::get<bool>(value));
    case F::HudPosition: return ui().setHudPosition(std::get<int>(value));
    case F::HudFileName: return ui().setHudFlag(H::FileName, std::get<bool>(value));
    case F::HudLayer: return ui().setHudFlag(H::Layer, std::get<bool>(value));
    case F::HudOverallProgress: return ui().setHudFlag(H::OverallProgress, std::get<bool>(value));
    case F::HudProgress: return ui().setHudFlag(H::Progress, std::get<bool>(value));
    case F::HudProjectedBlockName: return ui().setHudFlag(H::ProjectedBlockName, std::get<bool>(value));
    case F::HudWrongState: return ui().setHudFlag(H::WrongState, std::get<bool>(value));
    case F::HudWrongType: return ui().setHudFlag(H::WrongType, std::get<bool>(value));
    case F::HudExtraBlocks: return ui().setHudFlag(H::ExtraBlocks, std::get<bool>(value));
    case F::MaterialHudEnabled: ui().setMaterialHudEnabled(std::get<bool>(value)); return true;
    case F::MaterialHudStackConversion: ui().setMaterialHudStackConversion(std::get<bool>(value)); return true;
    case F::MaterialHudMaxItems: ui().setMaterialHudMaxItems(std::get<int>(value)); return true;
    case F::MaterialHudPosition: ui().setMaterialHudPosition(std::get<int>(value)); return true;
    case F::AltWheel: return ui().setAltWheelOffsetEnabled(std::get<bool>(value));
    case F::Radius: place::setPlacementRadius(std::get<int>(value)); return true;
    case F::Cooldown: place::setAutoPlacementBreakCooldownSeconds(std::get<int>(value)); return true;
    case F::Mode: setMode(std::get<int>(value)); return false;
    case F::Language: return i18n::setLanguageByCode(std::get<std::string>(value));
    default: return false;
    }
}
}
bool validSettingsIntent(SettingsIntent const& i) {
    if (!i.id) return false;
    if (i.action == SettingsAction::Consent)
        return std::holds_alternative<int>(i.expected) && std::get<int>(i.expected) >= 0 && std::get<int>(i.expected) <= 3
            && std::holds_alternative<int>(i.value) && std::get<int>(i.value) >= 0 && std::get<int>(i.value) <= 3;
    if (i.action == SettingsAction::ResetHotkeys) return true;
    if (i.action == SettingsAction::BeginCapture || i.action == SettingsAction::ResetHotkey || i.action == SettingsAction::ClearHotkey)
        return i.slot < input::kHotkeyCount;
    if (i.action != SettingsAction::Set || i.field < SettingsField::HudEnabled || i.field >= SettingsField::Count) return false;
    auto validValue = [&](SettingsValue const& v) {
        using F = SettingsField;
        auto integer = [&](int low, int high) { auto n = std::get_if<int>(&v); return n && *n >= low && *n <= high; };
        switch (i.field) {
        case F::HudPosition: case F::MaterialHudPosition: case F::Mode: return integer(0, 3);
        case F::MaterialHudMaxItems: return integer(settings::kMinMaterialHudItems, settings::kMaxMaterialHudItems);
        case F::Radius: return integer(1, 4);
        case F::Cooldown: return integer(0, 60);
        case F::Language: { auto s = std::get_if<std::string>(&v); return s && !s->empty() && s->size() <= 32; }
        default: return std::holds_alternative<bool>(v);
        }
    };
    return validValue(i.expected) && validValue(i.value);
}
bool submitSettingsIntent(SettingsIntent const& intent) {
    std::lock_guard lock(mutex);
    if (!validSettingsIntent(intent) || intent.id <= ack
        || (pending && (intent.id <= pending->id || intent.context <= pending->context))) return false;
    pending = intent; return true;
}
SettingsSnapshot getSettingsSnapshot() { std::lock_guard lock(mutex); return snapshot(); }
bool processSettingsActions() {
    std::lock_guard lock(mutex);
    bool cancelled = std::exchange(cancelCapture, false);
    if (cancelled) ui().stopHotkeyCapture();
    if (!pending) return false;
    auto i = *std::exchange(pending, std::nullopt);
    ack = i.id; accepted = false;
    auto s = snapshot();
    if (i.context != s.context) return false;
    if (i.action == SettingsAction::Set) {
        if (s.values[static_cast<std::size_t>(i.field)] != i.expected) return false;
        if (i.field == SettingsField::Language && !i18n::isValidLanguage(i18n::languageFromCode(std::get<std::string>(i.value)))) return false;
        if (i.field == SettingsField::Mode && std::get<int>(i.value) != 0 && !s.consent) return false;
        accepted = true;
        return i.value != i.expected && applyField(i.field, i.value);
    }
    if (i.action == SettingsAction::Consent) {
        if (activeMode() != std::get<int>(i.expected)) return false;
        ui().setExperimentalConsentGiven(true); setMode(std::get<int>(i.value)); accepted = true;
        return !s.consent;
    }
    if (i.hotkeysRevision != s.hotkeysRevision || (cancelled && i.action == SettingsAction::BeginCapture)) return false;
    accepted = true;
    switch (i.action) {
    case SettingsAction::BeginCapture: ui().beginHotkeyCapture(i.slot); return false;
    case SettingsAction::ClearHotkey: ui().clearHotkey(i.slot); return true;
    case SettingsAction::ResetHotkey: ui().resetHotkey(i.slot); return true;
    case SettingsAction::ResetHotkeys: ui().resetHotkeys(); ui().resetHotkeyState(); return true;
    default: return false;
    }
}
void requestHotkeyCaptureCancel() { std::lock_guard lock(mutex); cancelCapture = true; ui().stopHotkeyCapture(); }
void requestHudLayoutEdit() { std::lock_guard lock(mutex); hudLayoutEditRequested = true; }
bool consumeHudLayoutEditRequest() { std::lock_guard lock(mutex); return std::exchange(hudLayoutEditRequested, false); }
void resetSettingsActions() {
    std::lock_guard lock(mutex);
    if (pending) ack = pending->id;
    pending.reset(); accepted = cancelCapture = false;
    hudLayoutEditRequested = false;
    ui().stopHotkeyCapture();
}
}
