#include "ui/preview/StructurePreviewRenderer.h"
#include "ui/preview/StructurePreviewTransform.h"
#include "ui/preview/StructurePreviewStorage.h"
#include "render/OverlayMaterials.h"
#include "render/ScopedShaderColor.h"
#include "structure/StructureLoader.h"
#include "structure/StructurePaths.h"
#include "plugin/LHolo.h"
#include "ll/api/mod/NativeMod.h"
#include "i18n/Translator.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/UIPropertyBag.h"
#include "mc/client/gui/controls/renderers/StructureVolumeRenderer.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/SupplementaryFieldAutoGenerationMode.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/game/GameRenderer.h"
#include "mc/deps/minecraft_renderer/framebuilder/dragon/RenderMetadata.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/client/renderer/block/tessellation_pipeline/client_block_pipeline/TessellatorContext.h"
#include "mc/deps/minecraft_renderer/resources/OffscreenCaptureDescription.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/ILevel.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/ChunkBlockPos.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/chunk/ChunkSource.h"
#include "mc/world/level/chunk/NetworkChunkSource.h"
#include "mc/world/level/chunk/ChunkState.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/chunk/SubChunkInitMode.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/VanillaBlockActorFactory.h"
#include "mc/world/level/block/actor/component/IVanillaRenderBlockActorComponent.h"
#include "mc/dataloadhelper/NewUniqueIdsDataLoadHelper.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace lholo::ui::preview {
namespace {
thread_local StructureVolumeRenderer* activeRenderer{};
thread_local BlockSource* activeSource{};
thread_local ScreenContext* activeScreen{};
thread_local GameRenderer* activeGameRenderer{};
thread_local glm::vec2 activeCenter{};
thread_local float activeZoom{1};
thread_local bool zoomApplied{};
thread_local char const** activeStage{};
thread_local mce::Mesh const* activeBoundsMesh{};

OffscreenCaptureDescription const& emptyOffscreenCaptureDescription() {
    using Storage = decltype(dragon::RenderMetadata::mOffscreenCaptureDescription);
    static Storage empty{};
    return empty.get();
}

std::unique_ptr<mce::Mesh> buildPreviewBoundsMesh(Tessellator& tessellator, Vec3 const& extent) {
    float const diag = std::sqrt(extent.x * extent.x + extent.y * extent.y + extent.z * extent.z);
    float const h = std::max(0.008f, diag * 0.0009f);
    float const x0 = -h, y0 = -h, z0 = -h;
    float const x1 = extent.x + h, y1 = extent.y + h, z1 = extent.z + h;
    // 12 edges x 6 faces x 2 triangles x 3 vertices = 432 vertices.
    tessellator.begin(Tessellator::DebugContextCallback{}, mce::PrimitiveMode::TriangleList, 432, false);
    auto v = [&](float x, float y, float z) {
        tessellator.tex2({0.5f, 0.5f});
        tessellator.vertex(x, y, z);
    };
    auto quad = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
        v(a.x, a.y, a.z); v(b.x, b.y, b.z); v(c.x, c.y, c.z);
        v(a.x, a.y, a.z); v(c.x, c.y, c.z); v(d.x, d.y, d.z);
    };
    auto bar = [&](Vec3 a, Vec3 b, float r, float g, float bl) {
        tessellator.color(r, g, bl, 1.f);
        float const bx0 = std::min(a.x, b.x) - h, bx1 = std::max(a.x, b.x) + h;
        float const by0 = std::min(a.y, b.y) - h, by1 = std::max(a.y, b.y) + h;
        float const bz0 = std::min(a.z, b.z) - h, bz1 = std::max(a.z, b.z) + h;
        quad({bx0, by0, bz0}, {bx0, by1, bz0}, {bx1, by1, bz0}, {bx1, by0, bz0}); // -Z
        quad({bx0, by0, bz1}, {bx1, by0, bz1}, {bx1, by1, bz1}, {bx0, by1, bz1}); // +Z
        quad({bx0, by0, bz0}, {bx0, by0, bz1}, {bx0, by1, bz1}, {bx0, by1, bz0}); // -X
        quad({bx1, by0, bz0}, {bx1, by1, bz0}, {bx1, by1, bz1}, {bx1, by0, bz1}); // +X
        quad({bx0, by0, bz0}, {bx1, by0, bz0}, {bx1, by0, bz1}, {bx0, by0, bz1}); // -Y
        quad({bx0, by1, bz0}, {bx0, by1, bz1}, {bx1, by1, bz1}, {bx1, by1, bz0}); // +Y
    };
    // Bars 0: X axis (red, 0..35), 1: Y axis (green, 36..71), 2: Z axis (blue, 72..107), 3..11: white (108..431).
    bar({x0, y0, z0}, {x1, y0, z0}, 1.f, 0.f, 0.f);
    bar({x0, y0, z0}, {x0, y1, z0}, 0.f, 1.f, 0.f);
    bar({x0, y0, z0}, {x0, y0, z1}, 0.f, 0.7f, 1.f);
    bar({x1, y0, z0}, {x1, y1, z0}, 1.f, 1.f, 1.f);
    bar({x1, y1, z0}, {x0, y1, z0}, 1.f, 1.f, 1.f);
    bar({x0, y0, z1}, {x1, y0, z1}, 1.f, 1.f, 1.f);
    bar({x1, y0, z1}, {x1, y1, z1}, 1.f, 1.f, 1.f);
    bar({x1, y1, z1}, {x0, y1, z1}, 1.f, 1.f, 1.f);
    bar({x0, y1, z1}, {x0, y0, z1}, 1.f, 1.f, 1.f);
    bar({x1, y0, z0}, {x1, y0, z1}, 1.f, 1.f, 1.f);
    bar({x1, y1, z0}, {x1, y1, z1}, 1.f, 1.f, 1.f);
    bar({x0, y1, z0}, {x0, y1, z1}, 1.f, 1.f, 1.f);
    return std::make_unique<mce::Mesh>(tessellator.end(
        Tessellator::UploadMode::Buffered, "LHoloPreviewBounds", SupplementaryFieldAutoGenerationMode{0}));
}
// StructureVolumeRenderer copies its fitted world matrix to GameRenderer
// before dispatching geometry. Keep that render-graph snapshot and the live
// camera in sync; changing the projection stack alone is not consumed here.
class GeometryZoom {
    std::unique_ptr<MatrixStack::MatrixStackRef> world;
    GameRenderer* gameRenderer{};
    glm::mat4 previousWorld{1};
public:
    explicit GeometryZoom(ScreenContext* screen) {
        if (!screen || !activeGameRenderer || zoomApplied || activeZoom == 1) return;
        auto& stack = screen->camera.worldMatrixStack.get();
        world.reset(new MatrixStack::MatrixStackRef(stack.push(false)));
        stack.getTop()._m = zoomPreviewTransform(stack.top()._m.get(), activeCenter, activeZoom);
        gameRenderer = activeGameRenderer;
        previousWorld = gameRenderer->mLastLevelWorldMatrix->_m.get();
        gameRenderer->mLastLevelWorldMatrix->_m = zoomPreviewTransform(previousWorld,activeCenter,activeZoom);
        zoomApplied = true;
    }
    ~GeometryZoom() {
        if (gameRenderer) {
            gameRenderer->mLastLevelWorldMatrix->_m = previousWorld;
            zoomApplied = false;
        }
    }
};
bool blocksInstalled{}, blockActorsInstalled{}, sourceInstalled{};
// The native wrapper re-fetches Dimension's main source before calling its
// opaque _render entry. Redirect that acquisition for this invocation only,
// so callbacks and neighbour queries share the same private source as meshes.
LL_TYPE_INSTANCE_HOOK(PreviewNativeSource, ll::memory::HookPriority::Normal,
    Dimension, &Dimension::$getBlockSourceFromMainChunkSource, BlockSource&) {
    if (activeRenderer && activeSource && &activeSource->getDimension() == this) {
        return *activeSource;
    }
    return origin();
}
// Use vanilla's public render wrapper so its opaque capture-request argument
// remains game-owned. The SDK's empty capture-request declaration is not ABI
// sufficient for a direct call to _render, even when passing std::nullopt.
LL_TYPE_INSTANCE_HOOK(PreviewBlocks, ll::memory::HookPriority::Normal,
    StructureVolumeRenderer, &StructureVolumeRenderer::_renderBlocks, bool,
    BaseActorRenderContext& context, BlockSource& region, AABB const& area) {
    if (this != activeRenderer || !activeSource) return origin(context, region, area);
    if (activeStage) *activeStage = "native-block-meshes";
    GeometryZoom zoom{&context.mScreenContext};
    auto const result = origin(context, *activeSource, area);
    if (activeBoundsMesh && activeBoundsMesh->isValid()) {
        auto const& material = this->mAxesMaterial.get();
        if (render::materialExists(material)) {
            Vec3 const extent = area.max - area.min;
            Vec3 const half = extent * 0.5f;
            auto const& top = context.mScreenContext.camera.worldMatrixStack.get().top()._m.get();
            auto distToCenter = [&](Vec3 p) {
                glm::vec4 const projected = top * glm::vec4{p.x, p.y, p.z, 1.f};
                glm::vec2 const delta{projected.x - activeCenter.x, projected.y - activeCenter.y};
                return glm::dot(delta, delta);
            };
            // Match whichever origin StructureVolumeRenderer used for the fitted matrix
            // (world coordinates, area-local [0..extent], or centered [-half..+half]).
            Vec3 const offset = [&] {
                float const dWorld = distToCenter(area.min + half);
                float const dLocal = distToCenter(half);
                float const dCentered = distToCenter(Vec3{0.f, 0.f, 0.f});
                if (dWorld <= dLocal && dWorld <= dCentered) return area.min;
                if (dLocal <= dCentered) return Vec3{0.f, 0.f, 0.f};
                return Vec3{-half.x, -half.y, -half.z};
            }();
            auto* levelRenderer = context.mClientInstance.getLevelRenderer();
            auto const texture = levelRenderer
                ? render::resolveWhiteTextureVariant(*levelRenderer)
                : render::TextureVariant{};
            auto matrix = context.mScreenContext.camera.worldMatrixStack.get().push(false);
            matrix.mat->translate(offset.x, offset.y, offset.z);
            // Match _renderBlocks's viewport clipping: RenderDragon's 3D chunk
            // shaders ignore UI normalizedClipRegion, whereas mAxesMaterial obeys it.
            struct UnclippedBoundsScope {
                ScreenContext& screen;
                std::optional<glm::vec4> saved;
                ~UnclippedBoundsScope() { screen.normalizedClipRegion = saved; }
            } unclipped{context.mScreenContext, context.mScreenContext.normalizedClipRegion.get()};
            context.mScreenContext.normalizedClipRegion = std::nullopt;
            auto drawSegment = [&](uint startOffset, uint count, mce::Color rgba) {
                render::ScopedShaderColor color{context.mScreenContext.currentShaderColor, rgba};
                activeBoundsMesh->renderMesh(
                    context.mScreenContext,
                    material,
                    texture,
                    startOffset,
                    count,
                    emptyOffscreenCaptureDescription(),
                    nullptr
                );
            };
            drawSegment(0, 36, mce::Color{1.f, 0.f, 0.f, 1.f});    // X axis (red)
            drawSegment(36, 36, mce::Color{0.f, 1.f, 0.f, 1.f});   // Y axis (green)
            drawSegment(72, 36, mce::Color{0.f, 0.7f, 1.f, 1.f});  // Z axis (blue)
            drawSegment(108, 324, mce::Color{1.f, 1.f, 1.f, 1.f}); // Remaining 9 white edges
        }
    }
    if (activeStage) *activeStage = "native-render";
    return result;
}
LL_STATIC_HOOK(PreviewBlockActors, ll::memory::HookPriority::Normal,
    &StructureVolumeRenderer::_iterateOverBlockEntities, void, BlockSource& region, AABB const& area,
    std::function<void(IVanillaRenderBlockActorComponent&)> const& callback) {
    if (!activeRenderer || !activeSource) { origin(region, area, callback); return; }
    if (activeStage) *activeStage = "native-block-entities";
    GeometryZoom zoom{activeScreen};
    origin(*activeSource, area, callback);
    if (activeStage) *activeStage = "native-render";
}
// No parent: all lookups terminate in this private collection, including the
// neighbour halo requested by RenderChunkBuilder. Never enters a world source.
class PreviewChunks final : public NetworkChunkSource {
public:
    PreviewChunks(BlockSource& world, structure::LoadedStructure const& file)
    // ChunkSource(unique_ptr) is a decorating-source constructor: it requires
    // a live parent and dereferences it before returning. NetworkChunkSource's
    // Dimension constructor is the exported native root-source initializer.
    : NetworkChunkSource(world.getDimension()), mWorld(world),
      mWidth(file.sizeX), mDepth(file.sizeZ) {}
    std::shared_ptr<LevelChunk> getExistingChunk(ChunkPos const& p) override { return chunk(p); }
    std::shared_ptr<LevelChunk> getOrLoadChunk(ChunkPos const& p, LoadMode, bool) override { return chunk(p); }
    std::shared_ptr<LevelChunk> createNewChunk(ChunkPos const& p, LoadMode, bool) override { return chunk(p); }
    bool isChunkKnown(ChunkPos const& p) override { return inBounds(p); }
    bool isChunkSaved(ChunkPos const&) override { return false; }
    bool saveLiveChunk(LevelChunk&) override { return false; }
    bool canCreateViews() const override { return true; }
    bool canLaunchTasks() const override { return false; }
    std::unordered_map<ChunkPos, std::weak_ptr<LevelChunk>> const& getStorage() const override {
        return mChunkMap.get();
    }
    std::unordered_map<ChunkPos, std::weak_ptr<LevelChunk>> const* getChunkMap() const override {
        return &mChunkMap.get();
    }
    std::shared_ptr<LevelChunk> chunk(ChunkPos const& p) {
        if (!inBounds(p)) return {};
        auto& result = mChunks[{p.x, p.z}];
        if (!result) {
            result = std::make_shared<LevelChunk>(mWorld.getDimension(), p, false,
                SubChunkInitMode::All, false, LevelChunkBlockActorStorage::TrackingMode::PermanentlyRendered);
            result->mLoadState->store(ChunkState::Loaded);
            mChunkMap.get()[p] = result;
        }
        return result;
    }
private:
    bool inBounds(ChunkPos const& p) const {
        return p.x >= -1 && p.z >= -1 && p.x <= (mWidth - 1) / 16 + 1 && p.z <= (mDepth - 1) / 16 + 1;
    }
    BlockSource& mWorld;
    int mWidth, mDepth;
    std::map<std::pair<int, int>, std::shared_ptr<LevelChunk>> mChunks;
};
}

