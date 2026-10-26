#include "ui/MaterialFormat.h"
#include <cstdio>

namespace lholo::ui {

std::string materialIconId(std::string const& itemId, std::string const& typeName, int aux) {
    if (itemId.empty()) return typeName;
    return aux >= 0 ? itemId + ":" + std::to_string(aux) : itemId;
}
std::string formatStackCount(std::uint64_t count, int stackSize) {
    auto const stack = static_cast<std::uint64_t>(stackSize > 0 ? stackSize : 64);
    if (count < stack) return std::to_string(count);
    auto const stacks = count / stack;
    auto const remainder = count % stack;
    char buffer[64];
    if (remainder == 0) {
        std::snprintf(
            buffer, sizeof(buffer), "%llu (%llu x %llu)",
            static_cast<unsigned long long>(count),
            static_cast<unsigned long long>(stacks),
            static_cast<unsigned long long>(stack)
        );
    } else {
        std::snprintf(
            buffer, sizeof(buffer), "%llu (%llu x %llu + %llu)",
            static_cast<unsigned long long>(count),
            static_cast<unsigned long long>(stacks),
            static_cast<unsigned long long>(stack),
            static_cast<unsigned long long>(remainder)
        );
    }
    return buffer;
}

char const* materialDisplayName(
    std::string const&                  displayName,
    std::optional<i18n::TextKey> const& nameKey
) {
    return nameKey ? i18n::tr(*nameKey) : displayName.c_str();
}

}
