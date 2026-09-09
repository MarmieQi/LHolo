#include "overlay/BoundsWireframe.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"

namespace lholo::overlay {

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

        auto& tessellator = renderContext.getTessellator();
        tessellator.begin(
            Tessellator::DebugContextCallback{},
            mce::PrimitiveMode::LineList,
            24,
            false
        );
        tessellator.colorABGR(static_cast<int>(mColor));
        auto const addEdge = [&](Vec3 const& a, Vec3 const& b) {
            tessellator.vertex(a);
            tessellator.vertex(b);
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
            Tessellator::SupplementaryFieldAutoGenerationMode::None
        ));
        return;
    }

    if (!renderAlphaLayer || !mMesh || !mMesh->isValid()) return;
    auto& client = renderContext.getClient();
    auto* levelRenderer = client.getLevelRenderer();
    if (!levelRenderer) return;
    auto const& material = levelRenderer->getLevelRendererPlayer().mOutlineSelectionMaterial.get();
    if (!material) return;

    auto const& camera = renderContext.getCameraPosition();
    auto matrix = renderContext.getWorldMatrix().push(false);
    matrix->translate(
        static_cast<float>(mMin.x) - camera.x,
        static_cast<float>(mMin.y) - camera.y,
        static_cast<float>(mMin.z) - camera.z
    );
    mMesh->renderMesh(
        renderContext.getScreenContext(),
        material,
        0,
        static_cast<uint>(mMesh->getMeshVertexCount()),
        renderContext.mOffscreenCaptureDescription.get(),
        nullptr
    );
}

} // namespace lholo::overlay
