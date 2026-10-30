#include "app/TaskActions.h"
#include "app/TaskContext.h"
#include "structure/StructureFileLoader.h"
#include "structure/IndependentTaskFiles.h"
#include "structure/MaterialTracker.h"
#include "tasks/TaskStore.h"
#include <cstdio>
#include <algorithm>
#include <array>
#include <unordered_map>
#include <utility>

namespace {
std::filesystem::path taskPath;
bool world{}, inFlight{};
bool rejectNextStart{};
std::uint64_t worldRevision{1};
lholo::structure::detail::StructureLoadOrigin playerOrigin{10, 20, 30, 0};
std::shared_ptr<lholo::structure::detail::CompositionLoadResult> requested, completed;
std::vector<std::string> requestedPaths;
std::unordered_map<std::string, std::shared_ptr<lholo::structure::LoadedStructure const>> cache;
}
// Only world/file I/O boundaries are replaced. The real control frame, session,
// composer, catalog rules and asynchronous store writer run in this test.
namespace lholo::app::detail {
std::filesystem::path taskStorePath() { return taskPath; }
std::uint64_t taskWorldRevision() { return worldRevision; }
std::optional<structure::detail::StructureLoadOrigin> taskPlayerOrigin() {
    if (!world) return {};
    return playerOrigin;
}
}
namespace lholo::structure {
void clear() { detail::StructureSession::getInstance().clearLoaded({}); }
}
namespace lholo::structure::detail {
void invalidateMaterialList() {}
bool isStructureFileLoading() { return inFlight; }
std::shared_ptr<LoadedStructure const> cachedTaskFile(std::string const& path) {
    auto it = cache.find(path);
    return it == cache.end() ? nullptr : it->second;
}
bool startTaskComposition(std::string id, std::uint64_t revision, StructureLoadOrigin origin,
    std::vector<CompositionFile> files, std::vector<std::string> paths, bool) {
    if (inFlight) return false;
    if (std::exchange(rejectNextStart,false)) return false;
    requested = std::make_shared<CompositionLoadResult>();
    requested->request = StructureSession::getInstance().beginCompositionLoad(origin);
    requested->revision = revision;
    requested->taskId = std::move(id);
    requested->files = std::move(files);
    requestedPaths = std::move(paths);
    inFlight = true;
    return true;
}
std::shared_ptr<CompositionLoadResult> takeTaskCompositionResult() {
    return std::exchange(completed, {});
}
}

