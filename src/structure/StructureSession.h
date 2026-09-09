// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Structure-session ownership and synchronization. Callers receive snapshots
// or use concrete operations; the underlying mutex and mutable storage never
// escape this module.

#pragma once

#include "structure/LayerDisplayTypes.h"
#include "structure/StructureLoader.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace lholo::structure::detail {

struct StructureTransformSnapshot {
    int rotation{};
    int mirror{};
    int offsetX{};
    int offsetY{};
    int offsetZ{};
    LayerDisplayMode layerDisplayMode{LayerDisplayMode::All};
    int displayLayer{};
    LayerAxis layerAxis{LayerAxis::Y};
};

struct SavedProjectionSnapshot {
    bool available{};
    int  anchorX{};
    int  anchorY{};
    int  anchorZ{};
    StructureTransformSnapshot transform;
    std::string                structurePath;
};

struct StructureSessionSnapshot {
    std::shared_ptr<LoadedStructure const> loaded;
    std::string                            status;
    std::string                            lastPath;
    int                                    maxLayerY{};
    int                                    maxLayerX{};
    StructureTransformSnapshot             transform;
    SavedProjectionSnapshot                saved;
};

class StructureSession {
public:
    static StructureSession& getInstance();

    StructureSession(StructureSession const&)            = delete;
    StructureSession(StructureSession&&)                 = delete;
    StructureSession& operator=(StructureSession const&) = delete;
    StructureSession& operator=(StructureSession&&)      = delete;

    [[nodiscard]] StructureSessionSnapshot snapshot() const;
    [[nodiscard]] std::shared_ptr<LoadedStructure const> loaded() const;
    [[nodiscard]] bool hasLoaded() const;
    [[nodiscard]] std::string lastPath() const;

    void setStatus(std::string status);
    void setLastPath(std::string path);
    void replaceLoaded(std::shared_ptr<LoadedStructure> loaded, std::string path, std::string status);
    void clearLoaded(std::string status);

    [[nodiscard]] StructureTransformSnapshot transform() const;
    [[nodiscard]] bool layerDisplayEnabled() const;
    void resetTransform();
    bool setRotation(int value);
    bool setMirror(int value);
    bool setOffsetX(int value);
    bool setOffsetY(int value);
    bool setOffsetZ(int value);
    bool setLayerDisplayMode(LayerDisplayMode value);
    bool setDisplayLayer(int value);
    bool setLayerAxis(LayerAxis value);
    void adjustOffsets(int deltaX, int deltaY, int deltaZ);
    bool adjustDisplayLayer(int delta);

    [[nodiscard]] SavedProjectionSnapshot savedProjection() const;
    void setSavedProjection(SavedProjectionSnapshot const& saved);
    void refreshSavedTransformIfActive();
    void recordProjectionAnchor(int x, int y, int z);

private:
    StructureSession() = default;

    [[nodiscard]] StructureTransformSnapshot transformRelaxed() const;
    [[nodiscard]] SavedProjectionSnapshot savedProjectionLocked() const;
    void refreshSavedTransformLocked();

    mutable std::mutex              mMutex;
    std::shared_ptr<LoadedStructure> mLoaded;
    std::string                      mSavedStructurePath;
    std::string                      mLastPath;
    std::string                      mStatus{"尚未加载结构文件"};

    std::atomic_int mRotationQuarterTurns{0};
    std::atomic_int mMirrorMode{0};
    std::atomic_int mOffsetX{0};
    std::atomic_int mOffsetY{0};
    std::atomic_int mOffsetZ{0};
    std::atomic_int mLayerDisplayMode{0};
    std::atomic_int mDisplayLayer{0};
    std::atomic_int mLayerAxis{0};

    std::atomic_bool mHasSavedProjection{false};
    std::atomic_int  mSavedAnchorX{0};
    std::atomic_int  mSavedAnchorY{0};
    std::atomic_int  mSavedAnchorZ{0};
    std::atomic_int  mSavedRotation{0};
    std::atomic_int  mSavedMirror{0};
    std::atomic_int  mSavedOffsetX{0};
    std::atomic_int  mSavedOffsetY{0};
    std::atomic_int  mSavedOffsetZ{0};
    std::atomic_int  mSavedLayerDisplayMode{0};
    std::atomic_int  mSavedDisplayLayer{0};
    std::atomic_int  mSavedLayerAxis{0};
};

int maxLayerFor(LoadedStructure const& structure, LayerAxis axis);

} // namespace lholo::structure::detail
