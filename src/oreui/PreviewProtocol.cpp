#include "oreui/PreviewProtocol.h"
#include "oreui/CaptureProtocol.h"
#include "structure/capture/CaptureBounds.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
namespace lholo::oreui {
namespace {
bool parseFrame(nlohmann::json const& j, ui::preview::PreviewFrame& out) {
    if (!j.contains("frame")) return true;
    auto const& frame = j.at("frame");
    if (!frame.is_array() || frame.size() != 2 || !frame[0].is_number() || !frame[1].is_number()) return false;
    double const width = frame[0], height = frame[1];
    if (!(width >= 0.1 && width <= 1 && height >= 0.1 && height <= 1)) return false;
    out = {static_cast<float>(width), static_cast<float>(height)};
    return true;
}

std::optional<structure::capture::Point> parsePoint(nlohmann::json const& point) {
    if (!point.is_object()) return std::nullopt;
    auto coordinate = [](nlohmann::json const& number) {
        if (number.is_number_unsigned()
            && number.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            throw std::invalid_argument("coordinate range");
        }
        if (!number.is_number_integer()) throw std::invalid_argument("coordinate");
        auto const n = number.get<std::int64_t>();
        if (n < std::numeric_limits<int>::min() || n > std::numeric_limits<int>::max()) {
            throw std::invalid_argument("coordinate range");
        }
        return static_cast<int>(n);
    };
    return structure::capture::Point{coordinate(point.at("x")), coordinate(point.at("y")), coordinate(point.at("z"))};
}
}
std::optional<app::StructurePreviewIntent> parsePreviewIntent(std::string_view payload) {
    if (payload.size() > kCaptureIntentMaxBytes) return {};
    try {
        auto j = nlohmann::json::parse(payload);
        if (!j.is_object() || !j.at("id").is_number_unsigned()) return {};
        auto const id = j.at("id").get<std::uint64_t>();
        if (!id || id > 9007199254740991ULL) return {};

        app::StructurePreviewIntent value;
        value.id = id;
        if (!parseFrame(j, value.frame)) return {};

        std::string const source = j.contains("source") ? j.at("source").get<std::string>() : "task";
        if (source == "captureWorld") {
            if (payload.size() > 4096 || !j.at("revision").is_number_unsigned()) return {};
            auto const revision = j.at("revision").get<std::uint64_t>();
            if (revision > 9007199254740991ULL) return {};
            auto const& draft = j.at("draft");
            if (!draft.is_object() || !draft.at("includeEntities").is_boolean()) return {};
            auto first = parsePoint(draft.at("first"));
            auto second = parsePoint(draft.at("second"));
            if (!first || !second) return {};
            value.source = app::PreviewSourceKind::CaptureWorld;
            value.revision = revision;
            value.captureDraft.first = first;
            value.captureDraft.second = second;
            value.captureDraft.includeEntities = draft.at("includeEntities").get<bool>();
            return value;
        }
        if (source == "captureCustom") {
            auto const& size = j.at("size");
            std::array const axes{"x", "y", "z"};
            for (std::size_t index = 0; index < axes.size(); ++index) {
                auto const& extent = size.at(axes[index]);
                if (!extent.is_number_unsigned()) return {};
                auto const length = extent.get<std::uint64_t>();
                if (length == 0 || length > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) return {};
                value.customStructure.size[index] = static_cast<int>(length);
            }
            if (!structure::capture::customStructureVolume(value.customStructure.size)) return {};
            auto const& layers = j.at("layers");
            if (!layers.is_array() || layers.size() > static_cast<std::size_t>(value.customStructure.size[1])) return {};
            auto& fills = value.customStructure.layers;
            fills.reserve(layers.size());
            for (auto const& layer : layers) {
                auto const& y = layer.at("y");
                auto const& endY = layer.at("endY");
                if (!y.is_number_unsigned() || !endY.is_number_unsigned()
                    || y.get<std::uint64_t>() > endY.get<std::uint64_t>()
                    || endY.get<std::uint64_t>() >= static_cast<std::uint64_t>(value.customStructure.size[1])) {
                    return {};
                }
                auto const& block = layer.at("block").get_ref<std::string const&>();
                if (block.empty()) return {};
                fills.push_back({y.get<int>(), endY.get<int>(), block});
            }
            std::ranges::sort(fills, {}, &structure::capture::CustomLayerFill::y);
            for (std::size_t i = 1; i < fills.size(); ++i) {
                if (fills[i - 1].endY >= fills[i].y) return {};
            }
            value.source = app::PreviewSourceKind::CaptureCustom;
            return value;
        }
        if (source != "task") return {};
        if (payload.size() > 4096) return {};
        std::size_t const expectedSize = 4 + (j.contains("frame") ? 1 : 0) + (j.contains("source") ? 1 : 0);
        if (j.size() != expectedSize || !j.at("revision").is_number_unsigned()) return {};
        value.revision = j.at("revision").get<std::uint64_t>();
        value.taskId = j.at("taskId").get<std::string>();
        value.fileId = j.at("fileId").get<std::string>();
        if (value.revision > 9007199254740991ULL
            || value.taskId.empty() || value.taskId.size() > 64
            || value.fileId.empty() || value.fileId.size() > 64) return {};
        return value;
    } catch (std::exception const&) { return {}; }
}
}
