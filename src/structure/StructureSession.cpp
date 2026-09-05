// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "structure/StructureSession.h"

#include <algorithm>
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
        mLayerDisplayMode.load(std::memory_order_relaxed),
        mDisplayLayer.load(std::memory_order_relaxed),
        mLayerAxis.load(std::memory_order_relaxed)
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
            mSavedLayerDisplayMode.load(std::memory_order_relaxed),
            mSavedDisplayLayer.load(std::memory_order_relaxed),
            mSavedLayerAxis.load(std::memory_order_relaxed)
        },
        mSavedStructurePath
    };
}

StructureSessionSnapshot StructureSession::snapshot() const {
    std::lock_guard lock(mMutex);
    StructureSessionSnapshot result;
    result.loaded    = mLoaded;
    result.status    = mStatus;
    result.lastPath  = mLastPath;
    result.transform = transformRelaxed();
    result.saved     = savedProjectionLocked();
    if (mLoaded) {
        result.maxLayerY = maxLayerFor(*mLoaded, 0);
        result.maxLayerX = maxLayerFor(*mLoaded, 1);
    }
    return result;
}

std::shared_ptr<LoadedStructure const> StructureSession::loaded() const {
    std::lock_guard lock(mMutex);
    return mLoaded;
}

bool StructureSession::hasLoaded() const {
    std::lock_guard lock(mMutex);
    return static_cast<bool>(mLoaded);
}

std::string StructureSession::lastPath() const {
    std::lock_guard lock(mMutex);
    return mLastPath;
}

void StructureSession::setStatus(std::string status) {
    std::lock_guard lock(mMutex);
    mStatus = std::move(status);
}

void StructureSession::setLastPath(std::string path) {
    std::lock_guard lock(mMutex);
    mLastPath = std::move(path);
}

void StructureSession::replaceLoaded(
    std::shared_ptr<LoadedStructure> loaded,
    std::string                      path,
    std::string                      status
) {
    std::lock_guard lock(mMutex);
    mLastPath = std::move(path);
    mStatus   = std::move(status);
    mLoaded   = std::move(loaded);
}

void StructureSession::clearLoaded(std::string status) {
    std::lock_guard lock(mMutex);
    // Freeze the last active transform before dropping the structure. Once
    // mLoaded is empty, menu models legitimately clamp their current layer to
    // zero; that transient empty-session value must not replace the restore
    // snapshot.
    refreshSavedTransformLocked();
    mLoaded.reset();
    mStatus = std::move(status);
}

StructureTransformSnapshot StructureSession::transform() const { return transformRelaxed(); }

bool StructureSession::layerDisplayEnabled() const {
    return mLayerDisplayMode.load(std::memory_order_acquire) != 0;
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
bool StructureSession::setLayerDisplayMode(int value) { return exchangeIfChanged(mLayerDisplayMode, value); }
bool StructureSession::setDisplayLayer(int value) { return exchangeIfChanged(mDisplayLayer, value); }
bool StructureSession::setLayerAxis(int value) { return exchangeIfChanged(mLayerAxis, value); }

void StructureSession::adjustOffsets(int deltaX, int deltaY, int deltaZ) {
    if (deltaX != 0) setOffsetX(addClamped(mOffsetX.load(std::memory_order_relaxed), deltaX));
    if (deltaY != 0) setOffsetY(addClamped(mOffsetY.load(std::memory_order_relaxed), deltaY));
    if (deltaZ != 0) setOffsetZ(addClamped(mOffsetZ.load(std::memory_order_relaxed), deltaZ));
}

bool StructureSession::adjustDisplayLayer(int delta) {
    if (delta == 0 || mLayerDisplayMode.load(std::memory_order_relaxed) == 0) return false;
    auto const axis = mLayerAxis.load(std::memory_order_relaxed);
    auto       maxLayer = 0;
    {
        std::lock_guard lock(mMutex);
        if (mLoaded) {
            maxLayer = axis == 2
                ? static_cast<int>(mLoaded->materialCount)
                : maxLayerFor(*mLoaded, axis);
            if (maxLayer > 0) --maxLayer;
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
    mSavedLayerDisplayMode.store(saved.transform.layerDisplayMode, std::memory_order_relaxed);
    mSavedDisplayLayer.store(saved.transform.displayLayer, std::memory_order_relaxed);
    mSavedLayerAxis.store(saved.transform.layerAxis, std::memory_order_relaxed);
    mSavedStructurePath = saved.structurePath;
    mHasSavedProjection.store(saved.available, std::memory_order_release);
}

void StructureSession::refreshSavedTransformIfActive() {
    std::lock_guard lock(mMutex);
    refreshSavedTransformLocked();
}

void StructureSession::refreshSavedTransformLocked() {
    if (!mLoaded || !mHasSavedProjection.load(std::memory_order_acquire)) return;
    auto const current = transformRelaxed();
    mSavedRotation.store(current.rotation, std::memory_order_relaxed);
    mSavedMirror.store(current.mirror, std::memory_order_relaxed);
    mSavedOffsetX.store(current.offsetX, std::memory_order_relaxed);
    mSavedOffsetY.store(current.offsetY, std::memory_order_relaxed);
    mSavedOffsetZ.store(current.offsetZ, std::memory_order_relaxed);
    mSavedLayerDisplayMode.store(current.layerDisplayMode, std::memory_order_relaxed);
    mSavedDisplayLayer.store(current.displayLayer, std::memory_order_relaxed);
    mSavedLayerAxis.store(current.layerAxis, std::memory_order_relaxed);
}

void StructureSession::recordProjectionAnchor(int x, int y, int z) {
    std::lock_guard lock(mMutex);
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
    mSavedLayerDisplayMode.store(current.layerDisplayMode, std::memory_order_relaxed);
    mSavedDisplayLayer.store(current.displayLayer, std::memory_order_relaxed);
    mSavedLayerAxis.store(current.layerAxis, std::memory_order_relaxed);
    mSavedStructurePath = mLastPath;
    mHasSavedProjection.store(true, std::memory_order_release);
}

int maxLayerFor(LoadedStructure const& structure, int axis) {
    return std::max(0, (axis == 1 ? structure.sizeX : structure.sizeY) - 1);
}

} // namespace lholo::structure::detail
