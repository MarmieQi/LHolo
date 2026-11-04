// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "structure/StructureSession.h"
#include "structure/IndependentTaskFiles.h"

#include <algorithm>
#include <cassert>
#include <limits>

namespace lholo::structure::detail {
namespace {

bool exchangeIfChanged(std::atomic_int& target, int value) {
    return target.exchange(value, std::memory_order_relaxed) != value;
}

int addClamped(int value, int delta) {
    auto const next = std::clamp(
        static_cast<long long>(value) + static_cast<long long>(delta),
        static_cast<long long>(std::numeric_limits<int>::min()),
        static_cast<long long>(std::numeric_limits<int>::max())
    );
    return static_cast<int>(next);
}

} // namespace

StructureSession& StructureSession::getInstance() {
    static StructureSession instance;
    return instance;
}

StructureTransformSnapshot StructureSession::transformRelaxed() const {
    return {
        mRotationQuarterTurns.load(std::memory_order_relaxed),
        mMirrorMode.load(std::memory_order_relaxed),
        mOffsetX.load(std::memory_order_relaxed),
        mOffsetY.load(std::memory_order_relaxed),
        mOffsetZ.load(std::memory_order_relaxed),
        layerDisplayModeFromInt(mLayerDisplayMode.load(std::memory_order_relaxed)),
        mDisplayLayer.load(std::memory_order_relaxed),
        layerAxisFromInt(mLayerAxis.load(std::memory_order_relaxed))
    };
}

SavedProjectionSnapshot StructureSession::savedProjectionLocked() const {
    return {
        mHasSavedProjection.load(std::memory_order_acquire),
        mSavedAnchorX.load(std::memory_order_relaxed),
        mSavedAnchorY.load(std::memory_order_relaxed),
        mSavedAnchorZ.load(std::memory_order_relaxed),
        {
            mSavedRotation.load(std::memory_order_relaxed),
            mSavedMirror.load(std::memory_order_relaxed),
            mSavedOffsetX.load(std::memory_order_relaxed),
            mSavedOffsetY.load(std::memory_order_relaxed),
            mSavedOffsetZ.load(std::memory_order_relaxed),
            layerDisplayModeFromInt(mSavedLayerDisplayMode.load(std::memory_order_relaxed)),
            mSavedDisplayLayer.load(std::memory_order_relaxed),
            layerAxisFromInt(mSavedLayerAxis.load(std::memory_order_relaxed))
        },
        mSavedStructurePath
    };
}

bool StructureSessionSnapshot::hasVisibleSelection() const {
    if (!loaded) return false;
    if (!loaded->composed) return true;
    auto const* file = independentTaskFile(*loaded, selectedSource);
    return editor && file && file->visible && file->loaded;
}

StructureSessionSnapshot StructureSession::snapshot() const {
    std::lock_guard lock(mMutex);
    StructureSessionSnapshot result;
    result.loaded    = mLoaded;
    result.status    = i18n::format(mStatus);
    result.lastPath  = mLastPath;
    result.transform = transformRelaxed();
    result.saved     = savedProjectionLocked();
    result.editor = mLoaded && mLoaded->composed ? mEditorStructure : mLoaded;
    result.selectedSource = mSelectedSource.load(std::memory_order_acquire);
    if (result.editor) {
        result.maxLayerY = maxLayerFor(*result.editor, LayerAxis::Y);
        result.maxLayerX = maxLayerFor(*result.editor, LayerAxis::X);
    }
    return result;
}

std::shared_ptr<LoadedStructure const> StructureSession::loaded() const {
    std::lock_guard lock(mMutex);
    return mLoaded;
}

StructureActivationSnapshot StructureSession::activation() const {
    std::lock_guard lock(mMutex);
    return {mLoaded, mLoadOrigin};
}

bool StructureSession::hasLoaded() const {
    std::lock_guard lock(mMutex);
    return static_cast<bool>(mLoaded);
}

std::string StructureSession::lastPath() const {
    std::lock_guard lock(mMutex);
    return mLastPath;
}

void StructureSession::setStatus(i18n::Message status) {
    std::lock_guard lock(mMutex);
    mStatus = std::move(status);
}

void StructureSession::setLastPath(std::string path) {
    std::lock_guard lock(mMutex);
    mLastPath = std::move(path);
}

void StructureSession::replaceLoadedLocked(
    std::shared_ptr<LoadedStructure> loaded, std::string path, i18n::Message status,
    StructureLoadOrigin origin
) {
    assert(loaded); // every published structure must have its own initial placement
    ++mLoadRevision;
    mLastPath = std::move(path);
    mStatus   = std::move(status);
    mLoaded   = std::move(loaded);
    if (!mLoaded->composed) { mEditorStructure.reset(); mSelectedSource = -1; }
    mLoadOrigin = origin;
}

bool StructureSession::completeRestoreLoad(
    StructureLoadRequest const& request, std::shared_ptr<LoadedStructure> loaded,
    SavedProjectionSnapshot const& saved, i18n::Message status
) {
    std::lock_guard lock(mMutex);
    if (request.revision != mLoadRevision) return false;
    setRotation(saved.transform.rotation);
    setMirror(std::clamp(saved.transform.mirror, 0, 2));
    setOffsetX(saved.transform.offsetX);
    setOffsetY(saved.transform.offsetY);
    setOffsetZ(saved.transform.offsetZ);
    setLayerDisplayMode(saved.transform.layerDisplayMode);
    setDisplayLayer(saved.transform.displayLayer);
    setLayerAxis(saved.transform.layerAxis);
    replaceLoadedLocked(std::move(loaded), saved.structurePath, std::move(status),
        StructureLoadOrigin{saved.anchorX, saved.anchorY, saved.anchorZ, std::nullopt});
    return true;
}

void StructureSession::cancelPendingLoad() {
    std::lock_guard lock(mMutex);
    ++mLoadRevision;
    mCompositionPending=false;
}

StructureLoadRequest StructureSession::beginFileLoad(StructureLoadOrigin origin) {
    std::lock_guard lock(mMutex);
    mCompositionPending=false;
    mStatus = i18n::Message{i18n::TextKey::StatusLoading};
    return {++mLoadRevision, origin};
}
StructureLoadRequest StructureSession::beginCompositionLoad(StructureLoadOrigin origin) {
    std::lock_guard lock(mMutex);
    mCompositionPending=true;
    mStatus=i18n::Message{i18n::TextKey::TasksLoading};
    return {++mLoadRevision,origin};
}

bool StructureSession::completeFileLoad(
    StructureLoadRequest const& request, std::shared_ptr<LoadedStructure> loaded,
    std::string path, i18n::Message status
) {
    std::lock_guard lock(mMutex);
    if (request.revision != mLoadRevision) return false;
    resetTransform();
    replaceLoadedLocked(std::move(loaded), std::move(path), std::move(status), request.origin);
    return true;
}

bool StructureSession::failFileLoad(StructureLoadRequest const& request, i18n::Message status) {
    std::lock_guard lock(mMutex);
    if (request.revision != mLoadRevision) return false;
    ++mLoadRevision;
    mStatus = std::move(status);
    mCompositionPending=false;
    return true;
}

void StructureSession::clearLoaded(i18n::Message status) {
    std::lock_guard lock(mMutex);
    // Freeze the last active transform before dropping the structure. Once
    // mLoaded is empty, menu models legitimately clamp their current layer to
    // zero; that transient empty-session value must not replace the restore
    // snapshot.
    refreshSavedTransformLocked();
    ++mLoadRevision;
    mLoaded.reset();
    mCompositionPending=false;
    mEditorStructure.reset(); mSelectedSource = -1;
    mLoadOrigin.reset();
    mStatus = std::move(status);
}

StructureTransformSnapshot StructureSession::transform() const { return transformRelaxed(); }

bool StructureSession::completeCompositionLoad(StructureLoadRequest const& request, std::shared_ptr<LoadedStructure> loaded, StructureLoadOrigin origin) {
    std::lock_guard lock(mMutex);
    if (request.revision != mLoadRevision) return false;
    mSelectedSource=-2; mEditorStructure.reset();
    replaceLoadedLocked(std::move(loaded), {}, i18n::Message{i18n::TextKey::TasksActive}, origin);
    mCompositionPending=false;
    return true;
}
void StructureSession::selectCompositionFile(std::shared_ptr<LoadedStructure const> loaded, StructureTransformSnapshot const& t, int source, std::string path) {
    std::lock_guard lock(mMutex);
    if (!mLoaded || !mLoaded->composed) return;
    mEditorStructure=std::move(loaded); mSelectedSource=source; mLastPath=std::move(path); ++mLoadRevision;
    setRotation(t.rotation); setMirror(t.mirror); setOffsetX(t.offsetX); setOffsetY(t.offsetY); setOffsetZ(t.offsetZ);
    setLayerAxis(t.layerAxis); setLayerDisplayMode(t.layerDisplayMode); setDisplayLayer(t.displayLayer);
}
int StructureSession::selectedSourceFor(LoadedStructure const& loaded) const {
    std::lock_guard lock(mMutex);
    return mLoaded.get()==&loaded ? mSelectedSource.load(std::memory_order_acquire) : -2;
}

TransformState StructureSession::transformEditorState() const {
    std::lock_guard lock(mMutex);
    auto const t = transformRelaxed();
    return {{t.rotation, t.mirror, t.offsetX, t.offsetY, t.offsetZ}, mLoadRevision};
}

bool StructureSession::applyTransformEdit(TransformEdit const& edit) {
    std::lock_guard lock(mMutex);
    if (mCompositionPending || (mLoaded && mLoaded->composed && mSelectedSource<0)
        || !validTransformEdit(edit) || edit.context != mLoadRevision) return false;
    std::atomic_int* target{};
    switch (edit.field) {
    case TransformField::Rotation: target = &mRotationQuarterTurns; break;
    case TransformField::Mirror: target = &mMirrorMode; break;
    case TransformField::X: target = &mOffsetX; break;
    case TransformField::Y: target = &mOffsetY; break;
    case TransformField::Z: target = &mOffsetZ; break;
    }
    auto expected = edit.expected;
    return target && target->compare_exchange_strong(expected, edit.value, std::memory_order_relaxed);
}

bool StructureSession::layerDisplayEnabled() const {
    return layerDisplayModeFromInt(mLayerDisplayMode.load(std::memory_order_acquire))
        != LayerDisplayMode::All;
}

LayerSettingsSnapshot StructureSession::layerSettings() const {
    std::lock_guard lock(mMutex);
    auto t = transformRelaxed();
    LayerSettingsSnapshot result{{t.layerAxis, t.layerDisplayMode, t.displayLayer}, {}, mLoadRevision};
    auto editor = mLoaded && mLoaded->composed ? mEditorStructure : mLoaded;
    if (editor) result.maximum = {maxLayerFor(*editor, LayerAxis::Y), maxLayerFor(*editor, LayerAxis::X),
        std::max(0, static_cast<int>(std::min(editor->materialCount, static_cast<std::size_t>(std::numeric_limits<int>::max()))) - 1)};
    return result;
}

bool StructureSession::applyLayerSettings(std::uint64_t context, LayerSettings const& expected, LayerSettings desired) {
    std::lock_guard lock(mMutex);
    auto const t = transformRelaxed();
    if (context != mLoadRevision || LayerSettings{t.layerAxis, t.layerDisplayMode, t.displayLayer} != expected
        || toInt(desired.axis) < 0 || toInt(desired.axis) > 2 || toInt(desired.mode) < 0 || toInt(desired.mode) > 3) return false;
    if (desired == expected) return true;
    if (mCompositionPending || (mLoaded && mLoaded->composed && mSelectedSource<0)) return false;
    auto editor = mLoaded && mLoaded->composed ? mEditorStructure : mLoaded;
    auto const maximum = !editor ? 0 : desired.axis == LayerAxis::Material
        ? static_cast<int>(std::min(editor->materialCount, static_cast<std::size_t>(std::numeric_limits<int>::max()))) - 1
        : maxLayerFor(*editor, desired.axis);
    desired.layer = std::clamp(desired.layer, 0, std::max(0, maximum));
    setLayerAxis(desired.axis);
    setLayerDisplayMode(desired.mode);
    setDisplayLayer(desired.layer);
    return true;
}

void StructureSession::resetTransform() {
    mRotationQuarterTurns.store(0, std::memory_order_relaxed);
    mMirrorMode.store(0, std::memory_order_relaxed);
    mOffsetX.store(0, std::memory_order_relaxed);
    mOffsetY.store(0, std::memory_order_relaxed);
    mOffsetZ.store(0, std::memory_order_relaxed);
    mLayerDisplayMode.store(0, std::memory_order_relaxed);
    mDisplayLayer.store(0, std::memory_order_relaxed);
    mLayerAxis.store(0, std::memory_order_relaxed);
}

bool StructureSession::setRotation(int value) { return exchangeIfChanged(mRotationQuarterTurns, value); }
bool StructureSession::setMirror(int value) { return exchangeIfChanged(mMirrorMode, value); }
bool StructureSession::setOffsetX(int value) { return exchangeIfChanged(mOffsetX, value); }
bool StructureSession::setOffsetY(int value) { return exchangeIfChanged(mOffsetY, value); }
bool StructureSession::setOffsetZ(int value) { return exchangeIfChanged(mOffsetZ, value); }
bool StructureSession::setLayerDisplayMode(LayerDisplayMode value) {
    return exchangeIfChanged(mLayerDisplayMode, toInt(value));
}
bool StructureSession::setDisplayLayer(int value) { return exchangeIfChanged(mDisplayLayer, value); }
bool StructureSession::setLayerAxis(LayerAxis value) {
    return exchangeIfChanged(mLayerAxis, toInt(value));
}

void StructureSession::adjustOffsets(int deltaX, int deltaY, int deltaZ) {
    if (mCompositionPending || mSelectedSource==-2) return;
    if (deltaX != 0) setOffsetX(addClamped(mOffsetX.load(std::memory_order_relaxed), deltaX));
    if (deltaY != 0) setOffsetY(addClamped(mOffsetY.load(std::memory_order_relaxed), deltaY));
    if (deltaZ != 0) setOffsetZ(addClamped(mOffsetZ.load(std::memory_order_relaxed), deltaZ));
}

bool StructureSession::adjustDisplayLayer(int delta) {
    if (mCompositionPending || mSelectedSource==-2) return false;
    if (delta == 0 || layerDisplayModeFromInt(mLayerDisplayMode.load(std::memory_order_relaxed))
        == LayerDisplayMode::All) return false;
    auto const axis = layerAxisFromInt(mLayerAxis.load(std::memory_order_relaxed));
    auto       maxLayer = 0;
    {
        std::lock_guard lock(mMutex);
        auto editor = mLoaded && mLoaded->composed ? mEditorStructure : mLoaded;
        if (editor) {
            maxLayer = axis == LayerAxis::Material
                ? std::max(0, static_cast<int>(editor->materialCount) - 1)
                : maxLayerFor(*editor, axis);
        }
    }
    auto const current = static_cast<long long>(mDisplayLayer.load(std::memory_order_relaxed));
    auto const next = std::clamp(current + static_cast<long long>(delta), 0LL, static_cast<long long>(maxLayer));
    mDisplayLayer.store(static_cast<int>(next), std::memory_order_relaxed);
    return true;
}

SavedProjectionSnapshot StructureSession::savedProjection() const {
    std::lock_guard lock(mMutex);
    return savedProjectionLocked();
}

void StructureSession::setSavedProjection(SavedProjectionSnapshot const& saved) {
    std::lock_guard lock(mMutex);
    mHasSavedProjection.store(false, std::memory_order_relaxed);
    mSavedAnchorX.store(saved.anchorX, std::memory_order_relaxed);
    mSavedAnchorY.store(saved.anchorY, std::memory_order_relaxed);
    mSavedAnchorZ.store(saved.anchorZ, std::memory_order_relaxed);
    mSavedRotation.store(saved.transform.rotation, std::memory_order_relaxed);
    mSavedMirror.store(saved.transform.mirror, std::memory_order_relaxed);
    mSavedOffsetX.store(saved.transform.offsetX, std::memory_order_relaxed);
    mSavedOffsetY.store(saved.transform.offsetY, std::memory_order_relaxed);
    mSavedOffsetZ.store(saved.transform.offsetZ, std::memory_order_relaxed);
    mSavedLayerDisplayMode.store(toInt(saved.transform.layerDisplayMode), std::memory_order_relaxed);
    mSavedDisplayLayer.store(saved.transform.displayLayer, std::memory_order_relaxed);
    mSavedLayerAxis.store(toInt(saved.transform.layerAxis), std::memory_order_relaxed);
    mSavedStructurePath = saved.structurePath;
    mHasSavedProjection.store(saved.available, std::memory_order_release);
}

void StructureSession::refreshSavedTransformIfActive() {
    std::lock_guard lock(mMutex);
    refreshSavedTransformLocked();
}

void StructureSession::refreshSavedTransformLocked() {
    if (!mLoaded || mLoaded->composed || !mHasSavedProjection.load(std::memory_order_acquire)) return;
    auto const current = transformRelaxed();
    mSavedRotation.store(current.rotation, std::memory_order_relaxed);
    mSavedMirror.store(current.mirror, std::memory_order_relaxed);
    mSavedOffsetX.store(current.offsetX, std::memory_order_relaxed);
    mSavedOffsetY.store(current.offsetY, std::memory_order_relaxed);
    mSavedOffsetZ.store(current.offsetZ, std::memory_order_relaxed);
    mSavedLayerDisplayMode.store(toInt(current.layerDisplayMode), std::memory_order_relaxed);
    mSavedDisplayLayer.store(current.displayLayer, std::memory_order_relaxed);
    mSavedLayerAxis.store(toInt(current.layerAxis), std::memory_order_relaxed);
}

void StructureSession::recordProjectionAnchor(int x, int y, int z) {
    std::lock_guard lock(mMutex);
    if (mLoaded && mLoaded->composed) return;
    mHasSavedProjection.store(false, std::memory_order_relaxed);
    auto const current = transformRelaxed();
    mSavedAnchorX.store(x, std::memory_order_relaxed);
    mSavedAnchorY.store(y, std::memory_order_relaxed);
    mSavedAnchorZ.store(z, std::memory_order_relaxed);
    mSavedRotation.store(current.rotation, std::memory_order_relaxed);
    mSavedMirror.store(current.mirror, std::memory_order_relaxed);
    mSavedOffsetX.store(current.offsetX, std::memory_order_relaxed);
    mSavedOffsetY.store(current.offsetY, std::memory_order_relaxed);
    mSavedOffsetZ.store(current.offsetZ, std::memory_order_relaxed);
    mSavedLayerDisplayMode.store(toInt(current.layerDisplayMode), std::memory_order_relaxed);
    mSavedDisplayLayer.store(current.displayLayer, std::memory_order_relaxed);
    mSavedLayerAxis.store(toInt(current.layerAxis), std::memory_order_relaxed);
    mSavedStructurePath = mLastPath;
    mHasSavedProjection.store(true, std::memory_order_release);
}

int maxLayerFor(LoadedStructure const& structure, LayerAxis axis) {
    return std::max(0, (axis == LayerAxis::X ? structure.sizeX : structure.sizeY) - 1);
}

} // namespace lholo::structure::detail
