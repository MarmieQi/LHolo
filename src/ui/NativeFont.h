#pragma once

#include <memory>

class Font;
class FontHandle;

namespace lholo::ui {
std::shared_ptr<Font> resolveNativeFont(FontHandle const& handle);
}