struct StructurePreviewRenderer::Impl {
    std::shared_ptr<structure::LoadedStructure const> file;
    std::unique_ptr<PreviewChunks> chunks;
    std::unique_ptr<BlockSource> region;
    std::unique_ptr<Tessellator> tessellator;
    std::unique_ptr<ClientBlockPipeline::TessellatorContext> pipeline;
    // Delete through the native virtual base: the generated derived header's
    // default destructor omits source-listener/garbage cleanup done by the game.
    std::unique_ptr<UICustomRenderer> rendererOwner; // destroyed before source/chunks
    std::unique_ptr<mce::Mesh> boundsMesh;
    StructureVolumeRenderer* renderer{};
    std::string error;
    std::size_t cursor{};
    int baseY{};
    bool initialized{};
    char const* stage{"start"};
    std::string currentBlock;
    BlockPos currentPosition{};
};
StructurePreviewRenderer::StructurePreviewRenderer() : mImpl(std::make_unique<Impl>()) {}
StructurePreviewRenderer::~StructurePreviewRenderer() = default;
void StructurePreviewRenderer::setStructure(std::shared_ptr<structure::LoadedStructure const> file) {
    mImpl = std::make_unique<Impl>();
    mImpl->file = std::move(file);
}
std::string const& StructurePreviewRenderer::error() const { return mImpl->error; }
bool StructurePreviewRenderer::ready() const { return mImpl->initialized; }

