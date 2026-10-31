#include "oreui/StructureWriteProtocol.h"
#include "nlohmann/json.hpp"
namespace lholo::oreui {
std::optional<app::StructureWriteIntent> parseStructureWriteIntent(std::string_view payload) {
    if (payload.size() > 4096) return {};
    try {
        auto j = nlohmann::json::parse(payload);
        if (!j.is_object()) return {};
        if (j.size() == 3 && j.contains("action")) {
            app::StructureWriteIntent value;
            if (!j.at("id").is_number_unsigned() || !j.at("worldRevision").is_number_unsigned()) return {};
            value.id = j.at("id"); value.worldRevision = j.at("worldRevision");
            if (!value.id || value.id > 9007199254740991ULL || value.worldRevision > 9007199254740991ULL) return {};
            if (j.at("action") == "resume") value.action = app::StructureWriteIntent::Action::Resume;
            else if (j.at("action") == "cancel") value.action = app::StructureWriteIntent::Action::Cancel;
            else return {};
            return value;
        }
        if (j.size() != 8 || (j.at("mode") != "local" && j.at("mode") != "commands") || !j.at("skipAir").is_boolean()
            || !j.at("skipPlaced").is_boolean() || (j.at("mode") == "local" && j.at("skipPlaced") != false)
            || !j.at("id").is_number_unsigned() || !j.at("revision").is_number_unsigned()
            || !j.at("worldRevision").is_number_unsigned()) return {};
        app::StructureWriteIntent value{j.at("id"), j.at("revision"), j.at("worldRevision"), j.at("taskId"), j.at("fileId")};
        value.skipAir = j.at("skipAir");
        value.skipPlaced = j.at("skipPlaced");
        value.mode = j.at("mode") == "commands" ? structure::write::WriteMode::ServerCommands : structure::write::WriteMode::LocalWorld;
        if (!value.id || value.id > 9007199254740991ULL || value.revision > 9007199254740991ULL
            || value.worldRevision > 9007199254740991ULL || value.taskId.empty() || value.taskId.size() > 64
            || value.fileId.empty() || value.fileId.size() > 64) return {};
        return value;
    } catch (nlohmann::json::exception const&) { return {}; }
}
}
