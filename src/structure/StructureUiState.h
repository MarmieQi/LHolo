// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// UI-session ownership and synchronization. Callers receive snapshots or use
// concrete operations; atomics, mutexes and mutable containers never escape.

#pragma once

#include "settings/MaterialHudSettings.h"

#include "i18n/Message.h"
#include "input/HotkeyTypes.h"
#include "settings/HudPlacement.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class ItemStack;

namespace lholo::structure::detail {

struct MaterialRequirement {
    std::string   displayName;
    // Set when the display name comes from the interface language table rather
    // than from the game (projected liquids have no item to take a name from).
    std::optional<i18n::TextKey> nameKey;
    std::string   typeName;
    // Resolved inventory item type. Empty for materials without a directly
    // countable inventory form (for example projected water/lava cells).
    std::string   itemId;
    std::uint64_t count{};
    // Max stack size of the item this block resolves to (64 normally, 16 for
    // signs etc., 1 for filled buckets). Used for the JE-style "N (a x S + b)"
    // count display. Computed on the tick thread; 64 when unknown.
    int           stackSize{64};
    std::shared_ptr<ItemStack const> iconItem; // Immutable native item, including banner patterns.
    // Design of an item variant (banner colour, ominous type, patterns);
    // inventory items count toward this material only when their design
    // matches. Empty for ordinary materials.
    std::string   variantKey;
};

// One coherent, versioned snapshot for material-list consumers.
struct MaterialListSnapshot {
    std::uint64_t revision{};
    bool ready{};
    std::vector<MaterialRequirement> requirements;
};

struct ActionHintSnapshot {
    std::string   text;
    std::uint64_t expiry{};
    bool          persistent{};
};

// Current-layer missing requirements plus matching inventory counts, copied
// together so the render thread never observes mismatched vectors.
struct MaterialHudSnapshot {
    std::vector<MaterialRequirement> requirements;
    std::vector<int>                 available;
    bool                             ready{};
};

struct HudStateSnapshot {
    bool  enabled{true};
    bool  showFileName{true};
    bool  showLayer{true};
    bool  showOverallProgress{};
    bool  showProgress{true};
    bool  showWrongState{true};
    bool  showWrongType{true};
    bool  showExtraBlocks{true};
    bool  showProjectedBlockName{true};
    int   position{1};
    float uiScale{2.0f};
};
enum class HudFlag { Enabled, FileName, Layer, OverallProgress, Progress, WrongState, WrongType, ExtraBlocks, ProjectedBlockName };

struct HotkeyBindingSnapshot {
    unsigned int key{};
    unsigned int modifiers{};
    bool         capturing{};
};

struct PendingHotkeyActions {
    int  offsetX{};
    int  offsetY{};
    int  offsetZ{};
    int  layerDelta{};
    bool settingsSave{};
};

class StructureUiState {
public:
    static StructureUiState& getInstance();

    StructureUiState(StructureUiState const&)            = delete;
    StructureUiState(StructureUiState&&)                 = delete;
    StructureUiState& operator=(StructureUiState const&) = delete;
    StructureUiState& operator=(StructureUiState&&)      = delete;

    [[nodiscard]] bool guiVisible() const;
    void setGuiVisible(bool visible);
    [[nodiscard]] bool openingInputBlocked() const;
    void setOpeningInputBlockFrames(int frames);
    void consumeOpeningInputBlockFrame();
    [[nodiscard]] std::uint64_t blockGameInputUntil() const;
    void setBlockGameInputUntil(std::uint64_t deadline);

    [[nodiscard]] HudStateSnapshot hud() const;
    [[nodiscard]] settings::HudPlacements hudPlacements() const;
    void setHudPlacements(settings::HudPlacements const& placements);
    [[nodiscard]] std::array<int, 2> hudScales() const;
    void setHudScales(std::array<int, 2> const& values);
    [[nodiscard]] std::array<int, 2> hudBackgroundOpacities() const;
    void setHudBackgroundOpacities(std::array<int, 2> const& values);
    bool setUiScale(float scale);
    bool applyHud(HudStateSnapshot const& snapshot);
    bool setHudFlag(HudFlag field, bool enabled);
    bool setHudPosition(int position);

