#include "projection/mesh/ProjectionMeshOcclusion.h"
#include "projection/core/ProjectionBottomFace.h"
#include "projection/core/ProjectionState.h"
#include "projection/world/ProjectionVirtualWorld.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

#include <algorithm>

namespace lholo::projection::detail {

bool projectionFaceCovered(ProjectionState const& state, BlockSource& region,
    BlockPos const& neighbor, bool& worldDependent) {
    auto const key = std::tuple{neighbor.x, neighbor.y, neighbor.z};
    auto const index = state.worldLookup->indexAt(key);
    if (index) {
        auto const correction = state.renderCorrectionState(*index);
        if (correction == CorrectionState::Missing || correction == CorrectionState::Unknown) {
            auto const block = state.worldLookup->blockAt(key);
            if (block && block->getBlockType().mIsOpaqueFullBlock) return true;
        }
    }
    worldDependent = true;
    return readRealTessellationBlock(region, neighbor).getBlockType().mIsOpaqueFullBlock;
}

void hideProjectionBottomFaces(ProjectionTransparentGeometry& geometry,
    std::span<ProjectionBottomRange const> ranges) {
    for (auto const& range : ranges) {
        for (auto i = range.first / 4; i < range.last / 4; ++i) {
            std::array<std::array<float, 3>, 4> positions;
            for (std::size_t j = 0; j < 4; ++j) positions[j] = geometry.quads[i].vertices[j].position;
            geometry.quads[i].hidden = geometry.quads[i].hidden || projectionBottomFace(positions, range.cell);
        }
    }
}

bool hasProjectionBottomFace(mce::MeshData const& data, std::size_t first,
    std::array<float, 3> const& cell) {
    auto const& positions = data.mPositions.get();
    if (data.mMode != mce::PrimitiveMode::QuadList || !data.mIndices.get().empty()
        || first % 4 != 0 || positions.size() % 4 != 0) return false;
    for (auto i = first; i < positions.size(); i += 4) {
        std::array<std::array<float, 3>, 4> quad;
        for (std::size_t j = 0; j < 4; ++j) {
            auto const& p = positions[i + j];
            quad[j] = {p.x, p.y, p.z};
        }
        if (projectionBottomFace(quad, cell)) return true;
    }
    return false;
}

namespace {
template<class F> void vertexFields(mce::MeshData& data, F&& f) {
    f(data.mPositions.get()); f(data.mNormals.get()); f(data.mTangents.get());
    f(data.mColors.get()); f(data.mBoneId0s.get());
    for (auto& uv : data.mTextureUVs) f(uv.get());
    f(data.mPBRTextureIndices.get()); f(data.mMERS.get()); f(data.mGeoType.get());
}
}

void cullProjectionBottomFaces(mce::MeshData& data, std::span<ProjectionBottomRange const> ranges) {
    auto& positions = data.mPositions.get();
    auto const count = positions.size();
    if (ranges.empty() || data.mMode != mce::PrimitiveMode::QuadList
        || !data.mIndices.get().empty() || count % 4 != 0) return;
    bool consistent = true;
    vertexFields(data, [&](auto const& field) { consistent &= field.empty() || field.size() == count; });
    std::size_t previousEnd{};
    for (auto const& range : ranges) {
        consistent &= range.first >= previousEnd && range.first % 4 == 0 && range.last % 4 == 0
            && range.first <= range.last && range.last <= count;
        previousEnd = range.last;
    }
    if (!consistent) return;

    std::size_t write{}, rangeIndex{};
    for (std::size_t read = 0; read < count; read += 4) {
        while (rangeIndex < ranges.size() && read >= ranges[rangeIndex].last) ++rangeIndex;
        if (rangeIndex < ranges.size() && read >= ranges[rangeIndex].first) {
            std::array<std::array<float, 3>, 4> quad;
            for (std::size_t j = 0; j < 4; ++j) {
                auto const& p = positions[read + j];
                quad[j] = {p.x, p.y, p.z};
            }
            if (projectionBottomFace(quad, ranges[rangeIndex].cell)) continue;
        }
        if (write != read) vertexFields(data, [&](auto& field) {
            if (!field.empty()) std::copy_n(field.begin() + read, 4, field.begin() + write);
        });
        write += 4;
    }
    if (write != count) vertexFields(data, [&](auto& field) { if (!field.empty()) field.resize(write); });
}

} // namespace lholo::projection::detail
