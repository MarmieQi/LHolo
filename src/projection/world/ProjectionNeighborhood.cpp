#include "projection/core/ProjectionState.h"
#include <stdexcept>
namespace lholo::projection::detail {
CorrectionState ProjectionState::correctionAtWorld(ProjectionWorldLookup::Key const &key) const {
    auto owner = worldLookup->ownerAt(key);
    if (!owner) {
        auto task = worldLookup->task();
        if (!task) {
            auto local = worldLookup->localCellAt(key);
            return local && extraBlockPositions.contains(SubChunkKey{(*local)[0], (*local)[1], (*local)[2]})
                       ? CorrectionState::WrongType
                       : CorrectionState::Correct;
        }
        auto [x, y, z] = key;
        auto air = worldLookup->airOwnerAt(key);
        if (!air)
            return CorrectionState::Correct;
        if (*air != worldLookup->fileIndex() && correctionSnapshot) {
            for (auto const &neighbor : neighborCorrections)
                if (neighbor.file == *air && neighbor.extrasWorld.contains(key))
                    return CorrectionState::WrongType;
            return CorrectionState::Correct;
        }
        auto local = task->placements->localCell(*air, {x, y, z});
        auto foreign = *air == worldLookup->fileIndex() ? nullptr : taskRuntimeFiles[*air].lock();
        assert(*air == worldLookup->fileIndex() || foreign);
        auto const &extras = *air == worldLookup->fileIndex() ? extraBlockPositions : foreign->extraBlockPositions;
        return local && extras.contains(SubChunkKey{(*local)[0], (*local)[1], (*local)[2]}) ? CorrectionState::WrongType
                                                                                            : CorrectionState::Correct;
    }
    if (owner->file == worldLookup->fileIndex())
        return renderCorrectionState(owner->index);
    if (correctionSnapshot) {
        for (auto const &neighbor : neighborCorrections)
            if (neighbor.file == owner->file)
                return neighbor.states.rawState(owner->index);
        throw std::logic_error("Missing task neighbor correction snapshot");
    }
    auto other = taskRuntimeFiles[owner->file].lock();
    assert(other);
    return other->renderCorrectionState(owner->index);
}
} // namespace lholo::projection::detail
