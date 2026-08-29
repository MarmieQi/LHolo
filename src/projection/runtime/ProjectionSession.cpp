// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/runtime/ProjectionSession.h"

#include <algorithm>

namespace lholo::projection::detail {

ProjectionSession& ProjectionSession::getInstance() {
    static ProjectionSession instance;
    return instance;
}

float ProjectionSession::opacity() const {
    return mOpacity.load(std::memory_order_relaxed);
}

void ProjectionSession::setOpacity(float opacity) {
    mOpacity.store(std::clamp(opacity, 0.0f, 1.0f), std::memory_order_relaxed);
}

float ProjectionSession::correctionFillOpacity() const {
    return mCorrectionFillOpacity.load(std::memory_order_relaxed);
}

void ProjectionSession::setCorrectionFillOpacity(float opacity) {
    mCorrectionFillOpacity.store(std::clamp(opacity, 0.0f, 1.0f), std::memory_order_relaxed);
}

float ProjectionSession::correctionOutlineOpacity() const {
    return mCorrectionOutlineOpacity.load(std::memory_order_relaxed);
}

void ProjectionSession::setCorrectionOutlineOpacity(float opacity) {
    mCorrectionOutlineOpacity.store(std::clamp(opacity, 0.0f, 1.0f), std::memory_order_relaxed);
}

bool ProjectionSession::structureBoundsEnabled() const {
    return mStructureBoundsEnabled.load(std::memory_order_relaxed);
}

void ProjectionSession::setStructureBoundsEnabled(bool enabled) {
    mStructureBoundsEnabled.store(enabled, std::memory_order_relaxed);
}

bool ProjectionSession::correctionSeeThrough() const {
    return mCorrectionSeeThrough.load(std::memory_order_relaxed);
}

void ProjectionSession::setCorrectionSeeThrough(bool enabled) {
    mCorrectionSeeThrough.store(enabled, std::memory_order_relaxed);
}

bool ProjectionSession::missingSeeThrough() const {
    return mMissingSeeThrough.load(std::memory_order_relaxed);
}

void ProjectionSession::setMissingSeeThrough(bool enabled) {
    mMissingSeeThrough.store(enabled, std::memory_order_relaxed);
}

bool ProjectionSession::projectionSeeThrough() const {
    return mProjectionSeeThrough.load(std::memory_order_relaxed);
}

void ProjectionSession::setProjectionSeeThrough(bool enabled) {
    mProjectionSeeThrough.store(enabled, std::memory_order_relaxed);
}

std::optional<ProjectionAnchor> ProjectionSession::consumeAnchor() {
    if (!mPendingAnchor.exchange(false, std::memory_order_acq_rel)) return std::nullopt;
    return ProjectionAnchor{
        mPendingAnchorX.load(std::memory_order_relaxed),
        mPendingAnchorY.load(std::memory_order_relaxed),
        mPendingAnchorZ.load(std::memory_order_relaxed)
    };
}

void ProjectionSession::requestAnchor(int x, int y, int z) {
    mPendingAnchorX.store(x, std::memory_order_relaxed);
    mPendingAnchorY.store(y, std::memory_order_relaxed);
    mPendingAnchorZ.store(z, std::memory_order_relaxed);
    mPendingAnchor.store(true, std::memory_order_release);
}

void ProjectionSession::cancelAnchorRequest() {
    mPendingAnchor.store(false, std::memory_order_release);
}

} // namespace lholo::projection::detail
