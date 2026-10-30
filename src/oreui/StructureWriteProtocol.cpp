#include "oreui/StructureWriteProtocol.h"
#include "nlohmann/json.hpp"
namespace lholo::oreui {
std::optional<app::StructureWriteIntent> parseStructureWriteIntent(std::string_view payload) {
    if (payload.size() > 4096) return {};
    try {
        auto j = nlohmann::json::parse(payload);
        if (!j.is_object() || j.size() != 7 || j.at("mode") != "local" || !j.at("skipAir").is_boolean()
            || !j.at("id").is_number_unsigned() || !j.at("revision").is_number_unsigned()
            || !j.at("worldRevision").is_number_unsigned()) return {};
        app::StructureWriteIntent value{j.at("id"), j.at("revision"), j.at("worldRevision"), j.at("taskId"), j.at("fileId")};
        value.skipAir = j.at("skipAir");
        if (!value.id || value.id > 9007199254740991ULL || value.revision > 9007199254740991ULL
            || value.worldRevision > 9007199254740991ULL || value.taskId.empty() || value.taskId.size() > 64
            || value.fileId.empty() || value.fileId.size() > 64) return {};
        return value;
    } catch (nlohmann::json::exception const&) { return {}; }
}
}
