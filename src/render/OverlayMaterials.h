// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Shared engine resources for correction and box overlays. Resource lookup
// does not own projection/capture state or cache world-specific material handles.

#pragma once

#include <optional>
#include <utility>
#include <variant>

#include "mc/client/renderer/TextureGroup.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core_graphics/TextureSetLayerType.h"
#include "mc/deps/minecraft_renderer/renderer/BedrockTextureData.h"
#include "mc/deps/minecraft_renderer/renderer/IsMissingTexture.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"

namespace lholo::render {

using TextureVariant = std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture>;

inline bool materialExists(mce::MaterialPtr const& material) {
    return material.mRenderMaterialInfoPtr.get() != nullptr;
}

inline mce::MaterialPtr const& selectionOutlineMaterial(LevelRenderer& renderer) {
    return renderer.mLevelRendererPlayer->mOutlineSelectionMaterial.get();
}

// The vanilla 2x2 pure-white texture. Sampled at its center it reads exactly
// (1,1,1,1): the texture multiply stays neutral and alpha tests never discard.
// Resolved once from the vanilla TextureGroup, retried per frame until it
// shows up, and left empty when unavailable.
inline TextureVariant resolveWhiteTextureVariant(LevelRenderer& levelRenderer) {
    static std::optional<mce::TexturePtr> cached;
    if (!cached) {
        auto const& textureGroup = levelRenderer.mTextureGroup.get();
        if (textureGroup) {
            auto texture = textureGroup->getTexture(
                ResourceLocation{Core::PathView{"textures/ui/white_background"}},
                false,
                std::nullopt,
                cg::TextureSetLayerType::Color
            );
            auto const& clientTexture = texture.mClientTexture;
            if (clientTexture && clientTexture->mIsMissingTexture != IsMissingTexture::Yes) {
                cached.emplace(std::move(texture));
            }
        }
    }
    if (cached) return TextureVariant{*cached};
    return TextureVariant{};
}

} // namespace lholo::render