    [[nodiscard]] HotkeyBindingSnapshot hotkey(std::size_t index) const;
    [[nodiscard]] HotkeyBindingSnapshot inputHotkey(std::size_t index) const;
    void setHotkey(std::size_t index, unsigned int key, unsigned int modifiers);
    [[nodiscard]] std::optional<std::size_t> capturingHotkey() const;
    void beginHotkeyCapture(std::size_t index);
    void stopHotkeyCapture();
    void clearHotkey(std::size_t index);
    void bindCapturedHotkey(
        std::size_t  index,
        unsigned int key,
        unsigned int modifiers
    );
    void resetHotkey(std::size_t index);
    void resetHotkeys();

    void setControlHeld(bool held);
    void setAltHeld(bool held);
    void setShiftHeld(bool held);
    [[nodiscard]] unsigned int currentHotkeyModifiers() const;
    // Raw modifier tracking (event-driven keydown/keyup), not tied to any
    // rebindable hotkey slot. Drives the fixed Alt+wheel projection offset.
    [[nodiscard]] bool altHeld() const;
    // Preference for that fixed gesture: the key handler, the wheel handler
    // and the hotbar lock all consult it, so turning it off releases all three
    // at once instead of leaving a half-claimed Alt or a stuck hotbar lock.
    [[nodiscard]] bool altWheelOffsetEnabled() const;
    bool setAltWheelOffsetEnabled(bool enabled);
    [[nodiscard]] bool tryPressHotkey(std::size_t index);
    [[nodiscard]] bool releaseHotkeysForKey(unsigned int key, std::uint64_t now);
    void resetHotkeyState();
    [[nodiscard]] std::uint64_t ignoreHotkeyUntil() const;
    void setIgnoreHotkeyUntil(std::uint64_t deadline);

    // Accumulates a world-space step. The direction of a move hotkey depends on
    // the player's facing, so it is resolved by the caller in input/ViewMoveBasis
    // and only the resulting delta reaches this state.
    void queueOffsetDelta(int deltaX, int deltaY, int deltaZ);
    void queueLayerDelta(int delta);
    void requestSettingsSave();
    [[nodiscard]] PendingHotkeyActions consumePendingHotkeyActions();

    [[nodiscard]] bool experimentalConsentGiven() const;
    void setExperimentalConsentGiven(bool given);
    [[nodiscard]] bool materialHudEnabled() const;
    void setMaterialHudEnabled(bool enabled);
    [[nodiscard]] bool materialHudStackConversion() const;
    void setMaterialHudStackConversion(bool enabled);
    [[nodiscard]] int materialHudMaxItems() const;
    void setMaterialHudMaxItems(int count);
    [[nodiscard]] int materialHudPosition() const;
    void setMaterialHudPosition(int position);
    // Hints are stored as key plus arguments so a pending hint follows a
    // language switch like every other interface string.
    void setActionHint(i18n::Message message, std::uint64_t expiry);
    [[nodiscard]] std::uint64_t actionHintExpiry() const;
    [[nodiscard]] ActionHintSnapshot actionHint(std::uint64_t now = 0) const;
    [[nodiscard]] bool actionHintActive(std::uint64_t now) const;
    // Loading shares the existing hint bar, but does not expire or overwrite
    // transient hints. Completion tokens cannot clear a later loading hint.
    [[nodiscard]] std::uint64_t beginLoadingHint(i18n::TextKey key = i18n::TextKey::StatusLoading);
    void finishLoadingHint(std::uint64_t token);
    void clearLoadingHint();

    void requestMaterialList();
    [[nodiscard]] bool consumeMaterialListRequest();
    [[nodiscard]] bool materialListReady() const;
    [[nodiscard]] std::optional<MaterialListSnapshot> materialListSnapshotIfChanged(
        std::optional<std::uint64_t> revision
    ) const;
    void replaceMaterialRequirements(std::vector<MaterialRequirement> materials);
    [[nodiscard]] std::vector<MaterialRequirement> materialRequirements() const;
    // The material-list popup and the current-layer HUD deliberately own
    // separate snapshots: the popup covers the whole structure, while the HUD
    // follows projection correction and layer visibility.
    void replaceMaterialHudSnapshot(
        std::vector<MaterialRequirement> materials,
        std::vector<int>                 available
    );
    void setMaterialHudAvailability(std::vector<int> counts);
    [[nodiscard]] MaterialHudSnapshot materialHudSnapshot() const;
    void clearMaterialHud();
    void clearMaterials();

