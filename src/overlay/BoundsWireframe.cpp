#include "overlay/BoundsWireframe.h"

#include "render/OverlayMaterials.h"
#include "render/RenderCamera.h"
#include "render/BoxWireframeMesh.h"
#include "render/ScopedShaderColor.h"

#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/deps/minecraft_renderer/framebuilder/dragon/RenderMetadata.h"
#include "mc/deps/minecraft_renderer/renderer/Mesh.h"
#include "mc/deps/renderer/Camera.h"

namespace lholo::overlay {

namespace {

OffscreenCaptureDescription const& emptyOffscreenCaptureDescription() {
    using Storage = decltype(dragon::RenderMetadata::mOffscreenCaptureDescription);
    static Storage empty{};
    return empty.get();
}

} // namespace

BoundsWireframe::~BoundsWireframe() = default;

void BoundsWireframe::setBounds(BlockPos const& min, BlockPos const& max, std::uint32_t color) {
    mColor = color;
    if (mHasBounds && mMin == min && mMax == max) return;
    mMin = min;
    mMax = max;
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
        // Inclusive block coordinates become exact cell boundaries.
        float const x1 = static_cast<float>(mMax.x - mMin.x + 1);
        float const y1 = static_cast<float>(mMax.y - mMin.y + 1);
        float const z1 = static_cast<float>(mMax.z - mMin.z + 1);

        mMesh = render::buildBoxWireframeMesh(renderContext.mScreenContext.tessellator,
            {0,0,0}, {x1,y1,z1}, "LHoloSelectionBounds");
        return;
    }

    if (!renderAlphaLayer || !mMesh || !mMesh->isValid()) return;
    auto& client = renderContext.mClientInstance;
    auto* levelRenderer = client.getLevelRenderer();
    if (!levelRenderer) return;
    auto const& material = render::selectionOutlineMaterial(*levelRenderer);
    if (!render::materialExists(material)) return;
    auto const texture = render::resolveWhiteTextureVariant(*levelRenderer);

    Vec3 const camera = render::cameraPosition(renderContext);
    auto matrix = renderContext.mScreenContext.camera.worldMatrixStack.get().push(false);
    matrix.mat->translate(
        static_cast<float>(mMin.x) - camera.x,
        static_cast<float>(mMin.y) - camera.y,
        static_cast<float>(mMin.z) - camera.z
    );
    render::ScopedShaderColor boxColor{renderContext.mScreenContext.currentShaderColor, mColor};
    mMesh->renderMesh(
        renderContext.mScreenContext,
        material,
        texture,
        0,
        mMesh->mVertexCount.get().value_or(0u),
        emptyOffscreenCaptureDescription(),
        nullptr
    );
}

} // namespace lholo::overlay
