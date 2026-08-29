// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "structure/StructureUiState.h"

#include "ui/HotkeyFormat.h"

#include <Windows.h>

#include <utility>

namespace lholo::structure::detail {
namespace {

template <class T>
bool updateRelaxed(std::atomic<T>& target, T value) {
    if (target.load(std::memory_order_relaxed) == value) return false;
    target.store(value, std::memory_order_relaxed);
    return true;
}

} // namespace

StructureUiState::StructureUiState() {
    static constexpr unsigned int keys[kHotkeyCount]{
        'M', VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_UP, VK_DOWN, VK_UP, VK_DOWN, 'R', 'F', 0, 0
    };
    static constexpr unsigned int modifiers[kHotkeyCount]{
        lholo::ui::kHotkeyModifierAlt,
        lholo::ui::kHotkeyModifierControl,
        lholo::ui::kHotkeyModifierControl,
        lholo::ui::kHotkeyModifierControl,
        lholo::ui::kHotkeyModifierControl,
        lholo::ui::kHotkeyModifierShift,
        lholo::ui::kHotkeyModifierShift,
        lholo::ui::kHotkeyModifierAlt,
        lholo::ui::kHotkeyModifierAlt,
        0,
        0,
        0,
        0
    };
    for (std::size_t index = 0; index < kHotkeyCount; ++index) {
        mHotkeys[index].key.store(keys[index], std::memory_order_relaxed);
        mHotkeys[index].modifiers.store(modifiers[index], std::memory_order_relaxed);
    }
}

StructureUiState& StructureUiState::getInstance() {
    static StructureUiState instance;
    return instance;
}

StructureUiState::HotkeyStorage* StructureUiState::hotkeyStorage(std::size_t index) {
    return index < mHotkeys.size() ? &mHotkeys[index] : nullptr;
}

StructureUiState::HotkeyStorage const* StructureUiState::hotkeyStorage(std::size_t index) const {
    return index < mHotkeys.size() ? &mHotkeys[index] : nullptr;
}

bool StructureUiState::guiVisible() const {
    return mGuiVisible.load(std::memory_order_acquire);
}

bool StructureUiState::toggleGuiVisible() {
    auto const opening = !mGuiVisible.load(std::memory_order_acquire);
    mGuiVisible.store(opening, std::memory_order_release);
    return opening;
}

void StructureUiState::setGuiVisible(bool visible) {
    mGuiVisible.store(visible, std::memory_order_release);
}

bool StructureUiState::openingInputBlocked() const {
    return mOpeningInputBlockFrames.load(std::memory_order_acquire) > 0;
}

void StructureUiState::setOpeningInputBlockFrames(int frames) {
    mOpeningInputBlockFrames.store(frames, std::memory_order_release);
}

void StructureUiState::consumeOpeningInputBlockFrame() {
    if (mOpeningInputBlockFrames.load(std::memory_order_acquire) > 0) {
        mOpeningInputBlockFrames.fetch_sub(1, std::memory_order_acq_rel);
    }
}

std::uint64_t StructureUiState::blockGameInputUntil() const {
    return mBlockGameInputUntil.load(std::memory_order_acquire);
}

void StructureUiState::setBlockGameInputUntil(std::uint64_t deadline) {
    mBlockGameInputUntil.store(deadline, std::memory_order_release);
}

HudStateSnapshot StructureUiState::hud() const {
    return {
        mHudEnabled.load(std::memory_order_relaxed),
        mHudShowFileName.load(std::memory_order_relaxed),
        mHudShowLayer.load(std::memory_order_relaxed),
        mHudShowOverallProgress.load(std::memory_order_relaxed),
        mHudShowProgress.load(std::memory_order_relaxed),
        mHudShowWrongState.load(std::memory_order_relaxed),
        mHudShowWrongType.load(std::memory_order_relaxed),
        mHudShowProjectedBlockName.load(std::memory_order_relaxed),
        mHudPosition.load(std::memory_order_relaxed),
        mUiScale.load(std::memory_order_relaxed)
    };
}

bool StructureUiState::setUiScale(float scale) {
    return updateRelaxed(mUiScale, scale);
}

bool StructureUiState::applyHud(HudStateSnapshot const& snapshot) {
    bool changed = false;
    changed = updateRelaxed(mHudEnabled, snapshot.enabled) || changed;
    changed = updateRelaxed(mHudShowFileName, snapshot.showFileName) || changed;
    changed = updateRelaxed(mHudShowLayer, snapshot.showLayer) || changed;
    changed = updateRelaxed(mHudShowOverallProgress, snapshot.showOverallProgress) || changed;
    changed = updateRelaxed(mHudShowProgress, snapshot.showProgress) || changed;
    changed = updateRelaxed(mHudShowWrongState, snapshot.showWrongState) || changed;
    changed = updateRelaxed(mHudShowWrongType, snapshot.showWrongType) || changed;
    changed = updateRelaxed(
        mHudShowProjectedBlockName, snapshot.showProjectedBlockName
    ) || changed;
    changed = updateRelaxed(mHudPosition, snapshot.position) || changed;
    changed = updateRelaxed(mUiScale, snapshot.uiScale) || changed;
    return changed;
}

HotkeyBindingSnapshot StructureUiState::hotkey(std::size_t index) const {
    auto const* storage = hotkeyStorage(index);
    if (!storage) return {};
    return {
        storage->key.load(std::memory_order_relaxed),
        storage->modifiers.load(std::memory_order_relaxed),
        storage->capturing.load(std::memory_order_acquire)
    };
}

HotkeyBindingSnapshot StructureUiState::inputHotkey(std::size_t index) const {
    auto const* storage = hotkeyStorage(index);
    if (!storage) return {};
    return {
        storage->key.load(std::memory_order_acquire),
        storage->modifiers.load(std::memory_order_acquire),
        storage->capturing.load(std::memory_order_acquire)
    };
}

void StructureUiState::setHotkey(
    std::size_t  index,
    unsigned int key,
    unsigned int modifiers
) {
    if (auto* storage = hotkeyStorage(index)) {
        storage->key.store(key, std::memory_order_relaxed);
        storage->modifiers.store(modifiers, std::memory_order_relaxed);
    }
}

std::optional<std::size_t> StructureUiState::capturingHotkey() const {
    // Only one hotkey can be capturing at a time (beginHotkeyCapture clears the
    // rest), so a plain scan over every slot is sufficient and count-agnostic.
    for (std::size_t index = 0; index < mHotkeys.size(); ++index) {
        if (mHotkeys[index].capturing.load(std::memory_order_acquire)) return index;
    }
    return std::nullopt;
}

void StructureUiState::beginHotkeyCapture(std::size_t index) {
    stopHotkeyCapture();
    if (auto* storage = hotkeyStorage(index)) {
        storage->capturing.store(true, std::memory_order_release);
    }
}

void StructureUiState::stopHotkeyCapture() {
    for (auto& hotkey : mHotkeys) hotkey.capturing.store(false, std::memory_order_release);
}

void StructureUiState::clearHotkey(std::size_t index) {
    if (auto* storage = hotkeyStorage(index)) {
        storage->key.store(0, std::memory_order_release);
        storage->modifiers.store(0, std::memory_order_release);
        storage->capturing.store(false, std::memory_order_release);
    }
}

void StructureUiState::bindCapturedHotkey(
    std::size_t  index,
    unsigned int key,
    unsigned int modifiers
) {
    auto* target = hotkeyStorage(index);
    if (!target) return;
    for (std::size_t current = 0; current < mHotkeys.size(); ++current) {
        if (current == index) continue;
        auto& candidate = mHotkeys[current];
        if (candidate.key.load(std::memory_order_relaxed) == key
            && candidate.modifiers.load(std::memory_order_relaxed) == modifiers) {
            candidate.key.store(0, std::memory_order_relaxed);
            candidate.modifiers.store(0, std::memory_order_relaxed);
        }
    }
    target->key.store(key, std::memory_order_release);
    target->modifiers.store(modifiers, std::memory_order_release);
    stopHotkeyCapture();
}

void StructureUiState::resetHotkeys() {
    static constexpr unsigned int keys[kHotkeyCount]{
        'M', VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_UP, VK_DOWN, VK_UP, VK_DOWN, 'R', 'F', 0, 0
    };
    static constexpr unsigned int modifiers[kHotkeyCount]{
        lholo::ui::kHotkeyModifierAlt,
        lholo::ui::kHotkeyModifierControl,
        lholo::ui::kHotkeyModifierControl,
        lholo::ui::kHotkeyModifierControl,
        lholo::ui::kHotkeyModifierControl,
        lholo::ui::kHotkeyModifierShift,
        lholo::ui::kHotkeyModifierShift,
        lholo::ui::kHotkeyModifierAlt,
        lholo::ui::kHotkeyModifierAlt,
        0,
        0,
        0,
        0
    };
    for (std::size_t index = 0; index < mHotkeys.size(); ++index) {
        mHotkeys[index].key.store(keys[index], std::memory_order_relaxed);
        mHotkeys[index].modifiers.store(modifiers[index], std::memory_order_relaxed);
    }
    stopHotkeyCapture();
}

void StructureUiState::setControlHeld(bool held) {
    mControlHeld.store(held, std::memory_order_release);
}
void StructureUiState::setAltHeld(bool held) { mAltHeld.store(held, std::memory_order_release); }
void StructureUiState::setShiftHeld(bool held) { mShiftHeld.store(held, std::memory_order_release); }

unsigned int StructureUiState::currentHotkeyModifiers() const {
    unsigned int modifiers{};
    if (mControlHeld.load(std::memory_order_acquire)) modifiers |= lholo::ui::kHotkeyModifierControl;
    if (mAltHeld.load(std::memory_order_acquire)) modifiers |= lholo::ui::kHotkeyModifierAlt;
    if (mShiftHeld.load(std::memory_order_acquire)) modifiers |= lholo::ui::kHotkeyModifierShift;
    return modifiers;
}

bool StructureUiState::tryPressHotkey(std::size_t index) {
    auto* storage = hotkeyStorage(index);
    return storage && !storage->held.exchange(true, std::memory_order_acq_rel);
}

bool StructureUiState::releaseHotkeysForKey(unsigned int key, std::uint64_t now) {
    bool consumed{};
    for (auto& hotkey : mHotkeys) {
        if (key == hotkey.key.load(std::memory_order_acquire)) {
            consumed = hotkey.held.exchange(false, std::memory_order_acq_rel) || consumed;
        }
    }
    if (key < mConsumeKeyReleaseUntil.size()) {
        auto& deadline = mConsumeKeyReleaseUntil[key];
        if (consumed) {
            deadline.store(now + 100, std::memory_order_release);
            return true;
        }
        if (now <= deadline.load(std::memory_order_acquire)) return true;
    }
    return false;
}

void StructureUiState::resetHotkeyState() {
    mControlHeld.store(false, std::memory_order_release);
    mAltHeld.store(false, std::memory_order_release);
    mShiftHeld.store(false, std::memory_order_release);
    for (auto& hotkey : mHotkeys) hotkey.held.store(false, std::memory_order_release);
    for (auto& deadline : mConsumeKeyReleaseUntil) deadline.store(0, std::memory_order_release);
}

std::uint64_t StructureUiState::ignoreHotkeyUntil() const {
    return mIgnoreHotkeyUntil.load(std::memory_order_acquire);
}

void StructureUiState::setIgnoreHotkeyUntil(std::uint64_t deadline) {
    mIgnoreHotkeyUntil.store(deadline, std::memory_order_release);
}

void StructureUiState::queueMove(std::size_t index) {
    switch (index) {
    case 0: mPendingOffsetX.fetch_sub(1, std::memory_order_relaxed); break;
    case 1: mPendingOffsetX.fetch_add(1, std::memory_order_relaxed); break;
    case 2: mPendingOffsetZ.fetch_sub(1, std::memory_order_relaxed); break;
    case 3: mPendingOffsetZ.fetch_add(1, std::memory_order_relaxed); break;
    case 4: mPendingOffsetY.fetch_add(1, std::memory_order_relaxed); break;
    case 5: mPendingOffsetY.fetch_sub(1, std::memory_order_relaxed); break;
    default: break;
    }
}

void StructureUiState::queueLayerDelta(int delta) {
    if (delta > 0) mPendingLayerDelta.fetch_add(1, std::memory_order_relaxed);
    else if (delta < 0) mPendingLayerDelta.fetch_sub(1, std::memory_order_relaxed);
}

void StructureUiState::queueToggleManual() {
    mPendingToggleManual.store(true, std::memory_order_release);
}

void StructureUiState::queueToggleEasy() {
    mPendingToggleEasy.store(true, std::memory_order_release);
}

void StructureUiState::queueLoadProjection() {
    mPendingLoadProjection.store(true, std::memory_order_release);
}

void StructureUiState::queueCloseProjection() {
    mPendingCloseProjection.store(true, std::memory_order_release);
}

void StructureUiState::requestSettingsSave() {
    mPendingSettingsSave.store(true, std::memory_order_release);
}

PendingHotkeyActions StructureUiState::consumePendingHotkeyActions() {
    return {
        mPendingOffsetX.exchange(0, std::memory_order_acq_rel),
        mPendingOffsetY.exchange(0, std::memory_order_acq_rel),
        mPendingOffsetZ.exchange(0, std::memory_order_acq_rel),
        mPendingLayerDelta.exchange(0, std::memory_order_acq_rel),
        mPendingSettingsSave.exchange(false, std::memory_order_acq_rel),
        mPendingToggleManual.exchange(false, std::memory_order_acq_rel),
        mPendingToggleEasy.exchange(false, std::memory_order_acq_rel),
        mPendingLoadProjection.exchange(false, std::memory_order_acq_rel),
        mPendingCloseProjection.exchange(false, std::memory_order_acq_rel)
    };
}

void StructureUiState::requestMaterialList() {
    mMaterialListRequested.store(true, std::memory_order_release);
}

bool StructureUiState::consumeMaterialListRequest() {
    return mMaterialListRequested.exchange(false, std::memory_order_acq_rel);
}

void StructureUiState::replaceMaterialRequirements(std::vector<MaterialRequirement> materials) {
    std::lock_guard lock(mMaterialMutex);
    mMaterialRequirements = std::move(materials);
    // Availability belongs to the previous requirement list; drop it so the HUD
    // never pairs new requirements with stale counts before the next scan.
    mMaterialAvailability.clear();
}

std::vector<MaterialRequirement> StructureUiState::materialRequirements() const {
    std::lock_guard lock(mMaterialMutex);
    return mMaterialRequirements;
}

void StructureUiState::setMaterialAvailability(std::vector<int> counts) {
    std::lock_guard lock(mMaterialMutex);
    mMaterialAvailability = std::move(counts);
}

MaterialSnapshot StructureUiState::materialSnapshot() const {
    std::lock_guard lock(mMaterialMutex);
    return {mMaterialRequirements, mMaterialAvailability};
}

void StructureUiState::clearMaterials() {
    mMaterialListRequested.store(false, std::memory_order_release);
    std::lock_guard lock(mMaterialMutex);
    mMaterialRequirements.clear();
    mMaterialAvailability.clear();
}

} // namespace lholo::structure::detail
