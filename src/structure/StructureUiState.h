// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// UI-session ownership and synchronization. Callers receive snapshots or use
// concrete operations; atomics, mutexes and mutable containers never escape.

#pragma once

#include "input/HotkeyTypes.h"

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
  std::string displayName;
  std::string typeName;
  // Resolved inventory item type. Empty for materials without a directly
  // countable inventory form (for example projected water/lava cells).
  std::string itemId;
  std::uint64_t count{};
  // Max stack size of the item this block resolves to (64 normally, 16 for
  // signs etc., 1 for filled buckets). Used for the JE-style "N (a x S + b)"
  // count display. Computed on the tick thread; 64 when unknown.
  int stackSize{64};
};

struct ActionHintSnapshot {
  std::string text;
  std::uint64_t expiry{};
};

// Current-layer missing requirements plus matching inventory counts, copied
// together so the render thread never observes mismatched vectors.
struct MaterialHudSnapshot {
  std::vector<MaterialRequirement> requirements;
  std::vector<int> available;
  bool ready{};
};

struct HudStateSnapshot {
  bool enabled{true};
  bool showFileName{true};
  bool showLayer{true};
  bool showOverallProgress{};
  bool showProgress{true};
  bool showWrongState{true};
  bool showWrongType{true};
  bool showExtraBlocks{true};
  bool showProjectedBlockName{true};
  int position{1};
  float uiScale{2.0f};
};

struct HotkeyBindingSnapshot {
  unsigned int key{};
  unsigned int modifiers{};
  bool capturing{};
};

struct PendingHotkeyActions {
  int offsetX{};
  int offsetY{};
  int offsetZ{};
  int layerDelta{};
  bool settingsSave{};
  bool loadProjection{};
  bool closeProjection{};
};

class StructureUiState {
public:
  static StructureUiState &getInstance();

  StructureUiState(StructureUiState const &) = delete;
  StructureUiState(StructureUiState &&) = delete;
  StructureUiState &operator=(StructureUiState const &) = delete;
  StructureUiState &operator=(StructureUiState &&) = delete;

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
  bool applyHud(HudStateSnapshot const &snapshot);

  [[nodiscard]] HotkeyBindingSnapshot hotkey(std::size_t index) const;
  [[nodiscard]] HotkeyBindingSnapshot inputHotkey(std::size_t index) const;
  void setHotkey(std::size_t index, unsigned int key, unsigned int modifiers);
  [[nodiscard]] std::optional<std::size_t> capturingHotkey() const;
  void beginHotkeyCapture(std::size_t index);
  void stopHotkeyCapture();
  void clearHotkey(std::size_t index);
  void bindCapturedHotkey(std::size_t index, unsigned int key,
                          unsigned int modifiers);
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
  void queueOffsetDelta(int deltaX, int deltaY, int deltaZ);
  // Preview offset state: the user sees a temporary whole-structure outline
  // while adjusting, and the real transform changes only after the preview
  // expires with 1 second of inactivity.
  void addOffsetPreview(int deltaX, int deltaY, int deltaZ);
  [[nodiscard]] std::array<int, 3> offsetPreview() const;
  [[nodiscard]] bool offsetPreviewActive() const;
  [[nodiscard]] std::uint64_t offsetPreviewDeadline() const;
  void setOffsetPreviewDeadline(std::uint64_t deadline);
  [[nodiscard]] std::array<int, 3> consumeOffsetPreview();
  void clearOffsetPreview();
  void queueLayerDelta(int delta);
  void queueLoadProjection();
  void queueCloseProjection();
  void requestSettingsSave();
  [[nodiscard]] PendingHotkeyActions consumePendingHotkeyActions();

  [[nodiscard]] bool experimentalConsentGiven() const;
  void setExperimentalConsentGiven(bool given);
  [[nodiscard]] bool materialHudEnabled() const;
  void setMaterialHudEnabled(bool enabled);
  [[nodiscard]] int materialHudPosition() const;
  void setMaterialHudPosition(int position);
  void setActionHint(std::string text, std::uint64_t expiry);
  [[nodiscard]] std::uint64_t actionHintExpiry() const;
  [[nodiscard]] ActionHintSnapshot actionHint() const;