    // Clears transient UI and queued actions owned by the current world. User
    // preferences (HUD layout, hotkey bindings, consent) remain unchanged.
    void resetWorldSession();

private:
    mutable std::mutex mHudLayoutMutex;
    settings::HudPlacements mHudPlacements{};
    std::array<int, 2> mHudBackgroundOpacities{60, 60};
    std::array<int, 2> mHudScales{100, 100};
    StructureUiState();

    struct HotkeyStorage {
        std::atomic_uint key{};
        std::atomic_uint modifiers{};
        std::atomic_bool capturing{};
        std::atomic_bool held{};
    };

    [[nodiscard]] HotkeyStorage* hotkeyStorage(std::size_t index);
    [[nodiscard]] HotkeyStorage const* hotkeyStorage(std::size_t index) const;

    std::atomic_bool     mGuiVisible{false};
    std::atomic_int      mOpeningInputBlockFrames{0};
    std::atomic_uint64_t mBlockGameInputUntil{};

    std::atomic_bool  mHudEnabled{true};
    std::atomic_bool  mHudShowFileName{true};
    std::atomic_bool  mHudShowLayer{true};
    std::atomic_bool  mHudShowOverallProgress{false};
    std::atomic_bool  mHudShowProgress{true};
    std::atomic_bool  mHudShowWrongState{true};
    std::atomic_bool  mHudShowWrongType{true};
    std::atomic_bool  mHudShowExtraBlocks{true};
    std::atomic_bool  mHudShowProjectedBlockName{true};
    std::atomic_int   mHudPosition{1};
    std::atomic<float> mUiScale{2.0f};

    std::array<HotkeyStorage, input::kHotkeyCount> mHotkeys;
    std::atomic_bool mControlHeld{false};
    std::atomic_bool mAltHeld{false};
    std::atomic_bool mShiftHeld{false};
    std::atomic_bool mAltWheelOffsetEnabled{true};
    std::array<std::atomic_uint64_t, 256> mConsumeKeyReleaseUntil{};

    std::atomic_int      mPendingOffsetX{0};
    std::atomic_int      mPendingOffsetY{0};
    std::atomic_int      mPendingOffsetZ{0};
    std::atomic_int      mPendingLayerDelta{0};
    std::atomic_bool     mPendingSettingsSave{false};
    std::atomic_uint64_t mIgnoreHotkeyUntil{0};

    std::atomic_bool mExperimentalConsent{false};
    std::atomic_bool mMaterialHudEnabled{false};
    std::atomic_bool mMaterialHudStackConversion{false};
    std::atomic_int mMaterialHudMaxItems{settings::kDefaultMaterialHudItems};
    std::atomic_int  mMaterialHudPosition{3};
    mutable std::mutex mActionHintMutex;
    i18n::Message      mActionHintMessage;
    std::atomic_uint64_t mActionHintExpiry{};
    std::uint64_t       mLoadingHintSequence{}; // protected by mActionHintMutex
    i18n::TextKey       mLoadingHintKey{i18n::TextKey::StatusLoading}; // protected by mActionHintMutex
    std::atomic_uint64_t mLoadingHintToken{};

    mutable std::mutex                mMaterialMutex;
    std::atomic_bool                  mMaterialListRequested{false};
    std::atomic_bool                  mMaterialListReady{false};
    std::uint64_t                     mMaterialListRevision{}; // protected by mMaterialMutex
    std::vector<MaterialRequirement>  mMaterialRequirements;
    std::vector<MaterialRequirement>  mMaterialHudRequirements;
    std::vector<int>                  mMaterialHudAvailability;
    bool                              mMaterialHudReady{};
};

} // namespace lholo::structure::detail
