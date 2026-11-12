// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/hooks/ProjectionRenderHooks.h"

#include "projection/runtime/ProjectionRenderFrame.h"

#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

#include "ll/api/memory/Hook.h"

namespace lholo::projection::detail {
namespace {

LL_TYPE_INSTANCE_HOOK(
    LevelRendererPlayerRenderHitSelectHook,
    ll::memory::HookPriority::Normal,
    LevelRendererPlayer,
    &LevelRendererPlayer::renderHitSelect,
    void,
    BaseActorRenderContext& renderContext,
    BlockSource&            region,
    BlockPos const&         pos,
    bool                    fancyGraphics
) {
    if (shouldSuppressProjectionHitSelect(pos)) return;
    origin(renderContext, region, pos, fancyGraphics);
}

LL_TYPE_INSTANCE_HOOK(
    LevelRendererPlayerRenderBlockEntitiesHook,
    ll::memory::HookPriority::Normal,
    LevelRendererPlayer,
    &LevelRendererPlayer::$renderBlockEntities,
    void,
    BaseActorRenderContext& renderContext,
    bool                      renderAlphaLayer
) {
    origin(renderContext, renderAlphaLayer);
    renderProjectionFrame(renderContext, renderAlphaLayer);
}

} // namespace

bool installProjectionRenderHooks() {
    if (LevelRendererPlayerRenderHitSelectHook::hook() < 0) return false;
    if (LevelRendererPlayerRenderBlockEntitiesHook::hook() < 0) {
        LevelRendererPlayerRenderHitSelectHook::unhook();
        return false;
    }
    return true;
}

void uninstallProjectionRenderHooks() {
    LevelRendererPlayerRenderBlockEntitiesHook::unhook();
    LevelRendererPlayerRenderHitSelectHook::unhook();
}

} // namespace lholo::projection::detail
