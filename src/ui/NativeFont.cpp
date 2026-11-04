#include "ui/NativeFont.h"
#include "mc/client/gui/FontHandle.h"
#include "mc/client/gui/FontRepository.h"

namespace lholo::ui {
std::shared_ptr<Font> resolveNativeFont(FontHandle const& handle) {
    auto const* repository = handle.mFontRepository->get();
    if (!repository) return {};
    auto const id = handle.mFontId;
    auto const& fonts = repository->mLoadedFonts.get();
    // 26.60 drawDebugText inlines this lookup. The repository entry must win
    // over the handle's default so resource-pack/font overrides still apply.
    if (id != FontRepository::INVALID_FONT() && id < fonts.size())
        return fonts[static_cast<std::size_t>(id)].get();
    return handle.mDefaultFont;
}
}
