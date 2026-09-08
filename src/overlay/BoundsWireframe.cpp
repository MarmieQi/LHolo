#include "overlay/BoundsWireframe.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/SupplementaryFieldAutoGenerationMode.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include <array>
#include <cstddef>

#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/OffscreenCaptureDescription.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"

namespace lholo::overlay {

// The SDK reconstructs OffscreenCaptureDescription as an empty struct, but
// the game object is 48 bytes (RenderMetadata stores it inline). Passing a
// one-byte temporary made the game read 47 bytes of stack garbage as its
// control block. Zeroed 48 bytes is the "no active capture" state (the old
// variant member held monostate, which is all-zero as well).
auto const& emptyOffscreenCaptureDescription() {
    static std::array<std::byte, 48> const empty{};
    return reinterpret_cast<OffscreenCaptureDescription const&>(empty);
}

// 26.32: the packed-int color setters (color(int)/colorABGR(int)) were
// inlined out. Feed the color as floats in LHolo's ABGR constant order
// (r = low byte, a = high byte); the game packs the vertex color itself.
void setColorAbgr(Tessellator& tessellator, std::uint32_t colorAbgr) {
    tessellator.color(
        static_cast<float>((colorAbgr >> 0) & 0xFF) / 255.0f,
        static_cast<float>((colorAbgr >> 8) & 0xFF) / 255.0f,
        static_cast<float>((colorAbgr >> 16) & 0xFF) / 255.0f,
        static_cast<float>((colorAbgr >> 24) & 0xFF) / 255.0f
    );
}

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

        // 26.32: the BaseActorRenderContext accessors were inlined out; the
        // same objects stay reachable through the screen context members.
        auto& tessellator = renderContext.mScreenContext.tessellator;
        tessellator.begin(
            Tessellator::DebugContextCallback{},
            mce::PrimitiveMode::LineList,
            24,
            false
        );
        // colorABGR() only seeded the pending vertex color that the next
        // vertex() consumes; the member it wrote stays public.
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
            // SupplementaryFieldAutoGenerationMode::None
            SupplementaryFieldAutoGenerationMode{0}
        ));
        return;
    }

    if (!renderAlphaLayer || !mMesh || !mMesh->isValid()) return;
    auto& client = renderContext.mClientInstance;
    auto* levelRenderer = client.getLevelRenderer();
    if (!levelRenderer) return;
    auto const& material = levelRenderer->mLevelRendererPlayer->mOutlineSelectionMaterial.get();
    // 26.32: MaterialPtr's operator bool was inlined out; the member it
    // examined stays public.
    if (material.mRenderMaterialInfoPtr.get() == nullptr) return;

    // 26.32: renderMesh reads the model matrix from the screen-context
    // camera's world stack (the MeshContext we pass below); the camera
    // position lives in BaseActorRenderContext::Impl (see ProjectionRenderFrame).
    auto const& cameraPosition = *reinterpret_cast<Vec3 const*>(
        static_cast<std::uint8_t const*>(static_cast<void const*>(
            renderContext.mImpl.get()))
        + 0x3C
    );
    auto matrix = renderContext.mScreenContext.camera.worldMatrixStack.get().push(false);
    matrix.mat->translate(
        static_cast<float>(mMin.x) - cameraPosition.x,
        static_cast<float>(mMin.y) - cameraPosition.y,
        static_cast<float>(mMin.z) - cameraPosition.z
    );
    mMesh->renderMesh(
        renderContext.mScreenContext,
        material,
        // No texture override: the old textureless overload passed monostate.
        std::variant<::std::monostate, ::mce::TexturePtr, ::mce::ClientTexture, ::mce::ServerTexture>{},
        0,
        mMesh->mVertexCount.get().value_or(0u),
        emptyOffscreenCaptureDescription(),
        nullptr
    );
}

} // namespace lholo::overlay
