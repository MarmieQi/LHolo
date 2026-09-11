#include "overlay/BoundsWireframe.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/SupplementaryFieldAutoGenerationMode.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/deps/minecraft_renderer/framebuilder/dragon/RenderMetadata.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"
#include "mc/deps/renderer/Camera.h"

namespace lholo::overlay {

namespace {

Vec3 renderCameraPosition(BaseActorRenderContext const& renderContext) {
    // Same source as the projection pass: see renderCameraPosition() in
    // ProjectionRenderFrame.cpp for why this reads through Impl on 1.26.40.
    auto const* impl = reinterpret_cast<float const*>(renderContext.mImpl.get());
    if (!impl) return {};
    return {impl[10], impl[11], impl[12]};
}

OffscreenCaptureDescription const& emptyOffscreenCaptureDescription() {
    using Storage = decltype(dragon::RenderMetadata::mOffscreenCaptureDescription);
    static Storage empty{};
    return empty.get();
}

void setColorAbgr(Tessellator& tessellator, std::uint32_t colorAbgr) {
    tessellator.color(
        static_cast<float>((colorAbgr >> 0) & 0xFF) / 255.0f,
        static_cast<float>((colorAbgr >> 8) & 0xFF) / 255.0f,
        static_cast<float>((colorAbgr >> 16) & 0xFF) / 255.0f,
        static_cast<float>((colorAbgr >> 24) & 0xFF) / 255.0f
    );
}

} // namespace

BoundsWireframe::~BoundsWireframe() = default;

void BoundsWireframe::setBounds(BlockPos const& min, BlockPos const& max, std::uint32_t color) {
    if (mHasBounds && mMin == min && mMax == max && mColor == color) return;
    mMin = min;
    mMax = max;
    mColor = color;
    mHasBounds = true;
    mMesh.reset();
}

void BoundsWireframe::clear() {
    mHasBounds = false;
    mMesh.reset();
}

void BoundsWireframe::render(BaseActorRenderContext& renderContext, bool renderAlphaLayer) {
    if (!mHasBounds) return;

    if (!renderAlphaLayer && !mMesh) {
        constexpr float expansion = 0.01f;
        float const x0 = -expansion;
        float const y0 = -expansion;
        float const z0 = -expansion;
        float const x1 = static_cast<float>(mMax.x - mMin.x + 1) + expansion;
        float const y1 = static_cast<float>(mMax.y - mMin.y + 1) + expansion;
        float const z1 = static_cast<float>(mMax.z - mMin.z + 1) + expansion;

        auto& tessellator = renderContext.mScreenContext.tessellator;
        tessellator.begin(
            Tessellator::DebugContextCallback{},
            mce::PrimitiveMode::LineList,
            24,
            false
        );
        setColorAbgr(tessellator, mColor);
        auto const addEdge = [&](Vec3 const& a, Vec3 const& b) {
            tessellator.vertex(a.x, a.y, a.z);
            tessellator.vertex(b.x, b.y, b.z);
        };
        addEdge({x0,y0,z0},{x1,y0,z0}); addEdge({x1,y0,z0},{x1,y1,z0});
        addEdge({x1,y1,z0},{x0,y1,z0}); addEdge({x0,y1,z0},{x0,y0,z0});
        addEdge({x0,y0,z1},{x1,y0,z1}); addEdge({x1,y0,z1},{x1,y1,z1});
        addEdge({x1,y1,z1},{x0,y1,z1}); addEdge({x0,y1,z1},{x0,y0,z1});
        addEdge({x0,y0,z0},{x0,y0,z1}); addEdge({x1,y0,z0},{x1,y0,z1});
        addEdge({x1,y1,z0},{x1,y1,z1}); addEdge({x0,y1,z0},{x0,y1,z1});
        mMesh = std::make_unique<mce::Mesh>(tessellator.end(
            Tessellator::UploadMode::Buffered,
            "LHoloSelectionBounds",
            SupplementaryFieldAutoGenerationMode{0}
        ));
        return;
    }

    if (!renderAlphaLayer || !mMesh || !mMesh->isValid()) return;
    auto& client = renderContext.mClientInstance;
    auto* levelRenderer = client.getLevelRenderer();
    if (!levelRenderer) return;
    auto const& material = levelRenderer->mLevelRendererPlayer->mOutlineSelectionMaterial.get();
    if (material.mRenderMaterialInfoPtr.get() == nullptr) return;

    Vec3 const camera = renderCameraPosition(renderContext);
    auto matrix = renderContext.mScreenContext.camera.worldMatrixStack.get().push(false);
    matrix.mat->translate(
        static_cast<float>(mMin.x) - camera.x,
        static_cast<float>(mMin.y) - camera.y,
        static_cast<float>(mMin.z) - camera.z
    );
    mMesh->renderMesh(
        renderContext.mScreenContext,
        material,
        std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture>{},
        0,
        mMesh->mVertexCount.get().value_or(0u),
        emptyOffscreenCaptureDescription(),
        nullptr
    );
}

} // namespace lholo::overlay
