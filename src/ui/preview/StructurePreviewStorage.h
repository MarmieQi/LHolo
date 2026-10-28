#pragma once
class Block;
namespace lholo::ui::preview {
struct PreviewStorageLayers { Block const* primary{}; Block const* extra{}; };
// Parsed files separate body/liquid for projection queries. Native chunks
// store standalone water/lava in layer 0; layer 1 is for waterlogged bodies.
inline PreviewStorageLayers previewStorageLayers(Block const* body, Block const* liquid) {
    return {body ? body : liquid, body ? liquid : nullptr};
}
}
