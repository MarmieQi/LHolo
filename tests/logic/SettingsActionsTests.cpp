#include "app/SettingsActions.h"
#include "oreui/SettingsProtocol.h"
#include "input/HotkeyCapture.h"
#include "structure/StructureSession.h"
#include "structure/StructureUiState.h"
#include "place/PlaceHelper.h"
#include "place/PlacementState.h"
#include "i18n/Translator.h"
#include "nlohmann/json.hpp"
#include <Windows.h>
#include <cstdio>

// Keep the real placement state but replace its game Hook facade for this target.
namespace lholo::place {
void setEnabled(bool v) { detail::PlacementState::getInstance().setEnabled(v); }
bool isEnabled() { return detail::PlacementState::getInstance().enabled(); }
void setRangeEnabled(bool v) { detail::PlacementState::getInstance().setRangeEnabled(v); }
bool isRangeEnabled() { return detail::PlacementState::getInstance().rangeEnabled(); }
void setManualMode(bool v) { if (!v) detail::PlacementState::getInstance().resetManualInput(); detail::PlacementState::getInstance().setManualMode(v); }
bool isManualMode() { return detail::PlacementState::getInstance().manualMode(); }
void setPlacementRadius(int v) { detail::PlacementState::getInstance().setRadius(v); }
int getPlacementRadius() { return detail::PlacementState::getInstance().radius(); }
void setAutoPlacementBreakCooldownSeconds(int v) { detail::PlacementState::getInstance().setAutoPlacementBreakCooldownSeconds(v); }
int getAutoPlacementBreakCooldownSeconds() { return detail::PlacementState::getInstance().autoPlacementBreakCooldownSeconds(); }
}
int runSettingsActionsTests() {
    using namespace lholo;
    using namespace app;
    using F = SettingsField;
    auto& ui = structure::detail::StructureUiState::getInstance();
    auto& session = structure::detail::StructureSession::getInstance();
    int failures{};
    auto check = [&](bool ok, char const* msg) { if (!ok) {++failures; std::fprintf(stderr, "SettingsActionsTests: %s\n", msg);} };
    resetSettingsActions(); ui.resetHotkeys(); ui.setExperimentalConsentGiven(false);
    place::setEnabled(false); place::setManualMode(false); place::setRangeEnabled(false);
    ui.setUiScale(3.7f);
    auto snapshot = getSettingsSnapshot();
    nlohmann::json j{{"id", 1}, {"context", snapshot.context}, {"action", "set"}, {"field", "hudEnabled"}, {"expected", ui.hud().enabled}, {"value", false}};
    check(oreui::parseSettingsIntent(j.dump()).has_value(), "valid HUD boolean");
    auto bad = j; bad["value"] = 1;
    check(!oreui::parseSettingsIntent(bad.dump()), "boolean cannot become integer");
    bad = j; bad["field"] = "uiScale"; check(!oreui::parseSettingsIntent(bad.dump()), "OreUI cannot write ImGui scale");
    bad = j; bad["field"] = "activeMode"; bad["expected"] = 0; bad["value"] = 4;
    check(!oreui::parseSettingsIntent(bad.dump()), "mode range");
    bad["field"] = "placementRadius"; bad["expected"] = 1; bad["value"] = 18446744073709551615ULL;
    check(!oreui::parseSettingsIntent(bad.dump()), "integer overflow rejected");
    bad["value"] = 1.5; check(!oreui::parseSettingsIntent(bad.dump()), "fraction rejected");
    bad = j; bad["id"] = 9007199254740992ULL; check(!oreui::parseSettingsIntent(bad.dump()), "unsafe id");
    check(!oreui::parseSettingsIntent("{") && !oreui::parseSettingsIntent(std::string(4097, ' ')), "malformed and oversized");
    SettingsIntent i; i.id = 1; i.context = snapshot.context;
    auto apply = [&](F field, SettingsValue value) {
        auto s = getSettingsSnapshot(); i = {}; i.id = s.ack + 1; i.context = s.context; i.field = field;
        i.expected = s.values[static_cast<std::size_t>(field)]; i.value = value;
        check(submitSettingsIntent(i), "queue setting"); return processSettingsActions();
    };
    check(apply(F::HudEnabled, !ui.hud().enabled), "HUD change needs persistence");
    check(ui.hud().uiScale == 3.7f, "HUD edits preserve old ImGui scale");
    check(apply(F::HudPosition, (ui.hud().position + 1) % 4), "HUD position persisted");
    check(apply(F::WriteHudEnabled, !ui.writeHudEnabled()), "independent write HUD toggle uses saved-settings path");
    check(apply(F::MaterialHudEnabled, !ui.materialHudEnabled()), "material HUD toggle");
    auto conversion = j; conversion["field"] = "materialHudStackConversion"; conversion["expected"] = false; conversion["value"] = true;
    check(oreui::parseSettingsIntent(conversion.dump()).has_value(), "conversion boolean protocol");
    conversion["value"] = 1;
    check(!oreui::parseSettingsIntent(conversion.dump()), "conversion rejects numeric input");
    check(apply(F::MaterialHudStackConversion, true) && ui.materialHudStackConversion(), "conversion persists independently");
    check(apply(F::MaterialHudStackConversion, false) && !ui.materialHudStackConversion(), "conversion can be disabled");
    check(apply(F::MaterialHudMaxItems, 6) && ui.materialHudMaxItems() == 6, "visible item count persists");
    conversion["field"] = "materialHudMaxItems"; conversion["expected"] = 6; conversion["value"] = 0;
    check(!oreui::parseSettingsIntent(conversion.dump()), "zero rows rejected before queueing");
    conversion["value"] = 51;
    check(!oreui::parseSettingsIntent(conversion.dump()), "item limit range enforced");
    conversion["value"] = true;
    check(!oreui::parseSettingsIntent(conversion.dump()), "item count rejects booleans");
    check(apply(F::MaterialHudMaxItems, 5), "restore item limit");
    check(!apply(F::Mode, 2) && !getSettingsSnapshot().accepted && !place::isEnabled(), "backend refuses mode before consent");
    i = {}; snapshot = getSettingsSnapshot(); i.id = snapshot.ack + 1; i.context = snapshot.context;
    i.action = SettingsAction::Consent; i.expected = 0; i.value = 1;
    submitSettingsIntent(i); check(processSettingsActions() && ui.experimentalConsentGiven() && place::isManualMode(), "consent persists and activates requested mode");
    check(!apply(F::Mode, 3) && getSettingsSnapshot().accepted && place::isRangeEnabled() && !place::isManualMode() && !place::isEnabled(), "live mode mutually exclusive and not persisted");
    check(!apply(F::Mode, 2) && place::isEnabled() && !place::isRangeEnabled(), "easy mode exclusive");
    check(!apply(F::Mode, 0) && !place::isEnabled(), "disable live modes without save");
    place::setPlacementRadius(4); check(apply(F::Radius, 2), "radius persisted");
    place::setAutoPlacementBreakCooldownSeconds(10); check(apply(F::Cooldown, 31), "cooldown persisted");
    auto originalLanguage = std::string{i18n::languageCode(i18n::language())};
    auto target = originalLanguage == "en_US" ? "zh_CN" : "en_US";
    check(apply(F::Language, std::string{target}) && i18n::languageCode(i18n::language()) == target, "stable locale code applied");
    check(!apply(F::Language, std::string{"not_a_locale"}) && !getSettingsSnapshot().accepted, "unknown language rejected");
    i18n::setLanguageByCode(originalLanguage);
    auto command = [&](SettingsAction action, std::size_t slot = 0) {
        auto s = getSettingsSnapshot(); i = {}; i.id = s.ack + 1; i.context = s.context; i.action = action;
        i.slot = slot; i.hotkeysRevision = s.hotkeysRevision; check(submitSettingsIntent(i), "queue hotkey action"); return processSettingsActions();
    };
    auto const previousPageSlot = input::hotkeyIndex(input::HotkeyId::MaterialHudPreviousPage);
    auto const nextPageSlot = input::hotkeyIndex(input::HotkeyId::MaterialHudNextPage);
    check(ui.hotkey(previousPageSlot).key == 0 && ui.hotkey(nextPageSlot).key == 0, "paging defaults are unbound");
    command(SettingsAction::BeginCapture, previousPageSlot); input::captureHotkeyKey('J', 2);
    check(ui.hotkey(previousPageSlot).key == 'J' && ui.hotkey(previousPageSlot).modifiers == 2, "previous page uses native capture");
    command(SettingsAction::BeginCapture, nextPageSlot); input::captureHotkeyKey('J', 2);
    check(ui.hotkey(nextPageSlot).key == 'J' && ui.hotkey(previousPageSlot).key == 0, "page bindings share duplicate handling");
    check(ui.tryPressHotkey(nextPageSlot) && !ui.tryPressHotkey(nextPageSlot), "held page binding triggers once");
    ui.resetHotkeyState();
    command(SettingsAction::ResetHotkey, nextPageSlot);
    check(ui.hotkey(nextPageSlot).key == 0 && ui.hotkey(nextPageSlot).modifiers == 0, "reset page binding restores unbound default");
    check(!command(SettingsAction::BeginCapture, 8) && input::hotkeyCaptureActive(), "capture is live session state");
    auto before = ui.hotkey(8);
    check(!input::captureHotkeyKey(VK_F11, 0) && input::hotkeyCaptureActive(), "F11 belongs to game");
    input::captureHotkeyKey(VK_CONTROL, 1);
    check(input::hotkeyCaptureActive(), "modifier alone does not bind");
    input::captureHotkeyKey(VK_ESCAPE, 0);
    check(!input::hotkeyCaptureActive() && ui.hotkey(8).key == before.key, "Escape cancels without editing");
    command(SettingsAction::BeginCapture, 8); input::captureHotkeyKey('M', 2);
    check(ui.hotkey(8).key == 'M' && ui.hotkey(0).key == 0, "captured duplicate clears existing owner");
    command(SettingsAction::BeginCapture, 8); input::captureHotkeyKey(VK_BACK, 0);
    check(ui.hotkey(8).key == 0 && !input::hotkeyCaptureActive(), "Backspace clears binding");
    command(SettingsAction::BeginCapture, 7); input::captureHotkeyKey(VK_XBUTTON1, 1);
    check(ui.hotkey(7).key == VK_XBUTTON1 && ui.hotkey(7).modifiers == 1, "mouse side button captured with modifier");
    ui.setAltWheelOffsetEnabled(false); command(SettingsAction::ResetHotkeys);
    check(ui.hotkey(0).key == 'M' && ui.hotkey(0).modifiers == 2 && ui.altWheelOffsetEnabled(), "native defaults restore bindings and Alt wheel");
    command(SettingsAction::BeginCapture, 1); requestHotkeyCaptureCancel(); processSettingsActions();
    check(!input::hotkeyCaptureActive(), "leaving hotkey page cancels capture");
    snapshot = getSettingsSnapshot(); i = {}; i.id = snapshot.ack + 1; i.context = snapshot.context;
    i.action = SettingsAction::ClearHotkey; i.hotkeysRevision = snapshot.hotkeysRevision;
    submitSettingsIntent(i); ui.setHotkey(0, 'Q', 1);
    check(!processSettingsActions() && !getSettingsSnapshot().accepted && ui.hotkey(0).key == 'Q', "stale hotkey revision cannot overwrite external edit");
    i = {}; snapshot = getSettingsSnapshot(); i.id = snapshot.ack + 1; i.context = snapshot.context;
    i.field = F::HudEnabled; i.expected = ui.hud().enabled; i.value = !ui.hud().enabled;
    submitSettingsIntent(i); (void)session.beginFileLoad({});
    check(!processSettingsActions() && !getSettingsSnapshot().accepted, "context change rejects stale settings");
    resetSettingsActions(); ui.resetHotkeys(); ui.resetHotkeyState();
    requestHudLayoutEdit();
    check(consumeHudLayoutEditRequest() && !consumeHudLayoutEditRequest(), "HUD layout command is consumed once by the application frame");
    requestHudLayoutEdit(); resetSettingsActions();
    check(!consumeHudLayoutEditRequest(), "closing settings clears unconsumed HUD layout command");
    std::printf("SettingsActionsTests: %d failures\n", failures); return failures;
}
