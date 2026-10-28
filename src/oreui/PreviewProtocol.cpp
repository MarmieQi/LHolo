#include "oreui/PreviewProtocol.h"
#include "nlohmann/json.hpp"
namespace lholo::oreui {
std::optional<app::StructurePreviewIntent> parsePreviewIntent(std::string_view payload) {
    if (payload.size() > 4096) return {};
    try {
        auto j = nlohmann::json::parse(payload);
        if (!j.is_object() || (j.size() != 4 && !(j.size() == 5 && j.contains("frame"))) || !j.at("id").is_number_unsigned()
            || !j.at("revision").is_number_unsigned()) return {};
        app::StructurePreviewIntent value{j.at("id"), j.at("revision"), j.at("taskId"), j.at("fileId")};
        if (j.contains("frame")) {
            auto const& frame = j.at("frame");
            if (!frame.is_array() || frame.size() != 2 || !frame[0].is_number() || !frame[1].is_number()) return {};
            double const width = frame[0], height = frame[1];
            if (!(width >= 0.1 && width <= 1 && height >= 0.1 && height <= 1)) return {};
            value.frame = {static_cast<float>(width), static_cast<float>(height)};
        }
        if (!value.id || value.id > 9007199254740991ULL || value.revision > 9007199254740991ULL
            || value.taskId.empty() || value.taskId.size() > 64 || value.fileId.empty() || value.fileId.size() > 64) return {};
        return value;
    } catch (nlohmann::json::exception const&) { return {}; }
}
}
