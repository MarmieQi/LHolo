// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Shared lookups for overlay geometry whose color lives in vertex data: the
// glow sign text material (reads COLOR0, emissive constant color, native depth
// bias) and the vanilla 2x2 pure-white texture (neutral texture multiply plus
// a permanently-passing alpha test). Used by the projection renderer and the
// bounds wireframe.

#pragma once

#include <optional>
#include <variant>

#include "mc/client/renderer/RenderMaterialGroup.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/renderer/RenderMaterialInfo.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core_graphics/TextureSetLayerType.h"
#include "mc/deps/minecraft_renderer/renderer/BedrockTextureData.h"
#include "mc/deps/minecraft_renderer/renderer/IsMissingTexture.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/minecraft_renderer/renderer/RenderMaterial.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"

namespace lholo::render {

using TextureVariant = std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture>;

inline bool materialExists(mce::MaterialPtr const& material) {
    return material.mRenderMaterialInfoPtr.get() != nullptr;
}

// The vanilla 2x2 pure-white texture. Sampled at its center it reads exactly
// (1,1,1,1): the texture multiply stays neutral and alpha tests never discard.
// Resolved once from the vanilla TextureGroup, retried per frame until it
// shows up, and left empty when unavailable.
inline TextureVariant resolveWhiteTextureVariant(LevelRenderer* levelRenderer) {
    static mce::TexturePtr cached{};
    static bool            resolved = false;
    if (!resolved && levelRenderer) {
        auto const& textureGroup = levelRenderer->mTextureGroup.get();
        if (textureGroup) {
            auto texture = textureGroup->getTexture(
                ResourceLocation{Core::PathView{"textures/ui/white_background"}},
                false,
                std::nullopt,
                cg::TextureSetLayerType::Color
            );
            auto const& clientTexture = texture.mClientTexture;
            if (clientTexture && clientTexture->mIsMissingTexture != IsMissingTexture::Yes) {
                cached   = std::move(texture);
                resolved = true;
            }
        }
    }
    if (resolved) return TextureVariant{cached};
    return TextureVariant{};
}

// The glow sign text material reads COLOR0 and renders it as-is (emissive
// constant color, native depth bias), so overlay geometry keeps the vertex
// color it was built with instead of an engine-driven uniform color. Resolved
// once from the runtime material groups; returns null when the name does not
// exist so callers keep their previous material.
//
// The client library exports no mce::MaterialPtr constructor, so resolved
// handles live in zero-initialized aligned storage and only the internal
// shared pointer is ever assigned. The retained reference keeps the material
// alive across resource reloads.
inline mce::MaterialPtr const* resolveGlowSignMaterial() {
    alignas(mce::MaterialPtr) static std::byte storage[sizeof(mce::MaterialPtr)]{};
    static auto* const                        cached   = reinterpret_cast<mce::MaterialPtr*>(storage);
    static bool                               resolved = false;
    if (!resolved) {
        resolved = true;
        bool found = false;
        auto const scan = [&](char const*, mce::RenderMaterialGroup& group) {
            if (found) return;
            for (auto const& entry : group.mMaterials.get()) {
                auto const& info = entry.second;
                if (!info || !info->mPtr) continue;
                if (entry.first.getString() != "glow_sign_text") continue;
                cached->mRenderMaterialInfoPtr = info;
                found                          = true;
                break;
            }
        };
        scan("common", mce::RenderMaterialGroup::common());
        if (!found) scan("switchable", mce::RenderMaterialGroup::switchable());
    }
    return materialExists(*cached) ? cached : nullptr;
}

} // namespace lholo::render