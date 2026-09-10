// LHolo - Deferred interface messages

#include "i18n/Message.h"

namespace lholo::i18n {

std::string format(Message const& message) {
    char const* pattern = tr(message.key);
    if (pattern == nullptr || *pattern == '\0') return {};
    std::string rendered;
    std::size_t argIndex = 0;
    for (char const* cursor = pattern; *cursor != '\0'; ++cursor) {
        // Message arguments are already formatted, language-neutral fragments,
        // so %s is the only placeholder a message pattern may use.
        if (cursor[0] == '%' && cursor[1] == 's' && argIndex < kMessageArgCount) {
            rendered += message.args[argIndex++];
            ++cursor;
            continue;
        }
        rendered.push_back(*cursor);
    }
    return rendered;
}

} // namespace lholo::i18n
