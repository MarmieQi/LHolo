// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Structure-session ownership and synchronization. Callers receive snapshots
// or use concrete operations; the underlying mutex and mutable storage never
// escape this module.

#pragma once

#include "i18n/Message.h"
#include "structure/LayerDisplayTypes.h"
#include "structure/StructureLoader.h"
#include "structure/TransformTypes.h"
#include "structure/LayerSettings.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
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
    // Selected file in the current projection view; empty across dimensions.
    std::shared_ptr<LoadedStructure const> editor;
    int                                    selectedSource{-1};
    std::string                            status;
    std::string                            lastPath;
    int                                    maxLayerY{};
    int                                    maxLayerX{};
    StructureTransformSnapshot             transform;
    SavedProjectionSnapshot                saved;

    // Selection remains editable when hidden; projection consumers must check visibility separately.
    [[nodiscard]] bool hasVisibleSelection() const;
};

// Runtime placement intent, kept separate from immutable file contents.
struct StructureLoadOrigin {
    int x{}, y{}, z{};
    std::optional<int> dimensionId;
    bool operator==(StructureLoadOrigin const&) const = default;
};

struct StructureLoadRequest {
    std::uint64_t revision{};
    StructureLoadOrigin origin;
};

struct StructureActivationSnapshot {
    std::shared_ptr<LoadedStructure const> loaded;
    std::optional<StructureLoadOrigin> origin;
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
    [[nodiscard]] StructureActivationSnapshot activation() const;
    [[nodiscard]] bool hasLoaded() const;
    [[nodiscard]] std::string lastPath() const;

    // Status is stored as a key plus arguments, never as rendered text: the
    // menu renders it every frame, so a language switch applies to messages
    // that were produced before the switch.
    void setStatus(i18n::Message status);
    void setLastPath(std::string path);
    void clearLoaded(i18n::Message status);
    bool completeRestoreLoad(StructureLoadRequest const& request, std::shared_ptr<LoadedStructure> loaded,
                             SavedProjectionSnapshot const& saved, i18n::Message status);
    void cancelPendingLoad();
    [[nodiscard]] StructureLoadRequest beginFileLoad(StructureLoadOrigin origin);
    [[nodiscard]] StructureLoadRequest beginCompositionLoad(StructureLoadOrigin origin);
    bool completeFileLoad(StructureLoadRequest const& request, std::shared_ptr<LoadedStructure> loaded,
                          std::string path, i18n::Message status);
    bool failFileLoad(StructureLoadRequest const& request, i18n::Message status);
    bool completeCompositionLoad(StructureLoadRequest const&, std::shared_ptr<LoadedStructure>, StructureLoadOrigin);
    void selectCompositionFile(std::shared_ptr<LoadedStructure const>, StructureTransformSnapshot const&, int sourceIndex, std::string path);
    [[nodiscard]] int selectedSource() const { return mSelectedSource.load(std::memory_order_acquire); }
    [[nodiscard]] int selectedSourceFor(LoadedStructure const&) const;

    [[nodiscard]] StructureTransformSnapshot transform() const;
    [[nodiscard]] TransformState transformEditorState() const;
    bool applyTransformEdit(TransformEdit const& edit);
    [[nodiscard]] LayerSettingsSnapshot layerSettings() const;
    bool applyLayerSettings(std::uint64_t context, LayerSettings const& expected, LayerSettings desired);
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
    void replaceLoadedLocked(std::shared_ptr<LoadedStructure> loaded, std::string path,
                             i18n::Message status, StructureLoadOrigin origin);

    mutable std::mutex              mMutex;
    std::shared_ptr<LoadedStructure> mLoaded;
    std::shared_ptr<LoadedStructure const> mEditorStructure;
    std::atomic_int mSelectedSource{-1};
    std::atomic_bool mCompositionPending{};
    std::optional<StructureLoadOrigin> mLoadOrigin;
    std::uint64_t mLoadRevision{};
    std::string                      mSavedStructurePath;
    std::string                      mLastPath;
    i18n::Message                    mStatus{i18n::TextKey::StatusNotLoaded};

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

// Maximum zero-based spatial layer index, not the number of layers (X/Y only).
int maxLayerFor(LoadedStructure const& structure, LayerAxis axis);

} // namespace lholo::structure::detail