void StructurePreviewRenderer::render(MinecraftUIRenderContext& context, ClientInstance& client,
    UIControl& owner, float yaw, float pitch, float zoom) {
    auto& s = *mImpl;
    if (!s.file || !s.error.empty()) return;
    auto* player = client.getLocalPlayer();
    if (!player) return;
    try {
        auto const& f = *s.file;
        if (!s.region) {
            s.stage = "create-private-source";
            auto& world = player->getDimensionBlockSource();
            s.baseY = world.getMinHeight();
            if (f.sizeY > world.getMaxHeight() - s.baseY)
                throw std::runtime_error(i18n::tr(i18n::TextKey::PreviewHeightLimit));
            s.chunks = std::make_unique<PreviewChunks>(world, f);
            s.region = std::make_unique<BlockSource>(world.getLevel(), world.getDimension(), *s.chunks,
                false, true, false, false);
        }
        // Populate only private chunks, bounded across render frames. Simple
        // setters do not notify the world or run gameplay placement/ticking.
        auto const end = std::min(f.renderBlocks.size(), s.cursor + 8192);
        for (; s.cursor < end; ++s.cursor) {
            auto const& entry = f.renderBlocks[s.cursor];
            s.stage = "write-private-cell";
            s.currentBlock = entry.block ? entry.block->getTypeName()
                : entry.liquid ? entry.liquid->getTypeName() : "air";
            BlockPos const pos{entry.x, entry.y + s.baseY, entry.z};
            s.currentPosition = pos;
            auto chunk = s.chunks->chunk(ChunkPos{pos});
            if (!chunk) continue;
            ChunkBlockPos const local{pos, static_cast<short>(s.baseY)};
            auto const layers = previewStorageLayers(entry.block,entry.liquid);
            if (layers.primary) chunk->setBlockSimple(local, *layers.primary);
            if (layers.extra) s.region->setExtraBlockSimple(pos, *layers.extra);
            if (entry.block && entry.block->getBlockType().getBlockEntityType() != BlockActorType::Undefined) {
                s.stage = "create-block-entity";
                auto actor = VanillaBlockActorFactory::createBlockActor(pos, entry.block->getBlockType());
                if (actor) {
                    if (entry.blockEntityNbt) {
                        s.stage = "load-block-entity-nbt";
                        NewUniqueIdsDataLoadHelper helper;
                        helper.mLevel = &s.region->getLevel();
                        actor->load(s.region->getLevel(), *entry.blockEntityNbt, helper);
                        actor->mPosition = pos;
                    }
                    s.stage = "store-block-entity";
                    chunk->mBlockEntities->emplace(local, std::move(actor));
                }
            }
        }
        if (s.cursor != f.renderBlocks.size()) return;
        AABB const area{{0, static_cast<float>(s.baseY), 0},
            {static_cast<float>(f.sizeX), static_cast<float>(s.baseY + f.sizeY), static_cast<float>(f.sizeZ)}};
        if (!s.renderer) {
            s.currentBlock.clear();
            s.stage = "create-native-pipeline";
            s.pipeline = std::make_unique<ClientBlockPipeline::TessellatorContext>(client.getMinecraftGame_DEPRECATED());
            s.tessellator = std::make_unique<Tessellator>(context.mScreenContext.tessellator.mBufferResourceService);
            s.renderer = new StructureVolumeRenderer();
            s.rendererOwner.reset(s.renderer);
            s.stage = "initialize-native-chunk-builder";
            s.renderer->_initializeChunkBuilder(*s.pipeline, *s.region, *s.tessellator, area);
            // The wrapper keeps its normal listener lifecycle; only this
            // renderer's geometry and block-entity reads use the private source.
            s.renderer->mLastArea = area;
            s.boundsMesh = buildPreviewBoundsMesh(
                context.mScreenContext.tessellator,
                {static_cast<float>(f.sizeX), static_cast<float>(f.sizeY), static_cast<float>(f.sizeZ)}
            );
        }
        auto& bag = owner.getPropertyBag().mJsonValue.get();
        auto array = [](int x, int y, int z) {
            Json::Value value{Json::ValueType::ArrayValue};
            value.append(x); value.append(y); value.append(z); return value;
        };
        bag["#bottom_left_block"] = array(0, s.baseY, 0);
        bag["#top_right_block"] = array(f.sizeX, s.baseY + f.sizeY, f.sizeZ);
        bag["#block_position"] = array(-1, s.baseY, -1);
        bag["#include_entities"] = false;
        bag["#include_players"] = false;
        bag["#remove_blocks"] = false;
        bag["#has_focus"] = true;
        bag["#gesture_mouse_delta_x"] = 0.0f;
        bag["#gesture_mouse_delta_y"] = 0.0f;
        s.renderer->mRotation = Vec2{yaw, pitch};
        auto const size = owner.mSize.get(), position = owner.mCachedPosition.get();
        // Clip to the unscaled canvas. Zoom changes the fitted model extent,
        // never the visible window; preserve the caller's nested clip state.
        struct ClipScope {
            MinecraftUIRenderContext& context;
            std::optional<glm::vec4> clip, saved;
            ~ClipScope() {
                context.restoreSavedClippingRectangle();
                context.mScreenContext.normalizedClipRegion = clip;
                context.mSavedOriginalClippingRectangle = saved;
            }
        } clipScope{context, context.mScreenContext.normalizedClipRegion.get(),
            context.mSavedOriginalClippingRectangle.get()};
        context.saveCurrentClippingRectangle();
        context.setClippingRectangle({position.x, position.x + size.x, position.y, position.y + size.y});
        struct SourceScope {
            StructureVolumeRenderer* previousRenderer;
            BlockSource* previousSource;
            ScreenContext* previousScreen;
            GameRenderer* previousGameRenderer;
            glm::vec2 previousCenter;
            float previousZoom;
            char const** previousStage;
            mce::Mesh const* previousBoundsMesh;
            StructureVolumeRenderer& renderer;
            ~SourceScope() {
                renderer.mUnsafeSourcePointer = nullptr;
                activeRenderer = previousRenderer; activeSource = previousSource;
                activeScreen = previousScreen; activeZoom = previousZoom;
                activeGameRenderer = previousGameRenderer; activeCenter = previousCenter;
                activeStage = previousStage; activeBoundsMesh = previousBoundsMesh;
            }
        } sourceScope{activeRenderer, activeSource, activeScreen, activeGameRenderer, activeCenter, activeZoom, activeStage, activeBoundsMesh, *s.renderer};
        activeRenderer = s.renderer; activeSource = s.region.get();
        activeScreen = &context.mScreenContext; activeZoom = zoom;
        activeGameRenderer = &client.getGameRenderer(); activeCenter = position + size * 0.5f;
        activeStage = &s.stage; activeBoundsMesh = s.boundsMesh.get();
        // This renderer never subscribes to real-world block changes. The
        // preview scene owns its Level guard and the private data is immutable.
        s.renderer->mInitialize = false;
        s.stage = "native-render";
        s.renderer->$render(context, client, owner, 0);
        s.initialized = true;
        s.stage = "ready";
    } catch (std::exception const& e) {
        s.error = e.what();
        LHolo::getInstance().getSelf().getLogger().error(
            "[structure-preview] failed file={} stage={} cell={}/{} block={} pos=({},{},{}) error={}; resources retained until scene close",
            structure::detail::pathToUtf8(s.file->sourcePath),s.stage,s.cursor,s.file->renderBlocks.size(),
            s.currentBlock,s.currentPosition.x,s.currentPosition.y,s.currentPosition.z,s.error);
        LHolo::getInstance().getSelf().getLogger().flush();
        // Stop further draws, but do not destroy native geometry while the
        // current UIScene render batch may still reference it. Normal scene
        // teardown releases the renderer before its pipeline/source/chunks.
    }
}
bool installStructurePreviewRenderer() {
    sourceInstalled = PreviewNativeSource::hook() == 0;
    blocksInstalled = PreviewBlocks::hook() == 0;
    blockActorsInstalled = PreviewBlockActors::hook() == 0;
    if (sourceInstalled && blocksInstalled && blockActorsInstalled) return true;
    uninstallStructurePreviewRenderer(); return false;
}
void uninstallStructurePreviewRenderer() {
    if (blockActorsInstalled) PreviewBlockActors::unhook();
    if (blocksInstalled) PreviewBlocks::unhook();
    if (sourceInstalled) PreviewNativeSource::unhook();
    sourceInstalled = blocksInstalled = blockActorsInstalled = false;
}
}
