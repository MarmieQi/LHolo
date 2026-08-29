// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Projection-session ownership and synchronization. Projection algorithms run
// through withLockedState, so the mutex, state and capture wireframe cannot be
// acquired or retained independently.

#pragma once

#include "overlay/BoundsWireframe.h"
#include "projection/core/ProjectionState.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

namespace lholo::projection::detail {

struct ProjectionAnchor {
    int x{};
    int y{};
    int z{};
};

class ProjectionSession {
public:
    static ProjectionSession& getInstance();

    ProjectionSession(ProjectionSession const&)            = delete;
    ProjectionSession(ProjectionSession&&)                 = delete;
    ProjectionSession& operator=(ProjectionSession const&) = delete;
    ProjectionSession& operator=(ProjectionSession&&)      = delete;

    template <class Operation>
    decltype(auto) withLockedState(Operation&& operation) {
        using Result = std::invoke_result_t<
            Operation,
            ProjectionState&,
            overlay::BoundsWireframe&
        >;
        static_assert(!std::is_reference_v<Result> && !std::is_pointer_v<Result>);
        std::lock_guard lock(mStateMutex);
        return std::invoke(
            std::forward<Operation>(operation),
            mState,
            mCaptureBounds
        );
    }

    [[nodiscard]] float opacity() const;
    void setOpacity(float opacity);
    [[nodiscard]] float correctionFillOpacity() const;
    void setCorrectionFillOpacity(float opacity);
    [[nodiscard]] float correctionOutlineOpacity() const;
    void setCorrectionOutlineOpacity(float opacity);
    [[nodiscard]] bool structureBoundsEnabled() const;
    void setStructureBoundsEnabled(bool enabled);
    // See-through (X-ray): draw the meshes with depth testing off so they show
    // through world blocks. Independent for the correction markers and the
    // projection ghost blocks.
    [[nodiscard]] bool correctionSeeThrough() const;
    void setCorrectionSeeThrough(bool enabled);
    [[nodiscard]] bool missingSeeThrough() const;
    void setMissingSeeThrough(bool enabled);
    [[nodiscard]] bool projectionSeeThrough() const;
    void setProjectionSeeThrough(bool enabled);

    [[nodiscard]] std::optional<ProjectionAnchor> consumeAnchor();
    void requestAnchor(int x, int y, int z);
    void cancelAnchorRequest();

private:
    ProjectionSession() = default;

    std::atomic<float> mOpacity{1.0f};
    std::atomic<float> mCorrectionFillOpacity{0.15f};
    std::atomic<float> mCorrectionOutlineOpacity{1.0f};
    std::atomic_bool   mStructureBoundsEnabled{true};
    std::atomic_bool   mCorrectionSeeThrough{false};
    std::atomic_bool   mMissingSeeThrough{false};
    std::atomic_bool   mProjectionSeeThrough{false};
    std::atomic_bool   mPendingAnchor{false};
    std::atomic_int    mPendingAnchorX{0};
    std::atomic_int    mPendingAnchorY{0};
    std::atomic_int    mPendingAnchorZ{0};

    std::mutex               mStateMutex;
    ProjectionState          mState;
    overlay::BoundsWireframe mCaptureBounds;
};

} // namespace lholo::projection::detail
