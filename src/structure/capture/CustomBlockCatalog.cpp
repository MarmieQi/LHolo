#include "structure/capture/CustomBlockCatalog.h"

#include "mc/locale/I18n.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/registry/BlockTypeRegistry.h"

namespace lholo::structure::capture {

std::vector<CustomBlockEntry> customBlockCatalog(std::string_view localeCode) {
    auto const& blocks = BlockTypeRegistry::get().mBlockLookupMap.get();
    auto& translator = ::getI18n();
    auto const locale = translator.getLocaleFor(std::string{localeCode});
    std::vector<CustomBlockEntry> result;
    result.reserve(blocks.size());
    for (auto const& entry : blocks) {
        auto const* block = entry.second->mDefaultState;
        if (!block || block->isAir()) continue;
        auto key = entry.second->buildDescriptionId(*block);
        if (!key.ends_with(".name")) key += ".name";
        auto displayName = locale ? translator.get(key, std::vector<std::string>{}, locale) : std::string{};
        if (displayName.empty() || displayName == key) displayName = block->getTypeName();
        result.push_back({block->getTypeName(), std::move(displayName)});
    }
    return result;
}

} // namespace lholo::structure::capture
