#include "oreui/CaptureProtocol.h"
#include "structure/capture/CaptureActions.h"
#include "structure/capture/CaptureBounds.h"
#include "nlohmann/json.hpp"
#include <cstdio>
#include <limits>

// The action runner is tested against a fake world boundary. These tests do
// not read game memory or claim to validate StructureTemplate capture itself.
namespace lholo::structure::capture {
namespace {
Snapshot fake;
int exports{};
std::filesystem::path exportedPath;
}
Snapshot getSnapshot() { return fake; }
void updateDraft(Draft const& draft) { if (fake.draft != draft) { fake.draft = draft; ++fake.revision; } }
void setPointFromPlayer(PointSlot slot) {
    (slot == PointSlot::First ? fake.draft.first : fake.draft.second) = Point{1, 64, -2};
    ++fake.revision;
}
void clear() { fake.draft = {}; ++fake.revision; }
void exportStructure(Draft const&, std::filesystem::path const& output) { ++exports; exportedPath = output; }
}

int runCaptureActionsTests() {
    using namespace lholo::structure::capture;
    using lholo::oreui::parseCaptureIntent;
    int failures{};
    auto check = [&](bool value, char const* name) {
        if (!value) { ++failures; std::fprintf(stderr, "CaptureActionsTests: %s\n", name); }
    };
    auto const reproduced = captureSize({{0, -60, 0}, {35, -44, 153}});
    check(reproduced == std::optional<std::array<int, 3>>{{36, 17, 154}}, "reported 94248-cell selection is accepted without arbitrary size cap");
    check(captureSize({{-20, -64, -30}, {-20, -64, -30}}) == std::optional<std::array<int, 3>>{{1, 1, 1}},
          "inclusive single cell at negative coordinates");
    check(!captureSize({{1,0,0}, {0,1,1}}), "reversed unnormalized extent is rejected");
    check(!captureSize({{std::numeric_limits<int>::min(), 0, 0}, {std::numeric_limits<int>::max(), 0, 0}}),
          "individually valid int32 coordinates cannot overflow native side-length arithmetic");
    check(!captureSize({{0,0,0}, {49999,49999,0}}), "format-1 dense index length overflow rejected");
    check(!captureSize({{0,0,0}, {std::numeric_limits<int>::max()-1, std::numeric_limits<int>::max()-1, std::numeric_limits<int>::max()-1}}),
          "volume validated before multiplying three large axes");
    nlohmann::json payload{
        {"id", 1}, {"revision", 0}, {"action", "apply"},
        {"draft", {{"first", {{"x", -12}, {"y", 63}, {"z", 8}}},
            {"second", {{"x", 2}, {"y", 64}, {"z", 10}}}, {"includeEntities", true}}}
    };
    auto request = parseCaptureIntent(payload.dump());
    check(request && request->draft.first->x == -12 && request->draft.includeEntities, "valid typed draft");
    auto invalid = payload;
    invalid["draft"]["first"]["x"] = 1.5;
    check(!parseCaptureIntent(invalid.dump()), "fraction rejected");
    invalid["draft"]["first"]["x"] = 4294967295ULL;
    check(!parseCaptureIntent(invalid.dump()), "positive int32 overflow rejected");
    invalid["draft"]["first"]["x"] = 18446744073709551615ULL;
    check(!parseCaptureIntent(invalid.dump()), "uint64 overflow cannot wrap into signed coordinate");
    invalid["draft"]["first"]["x"] = -2147483649LL;
    check(!parseCaptureIntent(invalid.dump()), "negative int32 overflow rejected");
    invalid = payload; invalid["draft"]["first"]["x"] = "12";
    check(!parseCaptureIntent(invalid.dump()), "coordinate string rejected");
    invalid = payload; invalid["draft"]["first"] = nullptr; invalid["action"] = "export";
    check(!parseCaptureIntent(invalid.dump()), "incomplete export rejected");
    invalid = payload; invalid["action"] = "other";
    check(!parseCaptureIntent(invalid.dump()), "unknown action rejected");
    invalid = payload; invalid["id"] = 9007199254740992ULL;
    check(!parseCaptureIntent(invalid.dump()), "unsafe JS request id rejected");
    check(!parseCaptureIntent("{") && !parseCaptureIntent(std::string(4097, ' ')), "malformed and oversized payloads rejected");
    resetActions();
    fake = {}; fake.worldAvailable = true;
    check(submitAction(*request), "apply queued");
    check(!submitAction(*request), "concurrent action rejected");
    processActions();
    check(fake.draft == request->draft && completedAction() == 1 && !actionPending(), "apply commits and acknowledges");
    request->action = Action::FirstFromPlayer; request->revision = fake.revision; request->id = 2;
    submitAction(*request); processActions();
    check(fake.draft.first == Point{1, 64, -2}, "player point uses native boundary");
    request->action = Action::Export; request->revision = fake.revision; request->id = 3;
    request->output = std::filesystem::path{L"D:\\导出\\结构.mcstructure"};
    submitAction(*request); processActions();
    check(exports == 1 && exportedPath == request->output, "export preserves selected Unicode output path");
    request->revision = fake.revision; request->id = 4;
    submitAction(*request); ++fake.revision; processActions();
    check(exports == 1 && completedAction() == 4, "selection/context revision change blocks stale export");
    request->revision = fake.revision; request->id = 5;
    submitAction(*request); fake.worldAvailable = false; processActions();
    check(exports == 1 && completedAction() == 5, "world exit blocks export");
    fake.worldAvailable = true;
    request->action = Action::Clear; request->revision = fake.revision; request->id = 6;
    submitAction(*request); processActions();
    check(!fake.draft.first && !fake.draft.second, "clear removes selection");
    request->action = Action::Export; request->revision = fake.revision; request->id = 7;
    submitAction(*request); resetActions(); processActions();
    check(exports == 1 && !actionPending(), "reset drops queued world action");
    std::printf("CaptureActionsTests: %d failures\n", failures);
    return failures;
}
