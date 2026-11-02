#include "projection/world/CorrectionWorldReader.h"
#include "projection/correction/CorrectionCellMask.h"

#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/chunk/ChunkSource.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/chunk/SubChunk.h"

#include <limits>
#include <mutex>

namespace lholo::projection::detail {

bool CorrectionWorldReader::empty(BlockPos const& position) {
    std::array<int, 3> key{correctionSectionCoordinate(position.x), correctionSectionCoordinate(position.y),
                           correctionSectionCoordinate(position.z)};
    if (mSection && *mSection == key)
        return mEmpty;
    mSection = key;
    mEmpty = false;
    ++mProbes;
    if (key[1] < std::numeric_limits<short>::min() || key[1] > std::numeric_limits<short>::max())
        return false;
    // getAvailableChunk withholds incomplete chunks and retains ownership. The
    // same chunk/subchunk/write-lock pattern is used by ClientNetworkHandler.
    auto chunk = mSource.getChunkSource().getAvailableChunk(ChunkPos{key[0], key[2]});
    if (!chunk || chunk->mLoadState->load() != ChunkState::Loaded)
        return false;
    auto* subChunk = chunk->getSubChunk(static_cast<short>(key[1]));
    if (!subChunk)
        return false;
    std::lock_guard lock(subChunk->mWriteLock.get());
    auto state = subChunk->mSubChunkState;
    if (!subChunk->mIsInitialized || !subChunk->mBlocksReadPtr[0] ||
        (state != SubChunk::SubChunkState::Normal && state != SubChunk::SubChunkState::ProcessedSubChunk &&
         state != SubChunk::SubChunkState::RequestFinished) ||
        subChunk->isPlaceHolderSubChunk())
        return false;
    // A Paletted0 storage has one palette entry and no packed cell indices
    // (see SubChunkStoragePaletted<T,0> in the SDK). Inspect one entry per
    // layer, rather than calling isUniform on potentially mixed storage.
    auto airLayer = [](SubChunkStorage<Block> const* storage) {
        return !storage ||
               (storage->getType() == SubChunkStorageUnit::Type::Paletted0 && storage->getElement(0).isAir());
    };
    mEmpty = airLayer(subChunk->mBlocksReadPtr[0]) && airLayer(subChunk->mBlocksReadPtr[1]);
    return mEmpty;
}

} // namespace lholo::projection::detail
