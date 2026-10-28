#pragma once
#include <string>
#include <algorithm>
#include <cmath>
#include <array>
#include <cstdint>
#include "ui/preview/StructurePreviewFrame.h"
#include "mc/deps/input/PointerType.h"
class ClientInstance;
class UIRenderContext;
namespace lholo::ui::preview {
inline constexpr float kPreviewCloseLeft = 0.90f, kPreviewCloseRight = 0.96f;
inline constexpr float kPreviewCloseTop = 0.025f, kPreviewCloseBottom = 0.085f;
struct PreviewViewState {
    PreviewFrame frame;
    std::string name, error;
    std::array<int,3> dimensions{};
    std::uint64_t blockCount{};
    bool hasMetadata{};
    float pointerX{}, pointerY{}, yaw{95}, pitch{10}, zoom{1};
    bool dragging{}, closePressed{}, loading{true};
    void moveOnScreen(float x, float y) {
        move((x - (1 - frame.width) * 0.5f) / frame.width,
             (y - (1 - frame.height) * 0.5f) / frame.height);
    }
    bool overCanvas() const { return pointerX >= 0.04f && pointerX <= 0.96f && pointerY >= 0.10f && pointerY <= 0.90f; }
    Bedrock::Input::PointerType pointerType() const {
        using Bedrock::Input::PointerType;
        if (dragging) return PointerType::Grabbing;
        if (overCanvas()) return PointerType::Grab;
        if (pointerX >= kPreviewCloseLeft && pointerX <= kPreviewCloseRight
            && pointerY >= kPreviewCloseTop && pointerY <= kPreviewCloseBottom) return PointerType::Hand;
        return PointerType::Arrow;
    }
    void move(float x, float y) {
        if (dragging) {
            yaw = std::remainder(yaw + (x - pointerX) * 300.0f, 360.0f);
            pitch = std::clamp(pitch + (y - pointerY) * 180.0f, -85.0f, 85.0f);
        }
        pointerX = x; pointerY = y;
    }
    bool press(bool down) {
        bool const close = pointerX >= kPreviewCloseLeft && pointerX <= kPreviewCloseRight
            && pointerY >= kPreviewCloseTop && pointerY <= kPreviewCloseBottom;
        bool const finished = !down && closePressed && close;
        closePressed = down && close;
        dragging = down && !close && overCanvas();
        return finished;
    }
    void wheel(int steps) {
        zoom = std::clamp(zoom * std::pow(1.12f, static_cast<float>(steps)), 0.5f, 3.0f);
    }
};
void drawStructurePreviewView(UIRenderContext&, ClientInstance&, PreviewViewState const&);
void drawStructurePreviewBackground(UIRenderContext&, ClientInstance&, PreviewViewState const&);
}
