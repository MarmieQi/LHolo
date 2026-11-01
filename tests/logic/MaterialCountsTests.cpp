#include "structure/MaterialCounts.h"
#include <cstdio>
#include <memory>

int runMaterialCountsTests() {
    int failures{};
    auto check = [&](bool value, char const* message) {
        if (!value) {
            ++failures;
            std::fprintf(stderr, "MaterialCountsTests: %s\n", message);
        }
    };
    // Opaque pointers only: this worker path must not access Minecraft objects.
    int blockToken{};
    auto const* block = reinterpret_cast<Block const*>(&blockToken);
    auto source = [] {
        auto token = std::make_shared<int>();
        return std::shared_ptr<CompoundTag const>{token, reinterpret_cast<CompoundTag const*>(token.get())};
    };
    auto ordinary = source(), ominous = source();
    lholo::structure::detail::MaterialCounts counts;
    counts.add(block, ordinary);
    counts.add(block, ominous);
    counts.add(block, ominous);
    auto const& entries = counts.entries();
    check(entries.size() == 2, "same block retains different banner NBT");
    if (entries.size() == 2)
        check(entries[0].nbt == ordinary && entries[0].count == 1
            && entries[1].nbt == ominous && entries[1].count == 2,
            "each variant keeps its own source and count");
    counts.add(nullptr, ordinary);
    check(entries.size() == 2, "null block contributes nothing");
    std::printf("MaterialCountsTests: %d failures\n", failures);
    return failures;
}
