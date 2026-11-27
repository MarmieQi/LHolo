#include "oreui/SettingsBindings.h"
#include "oreui/SettingsProtocol.h"
#include "ui/HotkeyDefinitions.h"
#include "ui/HotkeyFormat.h"
#include "i18n/Translator.h"
#include "nlohmann/json.hpp"
#include <mutex>
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/ClientInstanceState.h"
namespace lholo::oreui {
namespace {
struct PublishedSettings {
    app::SettingsSnapshot snapshot;
    i18n::Language language{};
    bool operator==(PublishedSettings const&) const = default;
};
std::mutex mutex;
std::optional<PublishedSettings> published;
}
bool queueSettingsIntent(std::string_view payload) {
    auto i = parseSettingsIntent(payload);
    if (!i) return false;
    if ((i->action == app::SettingsAction::Consent
         || (i->action == app::SettingsAction::Set && i->field == app::SettingsField::Mode))
        && std::get<int>(i->value) != 0) {
        auto client = ll::service::getClientInstance();
        if (!client || client->$getClientInstanceState() != ClientInstanceState::Playing || !client->getLocalPlayer())
            return false;
    }
    return app::submitSettingsIntent(*i);
}
std::optional<std::string> takeSettingsUpdate(bool force) {
    std::lock_guard lock(mutex);
    auto s = app::getSettingsSnapshot();
    auto const lang = i18n::language();
    PublishedSettings current{s, lang};
    if (!force && published == current) return std::nullopt;
    published = std::move(current);
    nlohmann::json values = nlohmann::json::object(), hotkeys = nlohmann::json::array(), languages = nlohmann::json::array();
    for (std::size_t n = 0; n < s.values.size(); ++n)
        std::visit([&](auto const& v) { values[std::string{kSettingsNames[n]}] = v; }, s.values[n]);
    for (auto const& definition : ui::kHotkeyDefinitions) {
        auto slot = input::hotkeyIndex(definition.id);
        auto const& b = s.hotkeys[slot];
        hotkeys.push_back({{"slot", slot}, {"labelKey", i18n::kTextKeyIds[static_cast<std::size_t>(definition.label)]},
            {"groupKey", i18n::kTextKeyIds[static_cast<std::size_t>(definition.group)]},
            {"key", b.key}, {"modifiers", b.modifiers}, {"capturing", b.capturing},
            {"display", ui::hotkeyChordName(b.modifiers, b.key)}});
    }
    languages.push_back({{"code", i18n::kFollowGameLanguageCode}, {"name", i18n::tr(i18n::TextKey::LabelFollowGameLanguage)}});
    for (auto const& language : i18n::languages()) languages.push_back({{"code", language.code}, {"name", language.displayName}});
    return nlohmann::json{{"values", values}, {"hotkeys", hotkeys}, {"languages", languages},
        {"consent", s.consent}, {"localRangeAvailable", s.localRangeAvailable}, {"hotkeysRevision", s.hotkeysRevision}, {"context", s.context}, {"ack", s.ack}, {"accepted", s.accepted}}.dump();
}
void cancelSettingsHotkeyCapture() { app::requestHotkeyCaptureCancel(); }
void resetSettingsBindings() { std::lock_guard lock(mutex); published.reset(); app::resetSettingsActions(); }
}
