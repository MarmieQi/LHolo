#include "oreui/CaptureProtocol.h"
#include "structure/capture/CaptureBounds.h"
#include "nlohmann/json.hpp"
#include <limits>

namespace lholo::oreui {
std::optional<structure::capture::Request> parseCaptureIntent(std::string_view payload) {
    using namespace structure::capture;
    if (payload.size() > 4096) return std::nullopt;
    try {
        auto const value = nlohmann::json::parse(payload);
        auto validId = [](auto const& number) {
            return number.is_number_unsigned() && number.template get<std::uint64_t>() <= 9007199254740991ULL;
        };
        if (!value.is_object() || !validId(value.at("id")) || !validId(value.at("revision"))
            || value.at("id").get<std::uint64_t>() == 0) return std::nullopt;
        Request request;
        request.id = value.at("id").get<std::uint64_t>();
        request.revision = value.at("revision").get<std::uint64_t>();
        auto const action = value.at("action").get<std::string>();
        if (action == "exportEmpty") {
            auto const& size = value.at("size");
            std::array const axes{"x", "y", "z"};
            for (std::size_t index = 0; index < axes.size(); ++index) {
                auto const& extent = size.at(axes[index]);
                if (!extent.is_number_unsigned()) return std::nullopt;
                auto const length = extent.get<std::uint64_t>();
                if (length == 0 || length > std::numeric_limits<int>::max()) return std::nullopt;
                request.size[index] = static_cast<int>(length);
            }
            if (!emptyStructureVolume(request.size)) return std::nullopt;
            request.action = Action::ExportEmpty;
            return request;
        }
        if (action == "apply") request.action = Action::Apply;
        else if (action == "first") request.action = Action::FirstFromPlayer;
        else if (action == "second") request.action = Action::SecondFromPlayer;
        else if (action == "clear") request.action = Action::Clear;
        else if (action == "export") request.action = Action::Export;
        else return std::nullopt;
        auto const& draft = value.at("draft");
        if (!draft.at("includeEntities").is_boolean()) return std::nullopt;
        request.draft.includeEntities = draft.at("includeEntities").get<bool>();
        auto parsePoint = [](auto const& point) -> std::optional<Point> {
            if (point.is_null()) return std::nullopt;
            auto coordinate = [](auto const& number) {
                if (number.is_number_unsigned() && number.template get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) throw std::invalid_argument("coordinate range");
                if (!number.is_number_integer()) throw std::invalid_argument("coordinate");
                auto const n = number.template get<std::int64_t>();
                if (n < std::numeric_limits<int>::min() || n > std::numeric_limits<int>::max())
                    throw std::invalid_argument("coordinate range");
                return static_cast<int>(n);
            };
            return Point{coordinate(point.at("x")), coordinate(point.at("y")), coordinate(point.at("z"))};
        };
        request.draft.first = parsePoint(draft.at("first"));
        request.draft.second = parsePoint(draft.at("second"));
        if (request.action == Action::Export && (!request.draft.first || !request.draft.second)) return std::nullopt;
        return request;
    } catch (std::exception const&) { return std::nullopt; }
}
}
