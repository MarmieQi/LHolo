#include "oreui/PreviewProtocol.h"
#include "ui/preview/StructurePreviewView.h"
#include "ui/preview/StructurePreviewDefinition.h"
#include "ui/preview/StructurePreviewTransform.h"
#include "ui/preview/StructurePreviewStorage.h"
#include "nlohmann/json.hpp"
#include <cstdio>

int runStructurePreviewTests() {
    int failures{};
    auto check = [&](bool ok, char const* description) {
        if (!ok) { ++failures; std::printf("StructurePreviewTests FAILED: %s\n", description); }
    };
    using lholo::oreui::parsePreviewIntent;
    // Opaque identities only: no Minecraft block object is constructed or read.
    auto const* body = reinterpret_cast<Block const*>(0x1000);
    auto const* water = reinterpret_cast<Block const*>(0x2000);
    auto const* lava = reinterpret_cast<Block const*>(0x3000);
    for (auto const* liquid : {water,lava}) {
        auto layers = lholo::ui::preview::previewStorageLayers(nullptr,liquid);
        check(layers.primary == liquid && !layers.extra,
            "standalone water/lava enters native primary layer instead of air plus extra liquid");
    }
    auto layers = lholo::ui::preview::previewStorageLayers(body,water);
    check(layers.primary == body && layers.extra == water, "waterlogged block retains body and extra water");
    layers = lholo::ui::preview::previewStorageLayers(body,nullptr);
    check(layers.primary == body && !layers.extra, "dry solid never gains an extra liquid layer");
    layers = lholo::ui::preview::previewStorageLayers(nullptr,nullptr);
    check(!layers.primary && !layers.extra, "empty cell remains empty");
    auto definition = nlohmann::json::parse(lholo::ui::preview::kStructurePreviewDefinition, nullptr, false);
    check(!definition.is_discarded(), "native scene definition must be valid JSON before game scene creation");
    if (!definition.is_discarded()) {
        check(definition.value("type", "") == "screen" && definition.value("absorbs_input", false),
            "preview remains an independent input-absorbing native screen");
    }
    auto valid = nlohmann::json{{"id", 1}, {"revision", 7}, {"taskId", "task"}, {"fileId", "file"}};
    auto parsed = parsePreviewIntent(valid.dump());
    check(parsed && parsed->id == 1 && parsed->revision == 7 && parsed->fileId == "file", "stable file identity accepted");
    auto measured = valid; measured["frame"] = {0.8, 0.65};
    parsed = parsePreviewIntent(measured.dump());
    check(parsed && parsed->frame.width == 0.8f && parsed->frame.height == 0.65f, "actual menu frame survives bridge");
    measured["frame"] = {1.1, 0.65};
    check(!parsePreviewIntent(measured.dump()), "frame cannot extend beyond screen");
    auto invalid = valid; invalid["path"] = "D:/untrusted.mcstructure";
    check(!parsePreviewIntent(invalid.dump()), "frontend cannot supply an arbitrary path");
    for (auto const key : {"id", "revision"}) {
        invalid = valid; invalid[key] = -1;
        check(!parsePreviewIntent(invalid.dump()), "negative identifiers rejected");
        invalid[key] = 9007199254740992ULL;
        check(!parsePreviewIntent(invalid.dump()), "unsafe JavaScript integers rejected");
    }
    invalid = valid; invalid["id"] = 0;
    check(!parsePreviewIntent(invalid.dump()), "zero request rejected");
    invalid = valid; invalid["fileId"] = "";
    check(!parsePreviewIntent(invalid.dump()), "empty file identity rejected");
    invalid["fileId"] = std::string(65, 'x');
    check(!parsePreviewIntent(invalid.dump()), "oversized identity rejected");
    check(!parsePreviewIntent("[]") && !parsePreviewIntent("{") && !parsePreviewIntent(std::string(4097, 'x')),
        "invalid or oversized JSON rejected");

    using lholo::ui::preview::PreviewViewState;
    // Match native fitting: an off-origin canvas and a rotated model whose
    // centre has already been translated into GUI coordinates.
    glm::mat4 fitted{1};
    fitted[0] = {0,2,0,0}; fitted[1] = {-2,0,0,0}; fitted[3] = {320,180,7,1};
    auto const original = fitted;
    auto enlarged = lholo::ui::preview::zoomPreviewTransform(fitted,{320,180},2);
    auto smaller = lholo::ui::preview::zoomPreviewTransform(fitted,{320,180},0.5f);
    check(enlarged * glm::vec4{0,0,0,1} == original * glm::vec4{0,0,0,1},
        "zoom leaves the fitted model centre fixed in an off-origin canvas");
    auto const largePoint = enlarged * glm::vec4{3,4,5,1};
    auto const smallPoint = smaller * glm::vec4{3,4,5,1};
    check(largePoint.x == 304 && largePoint.y == 192 && smallPoint.x == 316 && smallPoint.y == 183,
        "zoom changes rendered geometry extent in both wheel directions");
    check(largePoint.z == 12 && largePoint.w == 1 && fitted == original,
        "zoom preserves depth and original transform for the native axes pass");
    PreviewViewState s;
    s.frame = {0.8f,0.6f};
    s.moveOnScreen(0.5f,0.5f);
    check(std::abs(s.pointerX - 0.5f) < 0.0001f && std::abs(s.pointerY - 0.5f) < 0.0001f,
        "screen pointer maps into centered menu frame");
    s.moveOnScreen(0.01f,0.01f); s.press(true);
    check(!s.dragging && !s.overCanvas(), "outside frame cannot drag or zoom preview");
    check(s.pointerType() == Bedrock::Input::PointerType::Arrow, "outside canvas uses arrow");
    s.frame = {};
    check(s.yaw == 95 && s.pitch == 10 && s.zoom == 1, "restore original preview camera defaults");
    s.move(0.5f, 0.5f); s.press(true); auto const yaw = s.yaw;
    check(s.pointerType() == Bedrock::Input::PointerType::Grabbing, "dragging uses closed grab cursor");
    s.move(0.6f, 0.6f);
    check(s.dragging && s.yaw != yaw, "drag changes preview camera");
    s.move(0.6f, 100.0f);
    check(s.pitch == 85.0f, "pitch stays within camera limits");
    s.press(false); auto const releasedYaw = s.yaw;
    s.move(0.7f, 0.6f);
    check(s.pointerType() == Bedrock::Input::PointerType::Grab, "hovering uses open grab cursor");
    check(s.yaw == releasedYaw, "released pointer does not keep rotating");
    s.wheel(1000); check(s.zoom == 3.0f, "zoom upper bound");
    s.wheel(-1000); check(s.zoom == 0.5f, "zoom lower bound");
    auto const closeX = (lholo::ui::preview::kPreviewCloseLeft + lholo::ui::preview::kPreviewCloseRight) * 0.5f;
    auto const closeY = (lholo::ui::preview::kPreviewCloseTop + lholo::ui::preview::kPreviewCloseBottom) * 0.5f;
    s.move(closeX, closeY); s.press(true);
    check(s.pointerType() == Bedrock::Input::PointerType::Hand, "close uses hand cursor");
    check(!s.dragging && s.press(false), "close press and release completes");
    s.press(true); s.move(0.5f, 0.5f);
    check(!s.press(false), "releasing outside close button cancels close");
    s.move(0.1f, closeY);
    s.press(true);
    check(!s.dragging && !s.closePressed, "title area does not rotate the structure or close the scene");
    return failures;
}
