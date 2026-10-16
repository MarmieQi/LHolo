// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/hooks/ProjectionGameHooks.h"

#include "projection/world/ProjectionVirtualWorld.h"
#include "projection/core/ProjectionTintRules.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <variant>

#include "mc/client/world/level/biome/biome_color_sampling/GrassTessellationPolicy.h"
#include "mc/deps/core/math/Color.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/biome/biome_color_sampling/BiomeColorSampling.h"
#include "mc/world/level/biome/biome_color_sampling/MapPolicy.h"
#include "mc/world/level/ActorBlockSyncMessage.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/material/Material.h"
#include "mc/world/level/block/actor/BlockActor.h"

#include "ll/api/memory/Hook.h"

namespace lholo::projection::detail {
namespace {

LL_TYPE_INSTANCE_HOOK(
    ProjectionGrassTintHook,
    ll::memory::HookPriority::Normal,
    BiomeColorSampling::GrassTessellationPolicy,
    static_cast<mce::Color (BiomeColorSampling::GrassTessellationPolicy::*)(
        Block const&, BlockSource&, BlockPos const&, BiomeTintCache const*) const>(
            &BiomeColorSampling::GrassTessellationPolicy::$get),
    mce::Color,
    Block const& block,
    BlockSource& region,
    BlockPos const& position,
    BiomeTintCache const* cache
) {
    if (!findTessellationBlock(position)) return origin(block, region, position, cache);
    // MapPolicy explicitly samples a region without a BiomeTintCache. Passing
    // null to the tessellation policy was not a verified cache-free contract.
    // Vanilla still selects the grass-tinted faces; do not recolor dirt/snow.
    auto const sample = BiomeColorSampling::getMapPolicy(TintMethod::Grass).get(region, position);
    ProjectionTintRgb const rgb{sample.r, sample.g, sample.b};
    if (validProjectionBiomeTint(rgb)) return mce::Color{sample.r, sample.g, sample.b, 1.0f};
    markTessellationBiomeSampleFailed();
    auto const& fallback = Biome::DEFAULT_GRASS_TINT();
    auto const selected = projectionFallbackBiomeTint({fallback.r, fallback.g, fallback.b});
    return mce::Color{selected[0], selected[1], selected[2], 1.0f};
}

LL_TYPE_INSTANCE_HOOK(
    BlockSourceGetBlockHook,
    ll::memory::HookPriority::Normal,
    BlockSource,
    static_cast<Block const& (BlockSource::*)(BlockPos const&) const>(&BlockSource::$getBlock),
    Block const&,
    BlockPos const& position
) {
    if (auto const* block = findTessellationBlock(position)) return *block;
    return origin(position);
}

LL_TYPE_INSTANCE_HOOK(
    BlockSourceGetBlockLayerHook,
    ll::memory::HookPriority::Normal,
    BlockSource,
    static_cast<Block const& (BlockSource::*)(BlockPos const&, uint) const>(&BlockSource::$getBlock),
    Block const&,
    BlockPos const& position,
    uint layer
) {
    if (layer == 0) {
        if (auto const* block = findTessellationBlock(position)) return *block;
    }
    return origin(position, layer);
}

LL_TYPE_INSTANCE_HOOK(
    BlockSourceGetBlockEntityHook,
    ll::memory::HookPriority::Normal,
    BlockSource,
    static_cast<BlockActor const* (BlockSource::*)(BlockPos const&) const>(&BlockSource::$getBlockEntity),
    BlockActor const*,
    BlockPos const& position
) {
    if (auto const* actor = findTessellationBlockActor(position)) return actor;
    return origin(position);
}

// Material and solidity queries read the chunk storage directly instead of
// going through getBlock, so during projection tessellation they still saw the
// real (unbuilt) world. Redstone dust uses them to decide whether it climbs the
// side of a neighbour, so wire running up projected blocks lost its vertical
// side lines. Answer them from the projected block while tessellating.
bool projectedSolidBlocking(Block const& block) {
    auto const& material = block.getMaterial();
    return material.mSolid && material.mBlocksMotion;
}

LL_TYPE_INSTANCE_HOOK(
    BlockSourceGetMaterialHook,
    ll::memory::HookPriority::Normal,
    BlockSource,
    static_cast<Material const& (BlockSource::*)(BlockPos const&) const>(&BlockSource::$getMaterial),
    Material const&,
    BlockPos const& position
) {
    if (auto const* block = findTessellationBlock(position)) return block->getMaterial();
    return origin(position);
}

LL_TYPE_INSTANCE_HOOK(
    BlockSourceGetMaterialXyzHook,
    ll::memory::HookPriority::Normal,
    BlockSource,
    static_cast<Material const& (BlockSource::*)(int, int, int) const>(&BlockSource::$getMaterial),
    Material const&,
    int x,
    int y,
    int z
) {
    if (auto const* block = findTessellationBlock(BlockPos{x, y, z})) return block->getMaterial();
    return origin(x, y, z);
}

LL_TYPE_INSTANCE_HOOK(
    BlockSourceIsSolidBlockingHook,
    ll::memory::HookPriority::Normal,
    BlockSource,
    static_cast<bool (BlockSource::*)(BlockPos const&) const>(&BlockSource::$isSolidBlockingBlock),
    bool,
    BlockPos const& position
) {
    if (auto const* block = findTessellationBlock(position)) return projectedSolidBlocking(*block);
    return origin(position);
}

LL_TYPE_INSTANCE_HOOK(
    BlockSourceIsSolidBlockingXyzHook,
    ll::memory::HookPriority::Normal,
    BlockSource,
    static_cast<bool (BlockSource::*)(int, int, int) const>(&BlockSource::$isSolidBlockingBlock),
    bool,
    int x,
    int y,
    int z
) {
    if (auto const* block = findTessellationBlock(BlockPos{x, y, z})) return projectedSolidBlocking(*block);
    return origin(x, y, z);
}

// BlockType::connectionUpdate recomputes flattened connection geometry
// correctly for every block family, but it also writes the recomputed block
// into the region it is handed. While a ScopedRegionWriteSuppression is
// active on this thread the write is swallowed: the caller keeps only the
// returned block, and the real world never sees the projected blocks.
LL_TYPE_INSTANCE_HOOK(
    BlockSourceSetBlockHook,
    ll::memory::HookPriority::Normal,
    BlockSource,
    &BlockSource::$setBlock,
    bool,
    BlockPos const&                 position,
    Block const&                    block,
    int                             updateFlags,
    ActorBlockSyncMessage const*    syncMsg,
    BlockChangeContext const&       changeSourceContext
) {
    if (regionWritesSuppressed()) return true;
    return origin(position, block, updateFlags, syncMsg, changeSourceContext);
}

LL_TYPE_INSTANCE_HOOK(
    BlockSourceSetBlockWithActorHook,
    ll::memory::HookPriority::Normal,
    BlockSource,
    static_cast<
        bool (BlockSource::*)(
            BlockPos const&, Block const&, int, std::shared_ptr<BlockActor>,
            ActorBlockSyncMessage const*, BlockChangeContext const&
        )>(&BlockSource::setBlock),
    bool,
    BlockPos const&                 position,
    Block const&                    block,
    int                             updateFlags,
    std::shared_ptr<BlockActor>     blockEntity,
    ActorBlockSyncMessage const*    syncMsg,
    BlockChangeContext const&       changeSourceContext
) {
    if (regionWritesSuppressed()) return true;
    return origin(position, block, updateFlags, blockEntity, syncMsg, changeSourceContext);
}

} // namespace

bool installProjectionGameHooks() {
    if (BlockSourceGetBlockHook::hook() < 0) return false;
    if (BlockSourceGetBlockLayerHook::hook() < 0) {
        BlockSourceGetBlockHook::unhook();
        return false;
    }
    if (BlockSourceGetBlockEntityHook::hook() < 0) {
        BlockSourceGetBlockLayerHook::unhook();
        BlockSourceGetBlockHook::unhook();
        return false;
    }
    if (BlockSourceSetBlockHook::hook() < 0) {
        BlockSourceGetBlockEntityHook::unhook();
        BlockSourceGetBlockLayerHook::unhook();
        BlockSourceGetBlockHook::unhook();
        return false;
    }
    if (BlockSourceSetBlockWithActorHook::hook() < 0) {
        BlockSourceSetBlockHook::unhook();
        BlockSourceGetBlockEntityHook::unhook();
        BlockSourceGetBlockLayerHook::unhook();
        BlockSourceGetBlockHook::unhook();
        return false;
    }
    if (ProjectionGrassTintHook::hook() < 0) {
        BlockSourceSetBlockWithActorHook::unhook();
        BlockSourceSetBlockHook::unhook();
        BlockSourceGetBlockEntityHook::unhook();
        BlockSourceGetBlockLayerHook::unhook();
        BlockSourceGetBlockHook::unhook();
        return false;
    }
    // Neighbour-shape accuracy only (redstone side lines); the projection still
    // works without them, so a hook that fails to install is simply skipped.
    (void)BlockSourceGetMaterialHook::hook();
    (void)BlockSourceGetMaterialXyzHook::hook();
    (void)BlockSourceIsSolidBlockingHook::hook();
    (void)BlockSourceIsSolidBlockingXyzHook::hook();
    return true;
}

void uninstallProjectionGameHooks() {
    BlockSourceIsSolidBlockingXyzHook::unhook();
    BlockSourceIsSolidBlockingHook::unhook();
    BlockSourceGetMaterialXyzHook::unhook();
    BlockSourceGetMaterialHook::unhook();
    ProjectionGrassTintHook::unhook();
    BlockSourceSetBlockWithActorHook::unhook();
    BlockSourceSetBlockHook::unhook();
    BlockSourceGetBlockEntityHook::unhook();
    BlockSourceGetBlockLayerHook::unhook();
    BlockSourceGetBlockHook::unhook();
}

} // namespace lholo::projection::detail
