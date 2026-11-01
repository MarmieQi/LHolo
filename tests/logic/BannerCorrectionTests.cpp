#include "projection/correction/BannerCorrection.h"
#include <cstdio>

int runBannerCorrectionTests() {
    using namespace lholo::projection::detail;
    int failures{};
    auto check = [&](bool value, char const* message) {
        if (!value) { ++failures; std::fprintf(stderr, "BannerCorrectionTests: %s\n", message); }
    };
    BannerAppearance expected{10, BannerBlockType::Default, {1, 2}, {3, 4}};
    auto actual = expected;
    check(compareBannerAppearance(expected, actual) == CorrectionState::Correct, "matching native appearance");
    actual.baseColor = 9;
    check(compareBannerAppearance(expected, actual) == CorrectionState::WrongType, "wrong base color");
    actual = expected; actual.type = BannerBlockType::IllagerCaptain;
    check(compareBannerAppearance(expected, actual) == CorrectionState::WrongType, "ordinary and ominous differ");
    actual = expected; actual.patterns = {2, 1};
    check(compareBannerAppearance(expected, actual) == CorrectionState::WrongState, "pattern order matters");
    actual = expected; actual.colors[1] = 5;
    check(compareBannerAppearance(expected, actual) == CorrectionState::WrongState, "pattern colors matter");
    actual = expected; actual.patterns.pop_back();
    check(compareBannerAppearance(expected, actual) == CorrectionState::WrongState, "missing pattern differs");
    actual.baseColor = 9;
    check(compareBannerAppearance(expected, actual) == CorrectionState::WrongType, "material error has priority");
    std::printf("BannerCorrectionTests: %d failures\n", failures);
    return failures;
}
