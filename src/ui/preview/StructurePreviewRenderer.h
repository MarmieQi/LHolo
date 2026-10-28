#pragma once
#include <memory>
#include <string>

class ClientInstance;
class MinecraftUIRenderContext;
class UIControl;
namespace lholo::structure { struct LoadedStructure; }
namespace lholo::ui::preview {
bool installStructurePreviewRenderer();
void uninstallStructurePreviewRenderer();
// Owns every native resource behind the renderer boundary. No task/session state.
class StructurePreviewRenderer {
public:
    StructurePreviewRenderer();
    ~StructurePreviewRenderer();
    StructurePreviewRenderer(StructurePreviewRenderer const&) = delete;
    StructurePreviewRenderer& operator=(StructurePreviewRenderer const&) = delete;
    void setStructure(std::shared_ptr<structure::LoadedStructure const>);
    void render(MinecraftUIRenderContext&, ClientInstance&, UIControl&, float yaw, float pitch, float zoom);
    std::string const& error() const;
    bool ready() const;
private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
}
