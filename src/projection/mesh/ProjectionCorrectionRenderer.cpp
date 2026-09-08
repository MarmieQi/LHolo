// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "projection/mesh/ProjectionCorrectionRenderer.h"

#include "plugin/LHolo.h"
#include "ll/api/mod/NativeMod.h"

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/core/ProjectionState.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <variant>
#include <vector>

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/minecraft_renderer/resources/OffscreenCaptureDescription.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "mc/deps/renderer/ShaderColor.h"

namespace lholo::projection::detail {
namespace {

// Litematica's default schematic overlay palette; alpha comes from the
// session's fill/outline opacity settings per draw.
// KNOWN ISSUE (26.32, revisit in a later adaptation): the on-screen color of
// a batch does not match the constant below — the game tints these immediate
// draws through MeshContext::currentShaderColor, and 26.32 apparently applies
// an extra transform (linear/sRGB or material tint) that 26.20 did not, so
// hues render shifted (blue #33B3E6 shows up washed/shifted). The mechanism
// (per-batch color + restore) works; only the value mapping is off. Fix by
// calibrating these constants against observed output, or by finding the
// color-space conversion the fallback pipeline applies to CurrentColor.
struct CorrectionRgb {
    float r;
    float g;
    float b;
};

constexpr CorrectionRgb correctionRgb(CorrectionColor color) {
    switch (color) {
        case CorrectionColor::WrongType:  return {1.0f, 0.2f, 0.2f};       // #FF3333
        case CorrectionColor::WrongState: return {1.0f, 0.5647f, 0.0627f}; // #FF9010
        case CorrectionColor::Extra:      return {1.0f, 0.298f, 0.902f};   // #FF4CE6
        case CorrectionColor::Missing:
        default:                          return {0.2f, 0.702f, 0.902f};   // #33B3E6
    }
}

// Same "no active capture" state the other mesh submissions pass: the SDK's
// OffscreenCaptureDescription is an empty shell but the game object is 48
// bytes stored inline in RenderMetadata (see DEVELOPMENT.md section 11.2).
auto const& emptyOffscreenCaptureDescription() {
    static std::array<std::byte, 48> const empty{};
    return reinterpret_cast<OffscreenCaptureDescription const&>(empty);
}

bool materialExists(mce::MaterialPtr const& material) {
    return material.mRenderMaterialInfoPtr.get() != nullptr;
}

// Restores the client's shared shader color when the pass ends. Every
// immediate draw colors through this slot; leaving a correction color in it
// would tint whatever the game draws next.
class ScopedShaderColor {
public:
    explicit ScopedShaderColor(ShaderColor& shaderColor) : mShaderColor(shaderColor) {
        mSaved = shaderColor;
    }
    ~ScopedShaderColor() { mShaderColor = mSaved; }
    ScopedShaderColor(ScopedShaderColor const&)            = delete;
    ScopedShaderColor& operator=(ScopedShaderColor const&) = delete;

private:
    ShaderColor& mShaderColor;
    ShaderColor  mSaved{};
};

} // namespace

void submitCorrectionOverlayPass(
    ProjectionState&        state,
    BaseActorRenderContext& renderContext,
    IClientInstance&        client
) {
    auto* levelRenderer = client.getLevelRenderer();
    if (!levelRenderer || !levelRenderer->mLevelRendererPlayer) return;
    auto const& outlineMaterial
        = levelRenderer->mLevelRendererPlayer->mOutlineSelectionMaterial.get();
    // The block-entity selection overlay material keeps the translucent
    // white-texture style the fills used on 26.20; the outline material draws
    // the wireframes. Both are colored through the shared shader color, the
    // same mechanism the vanilla block outline uses.
    auto const& fillMaterial
        = levelRenderer->mLevelRendererPlayer->selectionBlockEntityOverlayColorMaterial.get();
    if (!materialExists(outlineMaterial) || !materialExists(fillMaterial)) return;

    auto const hasAnyMesh = [](std::vector<std::unique_ptr<mce::Mesh>> const& meshes) {
        return std::any_of(meshes.begin(), meshes.end(), [](auto const& mesh) {
            return mesh && mesh->isValid();
        });
    };
    auto drawBatch = [&](std::vector<std::unique_ptr<mce::Mesh>> const& meshes,
                         mce::MaterialPtr const&                        material,
                         mce::Color const&                              color) {
        if (!materialExists(material)) return;
        // MeshContext::currentShaderColor is stored as a plain reference and
        // ShaderColor::dirty as a plain bool (TypedStorage reference/scalar
        // specializations), so both are written directly.
        auto& shaderColor       = renderContext.mScreenContext.currentShaderColor;
        shaderColor.color.get() = color;
        shaderColor.dirty       = true;
        for (auto const& mesh : meshes) {
            if (!mesh || !mesh->isValid()) continue;
            mesh->renderMesh(
                renderContext.mScreenContext,
                material,
                // Both materials bind their own defaults (the fill material's
                // white texture); no override is passed.
                std::variant<
                    std::monostate,
                    mce::TexturePtr,
                    mce::ClientTexture,
                    mce::ServerTexture>{},
                0,
                mesh->mVertexCount.get().value_or(0u),
                emptyOffscreenCaptureDescription(),
                nullptr
            );
        }
    };

    ScopedShaderColor const colorGuard(renderContext.mScreenContext.currentShaderColor);
    auto constexpr colorCount = static_cast<std::size_t>(CorrectionColor::Count);
    for (std::size_t color = 0; color < colorCount; ++color) {
        auto const rgb = correctionRgb(static_cast<CorrectionColor>(color));
        if (hasAnyMesh(state.correctionFillSectionMeshes[color])) {
            drawBatch(
                state.correctionFillSectionMeshes[color],
                fillMaterial,
                mce::Color{rgb.r, rgb.g, rgb.b, state.cachedCorrectionFillOpacity}
            );
        }
        if (hasAnyMesh(state.correctionOutlineSectionMeshes[color])) {
            drawBatch(
                state.correctionOutlineSectionMeshes[color],
                outlineMaterial,
                mce::Color{rgb.r, rgb.g, rgb.b, state.cachedCorrectionOutlineOpacity}
            );
        }
    }
}

} // namespace lholo::projection::detail
