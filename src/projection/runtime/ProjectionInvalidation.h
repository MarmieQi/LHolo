// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Reconciles cached projection settings with the current frame and applies the
// existing section invalidation, revision and progress-reset rules.

#pragma once

#include "structure/LayerDisplayTypes.h"

namespace lholo::projection::detail {

struct ProjectionState;

struct ProjectionInvalidationSettings {
    int   mirrorMode{};
    int   rotationTurns{};
    int   offsetX{};
    int   offsetY{};
    int   offsetZ{};
    structure::LayerDisplayMode layerDisplayMode{structure::LayerDisplayMode::All};
    int   displayLayer{};
    structure::LayerAxis layerAxis{structure::LayerAxis::Y};
    float structureOpacity{};
    float correctionFillOpacity{};
    float correctionOutlineOpacity{};
};

struct ProjectionInvalidationResult {
    bool geometryTransformChanged{};
    bool placementMoved{};
    bool layerChanged{};

    [[nodiscard]] bool placementViewChanged() const {
        return geometryTransformChanged || placementMoved || layerChanged;
    }
};

ProjectionInvalidationResult reconcileProjectionInvalidation(
    ProjectionState&                            state,
    ProjectionInvalidationSettings const& settings
);

} // namespace lholo::projection::detail
