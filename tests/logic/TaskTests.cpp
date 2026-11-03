#include "TaskComposerOracle.h"
#include "nlohmann/json.hpp"
#include "oreui/TaskProtocol.h"
#include "structure/IndependentTaskFiles.h"
#include "structure/StructureSession.h"
#include "structure/TaskComposer.h"
#include "structure/TaskProjectionView.h"
#include "tasks/TaskCatalog.h"
#include "tasks/TaskStore.h"
#include "tasks/TaskStoreWriter.h"
#include <cstdio>
#include <fstream>
#include <set>

int runTaskTests() {
    using namespace lholo;
    int failures{};
    auto check = [&](bool value, char const* name) {
        if (!value) {
            ++failures;
            std::printf("TaskTests FAILED: %s\n", name);
        }
    };
    auto* stone = reinterpret_cast<Block const*>(0x1000);
    auto* glass = reinterpret_cast<Block const*>(0x2000);
    auto* water = reinterpret_cast<Block const*>(0x3000);
    {
        auto source = std::make_shared<structure::LoadedStructure>();
        source->sizeX = 12; source->sizeY = 34; source->sizeZ = 7;
        source->materialCount = 4;
        for (auto const axis : {structure::LayerAxis::Y, structure::LayerAxis::X, structure::LayerAxis::Material}) {
            auto const maximum = axis == structure::LayerAxis::Y ? 33 : axis == structure::LayerAxis::X ? 11 : 3;
            for (auto const mode : {structure::LayerDisplayMode::All, structure::LayerDisplayMode::Single,
                                   structure::LayerDisplayMode::UpToCurrent, structure::LayerDisplayMode::FromCurrent}) {
                std::vector<structure::CompositionFile> slices{{"loaded", source, true, 10, 20, 30, 1, 2,
                    {axis, mode, 88}}, {"unloaded", {}, false, 0, 0, 0, 0, 0, {axis, mode, 88}}};
                structure::normalizeCompositionLayers(slices);
                check(slices[0].slice == structure::LayerSettings{axis, mode, maximum},
                    "loaded layer clamps to the selected source axis without changing display mode");
                check(slices[0].x == 10 && slices[0].y == 20 && slices[0].z == 30
                    && slices[0].rotation == 1 && slices[0].mirror == 2,
                    "normalizing source slices preserves placement and transforms");
                check(slices[1].slice.layer == 88, "unloaded file settings wait for actual dimensions");
            }
        }
        std::vector<structure::CompositionFile> slices{{"valid", source, true, 0, 0, 0, 0, 0,
            {structure::LayerAxis::Y, structure::LayerDisplayMode::Single, 20}}};
        structure::normalizeCompositionLayers(slices);
        check(slices[0].slice.layer == 20, "in-range layers remain unchanged");
        source->materialCount = 0;
        slices[0].slice = {structure::LayerAxis::Material, structure::LayerDisplayMode::Single, 88};
        structure::normalizeCompositionLayers(slices);
        check(slices[0].slice.layer == 0, "empty material palette has a safe zero layer");
        source->materialCount = 4;
        source->renderBlocks = {{0, 33, 0, stone}};
        source->regions = {{0, 0, 0, 12, 34, 7}};
        slices[0].slice = {structure::LayerAxis::Y, structure::LayerDisplayMode::Single, 88};
        structure::normalizeCompositionLayers(slices);
        auto independent = structure::composeTaskProjection(slices);
        check(independent.loaded->independentFiles
            && independent.loaded->independentFiles->files[0].slice.layer == 33,
            "independent projection receives the corrected layer before publication");
        auto empty = std::make_shared<structure::LoadedStructure>();
        empty->sizeX = empty->sizeY = empty->sizeZ = 1;
        slices.push_back({"overlap", empty, true});
        auto composed = structure::composeTaskProjection(slices);
        structure::TaskProjectionView view(slices);
        auto owner = view.ownerAt(0, {0, 33, 0});
        check(composed.loaded->independentFiles && composed.loaded->renderBlocks.empty() && owner && owner->file == 0 &&
                  owner->index == 0,
              "overlapping independent view keeps the final valid layer");
    }
    auto first = std::make_shared<structure::LoadedStructure>();
    first->sizeX = 3;
    first->sizeY = 2;
    first->sizeZ = 2;
    first->regions.push_back({0, 0, 0, 3, 2, 2});
    first->renderBlocks = {
        {0, 0, 0, stone, nullptr, {}, 0, -1}, {1, 0, 0, nullptr, nullptr}, {2, 1, 1, glass, water, {}, 1, 2}};
    first->materialCount = 3;
    auto nbtOwner = std::make_shared<int>(42);
    first->renderBlocks.mutableValues().back().blockEntityNbt =
        std::shared_ptr<CompoundTag const>(nbtOwner, reinterpret_cast<CompoundTag const*>(nbtOwner.get()));
    auto second = std::make_shared<structure::LoadedStructure>(*first);
    second->renderBlocks = {{0, 0, 0, glass, nullptr, {}, 0, -1}, {1, 0, 0, stone, nullptr, {}, 1, -1}};
    std::vector<structure::CompositionFile> files{{"one", first, true}, {"two", second, true}};
    int transformed{};
    auto copy = [&](Block const* p, int, int) {
        if (p)
            ++transformed;
        return p;
    };
    auto result = structure::composeTask(files, copy);
    auto boundsInTask = [](structure::OracleComposition const &composition, std::size_t index) {
        auto const& bounds = composition.loaded->sourceBounds.at(index);
        return std::array<long long, 6>{bounds.minimum[0] + composition.origin[0],
            bounds.minimum[1] + composition.origin[1], bounds.minimum[2] + composition.origin[2],
            bounds.maximum[0] + composition.origin[0], bounds.maximum[1] + composition.origin[1],
            bounds.maximum[2] + composition.origin[2]};
    };
    check(result.loaded->sourceBounds.size() == 2,
          "overlapping visible files each retain a full-file border");
    check(boundsInTask(result, 0) == std::array<long long, 6>{0, 0, 0, 3, 2, 2}
              && boundsInTask(result, 1) == std::array<long long, 6>{0, 0, 0, 3, 2, 2},
          "file borders use full file dimensions rather than the winning cells");
    check(result.loaded->renderBlocks.size() == 3, "overlap deduplicated and upper air reveals lower solid");
    check(result.loaded->renderBlocks.front().block == stone &&
              result.loaded->renderBlocks.front().sourceIndex == 0,
          "first file wins");
    check(result.loaded->renderBlocks[1].sourceIndex == 1, "lower file source preserved");
    check(result.loaded->renderBlocks.back().liquid == water, "winner retains liquid");
    check(result.loaded->renderBlocks.back().blockEntityNbt == first->renderBlocks.back().blockEntityNbt,
          "winner retains immutable block entity data");
    check(structure::coveringSource(*result.loaded, 1, 1, 1) == 0, "upper coverage owns schematic air");
    auto generation = result.loaded->generation;
    std::swap(files[0], files[1]);
    auto reordered = structure::composeTask(files, copy);
    check(reordered.loaded->renderBlocks.front().block == glass &&
              reordered.loaded->sourceFiles.front() == "two",
          "reordering changes winner and source identity together");
    std::swap(files[0], files[1]);
    files[0].visible = false;
    result = structure::composeTask(files, copy);
    check(result.loaded->sourceBounds.size() == 1 && result.loaded->sourceBounds[0].sourceIndex == 1,
          "hidden files have no border, independently of selected source");
    check(result.loaded->renderBlocks.front().block == glass &&
              result.loaded->renderBlocks.front().sourceIndex == 1,
          "hidden upper file reveals lower");
    check(result.loaded->generation != generation, "each composition publishes a new generation");
    files[0].visible = true;
    files[0].slice = {structure::LayerAxis::Y, structure::LayerDisplayMode::Single, 1};
    result = structure::composeTask(files, copy);
    check(result.loaded->renderBlocks.front().block == glass, "slice filtering precedes priority");
    check(boundsInTask(result, 0) == std::array<long long, 6>{0, 0, 0, 3, 2, 2},
          "slice filtering does not shrink the full-file border");
    files[1].visible = false;
    files[0].slice = {};
    files[0].x = -8;
    files[0].y = 5;
    files[0].rotation = 1;
    files[0].mirror = 1;
    result = structure::composeTask(files, copy);
    check(result.origin == std::array<int, 3>{-8, 5, 0}, "negative offsets preserve task origin");
    check(result.loaded->sizeX == 2 && result.loaded->sizeZ == 3, "rotation swaps footprint");
    check(boundsInTask(result, 0) == std::array<long long, 6>{-8, 5, 0, -6, 7, 3},
          "file border follows negative offset, rotation and mirror");
    for (auto const& b : result.loaded->renderBlocks)
        check(b.x >= 0 && b.y >= 0 && b.z >= 0, "normalized coordinates");
    files[1].visible = true;
    files[1].x = 20;
    result = structure::composeTask(files, copy);
    check(boundsInTask(result, 0) == std::array<long long, 6>{-8, 5, 0, -6, 7, 3}
              && boundsInTask(result, 1) == std::array<long long, 6>{20, 0, 0, 23, 2, 2},
          "normalization preserves each file's separate task-space border");
    for (auto const& b : result.loaded->renderBlocks)
        if (b.sourceIndex == 1 && b.block == glass)
            check(b.x + result.origin[0] == 20 && b.y + result.origin[1] == 0,
                  "normalization does not move the other file in task space");
    check(structure::coveringSource(*result.loaded, 15, 0, 0) == -1, "gap between files is outside coverage");
    files[0].visible = files[1].visible = false;
    result = structure::composeTask(files, copy);
    check(result.loaded->composed && result.loaded->renderBlocks.empty() && result.loaded->regions.empty(),
          "all files hidden produces empty projection");
    check(result.loaded->sourceFiles.size() == 2, "hidden files keep stable source identities");
    check(result.loaded->sourceBounds.empty(), "all hidden files produce no task-wide fallback border");

    {
        std::vector<structure::CompositionFile> identical{{"a", first, true}, {"b", first, true}};
        auto covered = structure::composeTask(identical, copy);
        check(covered.loaded->sourceBounds.size() == 2,
              "fully occluded visible files still have their own border");
        auto empty = std::make_shared<structure::LoadedStructure>();
        empty->sizeX = 11; empty->sizeY = 2; empty->sizeZ = 3;
        empty->regions = {{0, 0, 0, 1, 2, 3}, {10, 0, 0, 1, 2, 3}};
        identical = {{"air", empty, true}};
        auto air = structure::composeTask(identical, copy);
        check(air.loaded->renderBlocks.empty() && air.loaded->sourceBounds.size() == 1
                  && boundsInTask(air, 0) == std::array<long long, 6>{0, 0, 0, 11, 2, 3},
              "multi-region air file has one whole-file border across region gaps");
        identical[0].slice = {structure::LayerAxis::Material, structure::LayerDisplayMode::Single, 99};
        auto slicedAway = structure::composeTask(identical, copy);
        check(slicedAway.loaded->renderBlocks.empty() && slicedAway.loaded->regions.empty()
                  && slicedAway.loaded->sourceBounds.size() == 1,
              "fully filtered file retains its border without contributing correction air coverage");
    }

    tasks::Catalog catalog;
    tasks::Intent edit;
    edit.action = tasks::Action::Create;
    edit.value = "城堡 / 城 / Castle 🏰";
    check(tasks::applyCatalogEdit(catalog, edit), "create multilingual task");
    auto taskId = catalog.selectedTaskId;
    edit.taskId = taskId;
    edit.action = tasks::Action::AddFile;
    edit.value = "D:\\投影\\主体.mcstructure";
    check(tasks::applyCatalogEdit(catalog, edit), "attach external path");
    auto fileId = catalog.tasks[0].selectedFileId;
    edit.value = "D:\\投影\\屋顶.litematic";
    tasks::applyCatalogEdit(catalog, edit);
    auto roofId = catalog.tasks[0].selectedFileId;
    edit.action = tasks::Action::Move;
    edit.fileId = roofId;
    edit.delta = -1;
    check(tasks::applyCatalogEdit(catalog, edit) && catalog.tasks[0].files[0].id == roofId,
          "array order is priority; IDs unchanged");
    auto& file = catalog.tasks[0].files[0];
    file.rotation = 270;
    file.mirror = "z";
    file.x = -5;
    file.slice = {structure::LayerAxis::X, structure::LayerDisplayMode::FromCurrent, 2};
    file.dimension=1;
    auto directory = std::filesystem::temp_directory_path() / tasks::newId();
    auto path = directory / "tasks.json";
    tasks::writeCatalog(path, catalog);
    check(tasks::readCatalog(path) == catalog, "UTF-8 and per-file settings round trip");
    {
        nlohmann::json unsupported;
        { std::ifstream in(path); in>>unsupported; }
        unsupported["version"]=1;
        { std::ofstream out(path); out<<unsupported.dump(); }
        bool unsupportedRejected{};
        try { tasks::readCatalog(path); } catch (...) { unsupportedRejected=true; }
        check(unsupportedRejected,"unsupported task format is rejected without coordinate migration");
        tasks::writeCatalog(path,catalog);
        nlohmann::json invalid;
        { std::ifstream in(path); in>>invalid; }
        invalid["tasks"][0]["files"][0]["position"]["x"]=4294967295ULL;
        { std::ofstream out(path); out<<invalid.dump(); }
        bool overflowRejected{};
        try { tasks::readCatalog(path); } catch (...) { overflowRejected=true; }
        check(overflowRejected,"v2 world coordinate overflow rejected rather than wrapped");
        invalid["tasks"][0]["files"][0]["position"]["x"]=file.x;
        invalid["tasks"][0]["files"][0]["dimension"]=1.5;
        { std::ofstream out(path); out<<invalid.dump(); }
        bool dimensionRejected{};
        try { tasks::readCatalog(path); } catch (...) { dimensionRejected=true; }
        check(dimensionRejected,"dimension ID must be an integer");
        tasks::writeCatalog(path,catalog);
    }
    {
        tasks::TaskStoreWriter writer(path);
        writer.queue(catalog);
        auto latest = catalog;
        latest.tasks[0].name = "最后一次修改";
        writer.queue(latest);
        writer.finish();
        check(tasks::readCatalog(path) == latest, "shutdown flushes the latest queued catalog");
        check(writer.takeResult() == std::optional<std::string>{""}, "successful async save reported");
    }
    {
        auto blocked = directory / "not-a-directory";
        std::ofstream(blocked).put('x');
        tasks::TaskStoreWriter writer(blocked / "tasks.json");
        writer.queue(catalog);
        writer.finish();
        auto error = writer.takeResult();
        check(error && !error->empty(), "async write failure reported without overwriting existing file");
        std::filesystem::remove(blocked);
    }
    check(!std::filesystem::exists(directory / "tasks.json.tmp"), "atomic save leaves no temporary file");
    auto duplicate = catalog;
    duplicate.tasks[0].files[1].id = roofId;
    tasks::writeCatalog(path, duplicate);
    bool rejected{};
    try {
        tasks::readCatalog(path);
    } catch (std::exception const&) {
        rejected = true;
    }
    check(rejected && std::filesystem::exists(path), "duplicate IDs rejected without deleting data");
    std::filesystem::remove(path);
    std::filesystem::remove(directory);
    edit.action = tasks::Action::Delete;
    edit.taskId = taskId;
    check(tasks::applyCatalogEdit(catalog, edit) && catalog.tasks.empty() && catalog.selectedTaskId.empty(),
          "last task may be deleted");
    auto intent = oreui::parseTaskIntent(
        R"({"id":1,"revision":2,"action":"move","taskId":"task","fileId":"file","delta":-1})");
    check(intent && intent->delta == -1 && intent->action == tasks::Action::Move, "native task protocol");
    check(!oreui::parseTaskIntent(R"({"id":1,"revision":2,"action":"unknown"})"), "unknown action rejected");
    nlohmann::json longPath{{"id", 2}, {"revision", 2}, {"action", "path"},
                           {"value", std::string(oreui::kTaskValueMaxBytes, 'x')}};
    check(oreui::parseTaskIntent(longPath.dump()).has_value(), "long path accepted within task value boundary");
    longPath["value"] = std::string(oreui::kTaskValueMaxBytes + 1, 'x');
    check(!oreui::parseTaskIntent(longPath.dump()), "oversized task value rejected");

    auto& session = structure::detail::StructureSession::getInstance();
    structure::detail::SavedProjectionSnapshot saved{true, 1, 2, 3, {}, "legacy.mcstructure"};
    session.setSavedProjection(saved);
    auto request = session.beginFileLoad({1, 2, 3, 0});
    check(session.completeCompositionLoad(request, result.loaded, {1, 2, 3, 0}),
          "publish empty task snapshot");
    check(session.selectedSourceFor(*result.loaded) == -2,
          "new generation has no stale selected source before editor binding");
    session.selectCompositionFile(first, {1, 2, 8, 9, 10}, 0, "one.mcstructure");
    session.recordProjectionAnchor(80, 90, 100);
    session.refreshSavedTransformIfActive();
    check(session.selectedSourceFor(*result.loaded) == 0 && session.selectedSourceFor(*first) == -2,
          "selection is tied to the published projection snapshot");
    check(session.savedProjection().structurePath == saved.structurePath &&
              session.savedProjection().anchorX == 1,
          "task never rewrites single-file restore record");
    auto stale = session.beginFileLoad({});
    session.clearLoaded(i18n::Message{i18n::TextKey::StatusWorldExited});
    check(!session.completeCompositionLoad(stale, result.loaded, {}), "world exit rejects stale composition");
    auto const clearedSelection = session.snapshot();
    check(!clearedSelection.editor && clearedSelection.selectedSource == -1,
          "world exit clears file editor target");
    std::printf("TaskTests: %d failures\n", failures);
    return failures;
}
