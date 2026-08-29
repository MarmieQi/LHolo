// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// UI-session ownership and synchronization. Callers receive snapshots or use
// concrete operations; atomics, mutexes and mutable containers never escape.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace lholo::structure::detail {

struct MaterialRequirement {
    std::string   displayName;
    std::string   typeName;
    std::uint64_t count{};
    // Max stack size of the item this block resolves to (64 normally, 16 for
    // signs etc., 1 for filled buckets). Used for the JE-style "N (a x S + b)"
    // count display. Computed on the tick thread; 64 when unknown.
    int           stackSize{64};
};

// Requirements plus the matching held-item counts, copied together under one
// lock so the HUD always sees the two vectors aligned (available[i] pairs with
// requirements[i]). `available` is empty until the tick thread has scanned the
// inventory at least once for the current structure.
struct MaterialSnapshot {
    std::vector<MaterialRequirement> requirements;
    std::vector<int>                 available;
};

struct HudStateSnapshot {
    bool  enabled{true};
    bool  showFileName{true};
    bool  showLayer{true};
    bool  showOverallProgress{};
    bool  showProgress{true};
    bool  showWrongState{true};
    bool  showWrongType{true};
    bool  showProjectedBlockName{true};
    int   position{1};
    float uiScale{2.0f};
};

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
    bool toggleManual{};
    bool toggleEasy{};
    bool loadProjection{};
    bool closeProjection{};
};

class StructureUiState {
public:
    // 0 gui, 1-6 moves, 7 layer+, 8 layer-, 9 toggle manual, 10 toggle easy,
    // 11 load projection, 12 close projection.
    static constexpr std::size_t kHotkeyCount = 13;
    static constexpr std::size_t kMoveHotkeyCount = 6;

    static StructureUiState& getInstance();

    StructureUiState(StructureUiState const&)            = delete;
    StructureUiState(StructureUiState&&)                 = delete;
    StructureUiState& operator=(StructureUiState const&) = delete;
    StructureUiState& operator=(StructureUiState&&)      = delete;

    [[nodiscard]] bool guiVisible() const;
    [[nodiscard]] bool toggleGuiVisible();
    void setGuiVisible(bool visible);
    [[nodiscard]] bool openingInputBlocked() const;
    void setOpeningInputBlockFrames(int frames);
    void consumeOpeningInputBlockFrame();
    [[nodiscard]] std::uint64_t blockGameInputUntil() const;
    void setBlockGameInputUntil(std::uint64_t deadline);

    [[nodiscard]] HudStateSnapshot hud() const;
    bool setUiScale(float scale);
    bool applyHud(HudStateSnapshot const& snapshot);

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
    void resetHotkeys();

    void setControlHeld(bool held);
    void setAltHeld(bool held);
    void setShiftHeld(bool held);
    [[nodiscard]] unsigned int currentHotkeyModifiers() const;
    [[nodiscard]] bool tryPressHotkey(std::size_t index);
    [[nodiscard]] bool releaseHotkeysForKey(unsigned int key, std::uint64_t now);
    void resetHotkeyState();
    [[nodiscard]] std::uint64_t ignoreHotkeyUntil() const;
    void setIgnoreHotkeyUntil(std::uint64_t deadline);

    void queueMove(std::size_t index);
    void queueLayerDelta(int delta);
    void queueToggleManual();
    void queueToggleEasy();
    void queueLoadProjection();
    void queueCloseProjection();
    void requestSettingsSave();
    [[nodiscard]] PendingHotkeyActions consumePendingHotkeyActions();

    void requestMaterialList();
    [[nodiscard]] bool consumeMaterialListRequest();
    void replaceMaterialRequirements(std::vector<MaterialRequirement> materials);
    [[nodiscard]] std::vector<MaterialRequirement> materialRequirements() const;
    // Held-item counts aligned to the current requirements, updated from the game
    // tick thread (safe to touch the inventory / item registry there). The HUD,
    // which runs on the render thread, reads the snapshot instead of scanning.
    void setMaterialAvailability(std::vector<int> counts);
    [[nodiscard]] MaterialSnapshot materialSnapshot() const;
    void clearMaterials();

private:
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
    std::atomic_bool  mHudShowProjectedBlockName{true};
    std::atomic_int   mHudPosition{1};
    std::atomic<float> mUiScale{2.0f};

    std::array<HotkeyStorage, kHotkeyCount> mHotkeys;
    std::atomic_bool mControlHeld{false};
    std::atomic_bool mAltHeld{false};
    std::atomic_bool mShiftHeld{false};
    std::array<std::atomic_uint64_t, 256> mConsumeKeyReleaseUntil{};

    std::atomic_int      mPendingOffsetX{0};
    std::atomic_int      mPendingOffsetY{0};
    std::atomic_int      mPendingOffsetZ{0};
    std::atomic_int      mPendingLayerDelta{0};
    std::atomic_bool     mPendingToggleManual{false};
    std::atomic_bool     mPendingToggleEasy{false};
    std::atomic_bool     mPendingLoadProjection{false};
    std::atomic_bool     mPendingCloseProjection{false};
    std::atomic_bool     mPendingSettingsSave{false};
    std::atomic_uint64_t mIgnoreHotkeyUntil{0};

    mutable std::mutex                mMaterialMutex;
    std::atomic_bool                  mMaterialListRequested{false};
    std::vector<MaterialRequirement>  mMaterialRequirements;
    std::vector<int>                  mMaterialAvailability;
};

} // namespace lholo::structure::detail