  void requestMaterialList();
  [[nodiscard]] bool consumeMaterialListRequest();
  [[nodiscard]] bool materialListReady() const;
  void replaceMaterialRequirements(std::vector<MaterialRequirement> materials);
  [[nodiscard]] std::vector<MaterialRequirement> materialRequirements() const;
  // The material-list popup and the current-layer HUD deliberately own
  // separate snapshots: the popup covers the whole structure, while the HUD
  // follows projection correction and layer visibility.
  void replaceMaterialHudSnapshot(std::vector<MaterialRequirement> materials,
                                  std::vector<int> available);
  void setMaterialHudAvailability(std::vector<int> counts);
  [[nodiscard]] MaterialHudSnapshot materialHudSnapshot() const;
  void clearMaterialHud();
  void clearMaterials();

  // Clears transient UI and queued actions owned by the current world. User
  // preferences (HUD layout, hotkey bindings, consent) remain unchanged.
  void resetWorldSession();

private:
  StructureUiState();

  struct HotkeyStorage {
    std::atomic_uint key{};
    std::atomic_uint modifiers{};
    std::atomic_bool capturing{};
    std::atomic_bool held{};
  };

  [[nodiscard]] HotkeyStorage *hotkeyStorage(std::size_t index);
  [[nodiscard]] HotkeyStorage const *hotkeyStorage(std::size_t index) const;

  std::atomic_bool mGuiVisible{false};
  std::atomic_int mOpeningInputBlockFrames{0};
  std::atomic_uint64_t mBlockGameInputUntil{};

  std::atomic_bool mHudEnabled{true};
  std::atomic_bool mHudShowFileName{true};
  std::atomic_bool mHudShowLayer{true};
  std::atomic_bool mHudShowOverallProgress{false};
  std::atomic_bool mHudShowProgress{true};
  std::atomic_bool mHudShowWrongState{true};
  std::atomic_bool mHudShowWrongType{true};
  std::atomic_bool mHudShowExtraBlocks{true};
  std::atomic_bool mHudShowProjectedBlockName{true};
  std::atomic_int mHudPosition{1};
  std::atomic<float> mUiScale{2.0f};

  std::array<HotkeyStorage, input::kHotkeyCount> mHotkeys;
  std::atomic_bool mControlHeld{false};
  std::atomic_bool mAltHeld{false};
  std::atomic_bool mShiftHeld{false};
  std::array<std::atomic_uint64_t, 256> mConsumeKeyReleaseUntil{};

  std::atomic_int mPendingOffsetX{0};
  std::atomic_int mPendingOffsetY{0};
  std::atomic_int mPendingOffsetZ{0};
  std::atomic_int mPreviewOffsetX{0};
  std::atomic_int mPreviewOffsetY{0};
  std::atomic_int mPreviewOffsetZ{0};
  std::atomic_uint64_t mPreviewApplyDeadline{0};
  std::atomic_int mPendingLayerDelta{0};
  std::atomic_bool mPendingLoadProjection{false};
  std::atomic_bool mPendingCloseProjection{false};
  std::atomic_bool mPendingSettingsSave{false};
  std::atomic_uint64_t mIgnoreHotkeyUntil{0};

  std::atomic_bool mExperimentalConsent{false};
  std::atomic_bool mMaterialHudEnabled{false};
  std::atomic_int mMaterialHudPosition{3};
  mutable std::mutex mActionHintMutex;
  std::string mActionHintText;
  std::atomic_uint64_t mActionHintExpiry{};

  mutable std::mutex mMaterialMutex;
  std::atomic_bool mMaterialListRequested{false};
  std::atomic_bool mMaterialListReady{false};
  std::vector<MaterialRequirement> mMaterialRequirements;
  std::vector<MaterialRequirement> mMaterialHudRequirements;
  std::vector<int> mMaterialHudAvailability;
  bool mMaterialHudReady{};
};

} // namespace lholo::structure::detail
