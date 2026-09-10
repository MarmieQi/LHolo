// LHolo - Deferred interface messages
//
// A message that has to survive across frames is stored as a key plus string
// arguments rather than rendered text, so the display boundary always renders
// it in the language selected at draw time. Arguments are pre-formatted,
// language-neutral fragments (file names, numbers, sizes); the table entry
// supplies the wording and uses %s for each argument.

#pragma once

#include "i18n/Translator.h"

#include <array>
#include <cstddef>
#include <string>

namespace lholo::i18n {

inline constexpr std::size_t kMessageArgCount = 3;

struct Message {
    // Defaults to the "no message" sentinel, never to a real entry.
    TextKey                                   key{TextKey::None};
    std::array<std::string, kMessageArgCount> args{};
};

// Renders the message in the current language. Only %s placeholders are
// substituted, in order; every other character is copied literally, so a stray
// percent sign in a translation can never be read as a printf directive, and
// unused arguments stay harmless.
std::string format(Message const& message);

} // namespace lholo::i18n