int runStructureWriteActionTests(lholo::structure::detail::StructureLoadOrigin&);
int runTaskActionsTests() {
    using namespace lholo;
    using namespace app;
    int failures{};
    auto check = [&](bool ok, char const* message) {
        if (!ok) { ++failures; std::fprintf(stderr, "TaskActionsTests: %s\n", message); }
    };
    auto directory = std::filesystem::temp_directory_path() / tasks::newId();
    taskPath = directory / "tasks.json";
    world = true;
    auto& session = structure::detail::StructureSession::getInstance();
    session.clearLoaded({});
    loadTasks();
    auto command = [&](TaskAction action, std::string value = {}, std::string file = {}, std::string task = {}) {
        auto s = getTaskSnapshot();
        TaskIntent intent{s.ack + 1, s.revision, action, task.empty() ? s.catalog.selectedTaskId : task, file, value};
        check(submitTaskIntent(intent), "queue task action");
        processTaskActions();
        check(getTaskStatus().accepted, "action accepted by real control frame");
    };
    auto finish = [&](bool success, int height = 1, int width = 1, std::size_t materials = 0) {
        check(inFlight && requested != nullptr, "composition was scheduled");
        if (!requested) return;
        if (success) {
            for (std::size_t i = 0; i < requested->files.size(); ++i) {
                auto& file = requested->files[i];
                auto loaded = std::make_shared<structure::LoadedStructure>();
                loaded->sizeX = loaded->sizeY = loaded->sizeZ = 1;
                loaded->sizeY = height;
                loaded->sizeX = width;
                loaded->materialCount = materials;
                loaded->sourcePath = requestedPaths[i];
                loaded->renderBlocks.push_back({0, 0, 0, reinterpret_cast<Block const*>(0x1000)});
                file.loaded = loaded;
                cache[requestedPaths[i]] = loaded;
            }
            structure::normalizeCompositionLayers(requested->files);
            requested->composition = structure::composeTaskProjection(requested->files,
                [](Block const* b, int, int) { return b; });
        } else {
            requested->error = "Missing file";
            requested->errorFileId = requested->files.back().id;
        }
        inFlight = false;
        completed = std::move(requested);
        processTaskActions();
    };
    command(TaskAction::Create, "城堡"); finish(true);
    command(TaskAction::AddFile, "body.mcstructure");
    auto addedFile = getTaskSnapshot().catalog.tasks[0].files.back();
    check(addedFile.dimension == 0 && addedFile.x == 10 && addedFile.y == 20 && addedFile.z == 30,
        "adding a file immediately records player coordinates and dimension before parsing finishes");
    playerOrigin = {900, 80, 700, 0};
    finish(true);
    auto loadedFile = getTaskSnapshot().catalog.tasks[0].files.back();
    check(loadedFile.dimension == addedFile.dimension && loadedFile.x == addedFile.x
        && loadedFile.y == addedFile.y && loadedFile.z == addedFile.z,
        "moving during first load cannot replace the addition-time anchor");
    playerOrigin = {10, 20, 30, 0};
    failures += runStructureWriteActionTests(playerOrigin);
    auto fileId = getTaskSnapshot().catalog.tasks[0].files[0].id;
    auto original = session.loaded();
    command(TaskAction::Path, "missing.mcstructure", fileId); finish(false);
    check(session.loaded() == original && session.lastPath() == "body.mcstructure",
          "failed path change preserves projection and binds the last successful editor");
    check(getTaskSnapshot().catalog.tasks[0].files[0].path == "missing.mcstructure",
          "failed path remains available for user repair");
    session.adjustOffsets(3, 0, 0);
    processTaskActions();
    check(inFlight && requestedPaths == std::vector<std::string>{"body.mcstructure"},
          "hotkey after failure recomposes the actual active files, not the invalid catalog");
    check(requested && requested->files[0].x == 13, "live world coordinate carried to composition");
    finish(true);
    check(session.transform().offsetX == 13 && session.lastPath() == "body.mcstructure",
          "successful live edit keeps original source binding");
    check(getTaskSnapshot().catalog.tasks[0].files[0].x == 13 &&
          getTaskSnapshot().catalog.tasks[0].files[0].path == "missing.mcstructure",
          "persist transform by stable ID without replacing the user's missing path");
    command(TaskAction::Path, "repaired.mcstructure", fileId); finish(true);
    check(session.lastPath() == "repaired.mcstructure" && getTaskStatus().error.empty(),
          "repair publishes catalog and clears loading error");
    session.adjustOffsets(100, 0, 0); processTaskActions(); finish(false);
    check(session.transform().offsetX == 13,
          "failed live transform restores the last successful transform, not the failed request");
    command(TaskAction::AddFile, "missing-two.mcstructure");
    auto failedFileAnchor = getTaskSnapshot().catalog.tasks[0].files.back();
    finish(false);
    auto failedFile = getTaskSnapshot().catalog.tasks[0].files.back();
    check(failedFile.dimension == failedFileAnchor.dimension && failedFile.dimension.has_value()
        && failedFile.x == failedFileAnchor.x && failedFile.y == failedFileAnchor.y && failedFile.z == failedFileAnchor.z,
        "failed parsing preserves the anchor registered when adding the file");
    check(session.selectedSource() < 0, "failed new file does not edit the previously selected file");
    command(TaskAction::SelectFile, {}, fileId);
    session.adjustOffsets(0, 2, 0); processTaskActions();
    check(inFlight && requested->files.size() == 1 && requested->files[0].y == 22,
          "existing file remains editable after another file fails to attach");
    finish(true);
    // Compare actual file/world positions, not just the normalized composite
    // origin: reopening also loads a catalog file absent from the old result.
    auto fileWorldPosition = [&](std::string const& id) -> std::optional<std::array<long long, 3>> {
        auto const activation = session.activation();
        if (!activation.loaded || !activation.origin) return {};
        auto const& loaded = *activation.loaded;
        auto source = std::find(loaded.sourceFiles.begin(), loaded.sourceFiles.end(), id);
        if (source == loaded.sourceFiles.end()) return {};
        auto index = static_cast<int>(source - loaded.sourceFiles.begin());
        if (auto const* file=structure::independentTaskFile(loaded,index); file && file->loaded && file->visible) {
            return std::array<long long,3>{static_cast<long long>(activation.origin->x)+file->x,
                static_cast<long long>(activation.origin->y)+file->y,
                static_cast<long long>(activation.origin->z)+file->z};
        }
        for (auto const& block : loaded.renderBlocks) if (block.sourceIndex == index)
            return std::array<long long, 3>{static_cast<long long>(activation.origin->x) + block.x,
                static_cast<long long>(activation.origin->y) + block.y,
                static_cast<long long>(activation.origin->z) + block.z};
        return {};
    };
    auto const originalTask = getTaskSnapshot().catalog.selectedTaskId;
    auto const originalPosition = fileWorldPosition(fileId);
    command(TaskAction::Close);
    playerOrigin = {100, 70, -200, 0};
    command(TaskAction::Activate);
    check(requested->request.origin == structure::detail::StructureLoadOrigin{0,0,0,0},
          "absolute files use zero base in the current dimension");
    finish(true);
    check(originalPosition && fileWorldPosition(fileId) == originalPosition,
          "reopen preserves file world position even when normalization changes");
    command(TaskAction::Close);
    ++worldRevision;
    playerOrigin = {200, 80, 300, 0};
    command(TaskAction::Activate);
    check(requested->request.origin == structure::detail::StructureLoadOrigin{0,0,0,0},
        "world exit clears native resources without replacing saved file coordinates");
    finish(true);
    auto const newWorldPosition = fileWorldPosition(fileId);
    check(newWorldPosition==originalPosition,"another world uses the same coordinates by user preference");
    // A live dimension switch filters old files, rather than moving them into
    // the new dimension or requiring a new task.
    playerOrigin = {500, 90, 600, 1};
    processTaskActions();
    check(inFlight && requested->request.origin==structure::detail::StructureLoadOrigin{0,0,0,1},
        "dimension switch schedules a view in the new dimension");
    check(std::none_of(requested->files.begin(),requested->files.end(),[](auto const& f){return f.visible;}),
        "overworld files are excluded before composition in the Nether");
    finish(true);
    check(!fileWorldPosition(fileId) && session.selectedSource()<0,"other-dimension file cannot render or be edited");
    command(TaskAction::AddFile,"nether.mcstructure"); finish(true);
    auto netherId=getTaskSnapshot().catalog.tasks[0].selectedFileId;
    check(fileWorldPosition(netherId)==std::optional<std::array<long long,3>>{{500,90,600}},
        "new file binds directly to current player dimension and world coordinate");
    check(getTaskSnapshot().catalog.tasks[0].files.back().dimension==1,"file dimension persisted independently");
    auto netherPosition=fileWorldPosition(netherId);
    playerOrigin={13,22,30,2};
    processTaskActions(); finish(true);
    command(TaskAction::AddFile,"end.mcstructure"); finish(true);
    auto endId=getTaskSnapshot().catalog.tasks[0].selectedFileId;
    check(fileWorldPosition(endId)==originalPosition && !fileWorldPosition(fileId)
        && !fileWorldPosition(netherId),"same XYZ in the End is isolated from other dimensions");
    check(session.loaded()->independentFiles!=nullptr,
        "cross-dimension AABBs do not cause task merging");
    playerOrigin = {700, 100, 800, 0};
    processTaskActions();
    finish(true);
    check(fileWorldPosition(fileId)==originalPosition && !fileWorldPosition(netherId),
        "returning to Overworld restores only its files without coordinate conversion");
    command(TaskAction::SelectFile,{},fileId);
    command(TaskAction::Create, "第二个任务"); finish(true);
    auto const otherTask = getTaskSnapshot().catalog.selectedTaskId;
    command(TaskAction::Select, {}, {}, originalTask);
    check(requested->request.origin == structure::detail::StructureLoadOrigin{0,0,0,0},
          "task switching retains absolute placements");
    finish(true);
    command(TaskAction::Select, {}, {}, otherTask);
    check(requested->request.origin == structure::detail::StructureLoadOrigin{0,0,0,0},
        "every task uses the same world coordinate system");
    finish(true);
    command(TaskAction::Select, {}, {}, originalTask); finish(true);
    command(TaskAction::Activate);
    command(TaskAction::Close);
    auto closed = session.loaded();
    finish(true);
    check(!closed && !session.loaded() && getTaskStatus().activeTaskId.empty(),
          "late composition cannot reopen a closed task");
    command(TaskAction::Activate);
    session.clearLoaded({}); world = false;
    finish(true);
    check(!session.loaded() && getTaskStatus().activeTaskId.empty(),
          "world exit rejects pending first activation");
    shutdownTasks();
    loadTasks();
    world=true;
    playerOrigin={-900,60,-1000,0};
    command(TaskAction::Activate); finish(true);
    check(fileWorldPosition(fileId)==originalPosition,"restart restores absolute file coordinates from tasks.json");
    playerOrigin={-400,55,200,1};
    processTaskActions(); finish(true);
    check(fileWorldPosition(netherId)==netherPosition && !fileWorldPosition(fileId),
        "restart preserves independent Nether coordinates without 8:1 conversion");
    auto expected = getTaskSnapshot().catalog;
    shutdownTasks();
    check(tasks::readCatalog(taskPath) == expected, "mod shutdown flushes accepted task edits");
    tasks::Catalog fresh;
    tasks::Task freshTask{tasks::newId(),"新文件首次放置",{}, {}};
    tasks::File freshFile{tasks::newId(),"fresh.mcstructure"};
    freshTask.selectedFileId=freshFile.id;
    freshTask.files.push_back(freshFile);
    fresh.selectedTaskId=freshTask.id;
    fresh.tasks.push_back(freshTask);
    tasks::writeCatalog(taskPath,fresh);
    loadTasks();
    playerOrigin={100,50,200,0};
    command(TaskAction::Activate); finish(false);
    check(!getTaskSnapshot().catalog.tasks[0].files[0].dimension
        && getTaskSnapshot().catalog.tasks[0].files[0].x==0,
        "failed first placement leaves the new file unbound");
    playerOrigin={200,60,300,0};
    rejectNextStart=true;
    command(TaskAction::Activate);
    check(!inFlight,"busy loader leaves first placement waiting without binding coordinates");
    playerOrigin={-300,70,400,1};
    processTaskActions(); finish(true);
    check(fileWorldPosition(freshFile.id)==std::optional<std::array<long long,3>>{{-300,70,400}},
        "retried first placement captures the position when the loader accepts the request");
    auto placed=getTaskSnapshot().catalog.tasks[0].files[0];
    check(placed.dimension==1 && placed.x==-300 && placed.y==70 && placed.z==400,
        "loader retry captures a dimension change and commits it with absolute XYZ");
    session.setLayerAxis(structure::LayerAxis::Y);
    session.setLayerDisplayMode(structure::LayerDisplayMode::Single);
    session.setDisplayLayer(88);
    session.setRotation(1);
    session.setMirror(2);
    processTaskActions(); finish(true, 100, 40, 4);
    check(session.layerSettings().values.layer == 88, "large source accepts layer 88");
    command(TaskAction::Path, "short.mcstructure", freshFile.id); finish(true, 34, 8, 2);
    auto corrected = getTaskSnapshot().catalog.tasks[0].files[0];
    auto layers = session.layerSettings();
    check(corrected.slice == structure::LayerSettings{structure::LayerAxis::Y, structure::LayerDisplayMode::Single, 33}
        && layers.values == corrected.slice && layers.maximum[0] == 33,
        "path replacement publishes one corrected layer to catalog, editor and render settings");
    check(corrected.dimension == placed.dimension && corrected.x == placed.x && corrected.y == placed.y
        && corrected.z == placed.z && corrected.rotation == 90 && corrected.mirror == "z",
        "path replacement preserves absolute position, dimension, rotation and mirror");
    check(session.loaded()->independentFiles->files[0].slice == corrected.slice,
        "independent render snapshot uses the normalized source layer");
    auto goodProjection = session.loaded();
    command(TaskAction::Path, "unreadable.mcstructure", freshFile.id); finish(false);
    check(session.loaded() == goodProjection && session.layerSettings().values == corrected.slice
        && getTaskSnapshot().catalog.tasks[0].files[0].slice == corrected.slice,
        "failed replacement keeps the last valid projection and layer settings");
    command(TaskAction::Path, "taller.mcstructure", freshFile.id); finish(true, 100, 40, 4);
    check(session.layerSettings().values.layer == 33, "replacement with taller file preserves an in-range layer");
    command(TaskAction::Close);
    playerOrigin = {321, 64, -654, 0};
    command(TaskAction::AddFile, "added-while-closed.mcstructure");
    auto closedAddition = getTaskSnapshot().catalog.tasks[0].files.back();
    check(!inFlight && closedAddition.dimension == 0 && closedAddition.x == 321
        && closedAddition.y == 64 && closedAddition.z == -654,
        "adding to an inactive task registers its anchor without loading");
    playerOrigin = {999, 70, 888, 0};
    rejectNextStart = true;
    command(TaskAction::Activate);
    check(!inFlight, "registered file can wait for the loader");
    processTaskActions(); finish(false);
    auto afterFailure = getTaskSnapshot().catalog.tasks[0].files.back();
    check(afterFailure.dimension == closedAddition.dimension && afterFailure.x == closedAddition.x
        && afterFailure.y == closedAddition.y && afterFailure.z == closedAddition.z,
        "loader delay and parsing failure do not change the addition-time anchor");
    command(TaskAction::Activate); finish(true);
    check(fileWorldPosition(closedAddition.id) == std::optional<std::array<long long,3>>{{321,64,-654}},
        "later activation uses the registered anchor instead of the current player position");

    command(TaskAction::Close);
    world = false;
    command(TaskAction::AddFile, "added-without-player.mcstructure");
    auto noWorldFile = getTaskSnapshot().catalog.tasks[0].files.back();
    check(!noWorldFile.dimension, "adding without player coordinates leaves the file unlocated");
    world = true;
    playerOrigin = {-50, 65, 80, 0};
    command(TaskAction::Activate); finish(true);
    auto located = getTaskSnapshot().catalog.tasks[0].files.back();
    check(located.dimension == 0 && located.x == -50 && located.y == 65 && located.z == 80,
        "an unlocated file fetches player coordinates when loading becomes possible");
    auto expectedLayers = getTaskSnapshot().catalog;
    shutdownTasks();
    check(tasks::readCatalog(taskPath) == expectedLayers, "corrected layers survive actual task-store persistence");
    std::filesystem::remove(taskPath);
    std::filesystem::remove(directory);
    requested.reset(); completed.reset(); cache.clear(); inFlight = false;
    std::printf("TaskActionsTests: %d failures\n", failures);
    return failures;
}
