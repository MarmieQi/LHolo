#include "oreui/StructureWriteProtocol.h"
#include "structure/write/StructureWritePlan.h"
#include "structure/write/local/BedrockTemplateBuilder.h"
#include "structure/StructureTransform.h"
#include "nlohmann/json.hpp"
#include <cstdio>
#include <limits>

int runStructureWriteTests() {
    int failures{};
    auto check = [&](bool valid, char const* message) {
        if (!valid) { ++failures; std::fprintf(stderr, "World write: %s\n", message); }
    };
    using namespace lholo::structure::write;
    using lholo::oreui::parseStructureWriteIntent;
    check(nativeTemplateVolume({2, 3, 4}) == 24, "native template volume uses all axes");
    for (auto size : {std::array<int, 3>{0,1,1}, {-1,1,1},
        {std::numeric_limits<int>::max(), 2, 1},
        {std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), std::numeric_limits<int>::max()}}) {
        bool rejected{};
        try { nativeTemplateVolume(size); } catch (std::length_error const&) { rejected = true; }
        check(rejected, "invalid or overflowing NBT list dimensions rejected before allocation");
    }
    nlohmann::json request{{"id", 1}, {"revision", 8}, {"worldRevision", 3},
        {"taskId", "task"}, {"fileId", "file"}, {"mode", "local"}, {"skipAir", false}};
    check(parseStructureWriteIntent(request.dump()).has_value(), "local request accepted");
    check(!parseStructureWriteIntent(request.dump())->skipAir, "explicit false preserves air writing");
    auto skipRequest = request; skipRequest["skipAir"] = true;
    check(parseStructureWriteIntent(skipRequest.dump())->skipAir, "true enables air skipping");
    skipRequest["skipAir"] = 1;
    check(!parseStructureWriteIntent(skipRequest.dump()), "skip-air value must be boolean");
    skipRequest.erase("skipAir");
    check(!parseStructureWriteIntent(skipRequest.dump()), "request includes explicit skip-air choice");
    for (auto mode : {"commands", "server", "unknown"}) {
        auto bad = request; bad["mode"] = mode;
        check(!parseStructureWriteIntent(bad.dump()), "unimplemented modes rejected");
    }
    for (auto field : {"id", "revision", "worldRevision"}) {
        auto bad = request; bad[field] = -1;
        check(!parseStructureWriteIntent(bad.dump()), "negative context rejected");
        bad[field] = 9007199254740992ULL;
        check(!parseStructureWriteIntent(bad.dump()), "unsafe integer rejected");
    }
    auto bad = request; bad["fileId"] = "";
    check(!parseStructureWriteIntent(bad.dump()), "missing file rejected");
    bad = request; bad["x"] = 999;
    check(!parseStructureWriteIntent(bad.dump()), "client cannot override native placement");
    check(!parseStructureWriteIntent("{") && !parseStructureWriteIntent(std::string(4097, 'x')), "malformed requests rejected");

    // All mirror/rotation combinations must cover exactly the current projection footprint.
    for (int mirror = 0; mirror < 3; ++mirror) for (int rotation = 0; rotation < 4; ++rotation) {
        WriteTarget target{{-20, -60, 30}, 0, rotation, mirror};
        std::array<int, 3> maximum;
        check(writeBounds(target, {3, 2, 5}, maximum), "valid negative target");
        for (int x : {0, 2}) for (int z : {0, 4}) {
            auto cell = lholo::structure::transformCell(x, 1, z, 3, 5, mirror, rotation);
            for (int axis = 0; axis < 3; ++axis)
                check(cell[axis] + target.position[axis] >= target.position[axis]
                    && cell[axis] + target.position[axis] <= maximum[axis], "transformed corner in write bounds");
        }
    }
    std::array<int, 3> maximum;
    check(!writeBounds({{std::numeric_limits<int>::max(), 0, 0}, 0, 0, 0}, {2, 1, 1}, maximum), "coordinate overflow rejected");
    check(writeBounds({{std::numeric_limits<int>::min(), -64, 0}, 0, 0, 0}, {1, 1, 1}, maximum), "minimum coordinate accepted");

    StructureWritePlan plan;
    plan.size = {6, 1, 1};
    plan.regions = {{{0,0,0}, {2,1,1}}, {{3,0,0}, {3,1,1}}};
    plan.omittedCells = {4};
    plan.cells = {{{0,0,0}, 0, -1, {}}, {{3,0,0}, 1, 2, {}}, {{5,0,0}, -1, 2, {}}};
    auto indices = makeTemplateIndices(plan);
    check(indices.primary == std::vector<int>{1,0,-1,2,-1,3}, "solid, air, region gap, omission and liquid preserved");
    check(indices.secondary == std::vector<int>{-1,-1,-1,3,-1,-1}, "waterlogged secondary layer retained");
    plan.skipAir = true;
    auto skipped = makeTemplateIndices(plan);
    check(skipped.primary == std::vector<int>{1,-1,-1,2,-1,3}, "enabled mode skips schematic air and retains non-air cells");
    check(skipped.secondary == indices.secondary, "skipping air preserves waterlogged and pure liquid placement");
    plan.cells.clear();
    auto airOnly = makeTemplateIndices(plan);
    check(airOnly.primary == std::vector<int>(6, -1) && airOnly.secondary.empty(),
        "all-air structure skips every cell and has no extra layer");
    plan.cells = {{{0,0,0}, 0, -1, {}}, {{5,0,0}, -1, 2, {}}};
    auto dry = makeTemplateIndices(plan);
    check(dry.primary == std::vector<int>{1,-1,-1,-1,-1,3} && dry.secondary.empty(),
        "solid and standalone liquid use only the primary layer");
    return failures;
}
