// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "structure/StructureFileLoader.h"
#include "structure/ProjectionActions.h"
#include "diagnostics/StructurePreviewTrace.h"

#include "plugin/LHolo.h"
#include "structure/MaterialTracker.h"
#include "structure/StructureLoader.h"
#include "structure/StructurePaths.h"
#include "structure/StructureSession.h"
#include "structure/IndependentTaskFiles.h"
#include "structure/StructureUiState.h"
#include "structure/formats/StructureFormatLoaders.h"
#include "mc/world/level/block/states/VanillaBlockStateTransformUtils.h"
#include "mc/util/Mirror.h"
#include "mc/util/Rotation.h"
#include <unordered_map>
#include <limits>
#include <algorithm>
#include <atomic>
#include <stdexcept>

#include <chrono>
#include <exception>
#include <future>
#include <mutex>
#include <optional>
#include <utility>

#include <gsl/util>
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/Bedrock.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/LevelListener.h"

namespace lholo::structure::detail {
namespace {
std::atomic_uint64_t worldRevision{1};
// Native palette/template access remains serialized even though job ownership
// and UI busy state are independent for projection and read-only preview.
std::mutex nativeParserMutex;

struct FileLoader final : LevelListener {
    std::mutex mutex;
    std::future<void> task;
    Level* level{};
    bool stopped{true};
    std::shared_ptr<CompositionLoadResult> compositionResult;
    StructureReadQueue reader{[](std::string const& path,std::string& error,std::atomic_bool const& cancelled)
        -> std::shared_ptr<LoadedStructure const> {
        std::lock_guard parseLock(nativeParserMutex);
        if (cancelled.load()) return {};
        diagnostics::traceStructurePreview(fmt::format("parse begin path={}",path));
        std::shared_ptr<LoadedStructure const> loaded;
        try { loaded=loadStructureFile(pathFromUtf8(path),error); }
        catch (std::exception const& e) { error=e.what(); }
        catch (...) { error="Unknown structure read error"; }
        diagnostics::traceStructurePreview(fmt::format("parse end path={} success={} cancelled={} error={}",
            path,static_cast<bool>(loaded),cancelled.load(),error));
        return loaded;
    }};
    std::unordered_map<std::string, std::shared_ptr<LoadedStructure const>> cache;

    void onLevelDestruction(std::string const&) override {
        std::lock_guard lock(mutex);
        // StructureTemplate uses this world's non-owning unknown-block
        // registry. Stop publication and finish parsing BEFORE it is freed,
        // including a first load which has no active projection listener yet.
        level = nullptr;
        worldRevision.fetch_add(1, std::memory_order_release);
        resetProjectionActions();
        StructureSession::getInstance().clearLoaded(i18n::Message{i18n::TextKey::StatusWorldExited});
        reader.cancelAll();
        if (task.valid()) task.wait();
        reader.drain();
        compositionResult.reset(); cache.clear();
        // The dying Level removes its listeners; never remove through it later.
    }
};

FileLoader& fileLoader() {
    static FileLoader loader;
    return loader;
}

auto& logger() { return LHolo::getInstance().getSelf().getLogger(); }

bool startLoad(std::string path, StructureLoadOrigin origin, std::optional<SavedProjectionSnapshot> saved) {
    auto& loader = fileLoader();
    std::lock_guard lock(loader.mutex);
    if (loader.stopped) return false;
    if (loader.task.valid()) {
        if (loader.task.wait_for(std::chrono::seconds{0}) != std::future_status::ready) return false;
        loader.task.get();
    }

    loader.compositionResult.reset();
    loader.cache.clear();

    auto currentLevel = ll::service::getMultiPlayerLevel();
    if (!currentLevel) return false;
    if (loader.level != &*currentLevel) {
        if (loader.level) loader.level->removeListener(loader);
        currentLevel->addListener(loader);
        loader.level = &*currentLevel;
    }

    auto& session = StructureSession::getInstance();
    auto const request = session.beginFileLoad(origin);
    auto& ui = StructureUiState::getInstance();
    auto const hint = ui.beginLoadingHint();
    auto const failureKey = saved ? i18n::TextKey::StatusRestoreFailed : i18n::TextKey::StatusLoadFailed;
    try {
        loader.task = std::async(std::launch::async, [path = std::move(path), saved = std::move(saved), request, hint, failureKey] {
            std::lock_guard parseLock(nativeParserMutex);
            auto finishHint = gsl::finally([&] { StructureUiState::getInstance().finishLoadingHint(hint); });
            auto& session = StructureSession::getInstance();
            auto fail = [&](std::string error) {
                if (session.failFileLoad(request, i18n::Message{failureKey, {error}})) {
                    logger().error("Could not load structure {}: {}", path, error);
                }
            };
            try {
                std::string error;
                auto loaded = loadStructureFile(pathFromUtf8(path), error);
                if (!loaded) {
                    fail(std::move(error));
                    return;
                }
                auto const count = loaded->renderBlocks.size();
                auto const status = saved ? i18n::Message{i18n::TextKey::StatusRestoredPending}
                                          : makeLoadedStatusMessage(*loaded);
                bool const published = saved
                    ? session.completeRestoreLoad(request, std::move(loaded), *saved, status)
                    : session.completeFileLoad(request, std::move(loaded), path, status);
                if (!published) return;
                invalidateMaterialList();
                if (!saved) saveSettings();
                logger().info("Loaded structure {}: {} renderable blocks", path, count);
            } catch (std::exception const& error) {
                fail(error.what());
            } catch (...) {
                fail("Unknown exception while loading structure");
            }
        });
    } catch (std::exception const& error) {
        ui.finishLoadingHint(hint);
        session.failFileLoad(request, i18n::Message{failureKey, {error.what()}});
        logger().error("Could not start background structure load: {}", error.what());
        return false;
    }
    return true;
}

} // namespace

void enableStructureFileLoader() {
    auto& loader = fileLoader();
    std::lock_guard lock(loader.mutex);
    loader.stopped = false;
    worldRevision.fetch_add(1, std::memory_order_release);
}

std::uint64_t structureFileWorldRevision() { return worldRevision.load(std::memory_order_acquire); }

void shutdownStructureFileLoader() {
    auto& loader = fileLoader();
    std::lock_guard lock(loader.mutex);
    loader.stopped = true;
    loader.reader.cancelAll();
    StructureSession::getInstance().cancelPendingLoad();
    // Keep the task in the gate until it is joined: a concurrent destruction
    // callback must not mistake a moved-out future for a finished parser.
    // The parser never acquires loader.mutex.
    if (loader.task.valid()) loader.task.get();
    loader.reader.drain();
    loader.compositionResult.reset(); loader.cache.clear();
    if (loader.level) {
        loader.level->removeListener(loader);
        loader.level = nullptr;
    }
    StructureUiState::getInstance().clearLoadingHint();
}

bool startStructureFileLoad(std::string path, StructureLoadOrigin origin) {
    return startLoad(std::move(path), origin, std::nullopt);
}

bool isStructureFileLoading() {
    auto& loader = fileLoader();
    std::lock_guard lock(loader.mutex);
    return loader.task.valid() && loader.task.wait_for(std::chrono::seconds{0}) != std::future_status::ready;
}

bool startSavedProjectionLoad() {
    auto saved = StructureSession::getInstance().savedProjection();
    auto const origin = StructureLoadOrigin{saved.anchorX, saved.anchorY, saved.anchorZ, std::nullopt};
    auto path = saved.structurePath;
    return startLoad(std::move(path), origin, std::move(saved));
}

bool startTaskComposition(std::string taskId, std::uint64_t revision, StructureLoadOrigin origin,
                          std::vector<CompositionFile> files, std::vector<std::string> paths,
                          bool reloadFiles) {
    auto& loader = fileLoader();
    std::lock_guard lock(loader.mutex);
    if (loader.stopped)
        return false;
    if (loader.task.valid()) {
        if (loader.task.wait_for(std::chrono::seconds{0}) != std::future_status::ready)
            return false;
        loader.task.get();
    }
    auto level = ll::service::getMultiPlayerLevel();
    if (!level)
        return false;
    if (loader.level != &*level) {
        if (loader.level)
            loader.level->removeListener(loader);
        level->addListener(loader);
        loader.level = &*level;
        loader.cache.clear();
    }
    if (!reloadFiles)
        for (std::size_t i = 0; i < files.size(); ++i)
            if (auto it = loader.cache.find(paths[i]); it != loader.cache.end())
                files[i].loaded = it->second;
    auto result = std::make_shared<CompositionLoadResult>();
    result->request = StructureSession::getInstance().beginCompositionLoad(origin);
    result->revision = revision;
    result->taskId = std::move(taskId);
    result->files = std::move(files);
    loader.compositionResult = result;
    auto& ui = StructureUiState::getInstance();
    auto const hint = ui.beginLoadingHint(i18n::TextKey::TasksLoading);
    try {
        loader.task = std::async(std::launch::async, [result, paths = std::move(paths), hint] {
            std::lock_guard parseLock(nativeParserMutex);
            auto finishHint = gsl::finally([&] { StructureUiState::getInstance().finishLoadingHint(hint); });
            try {
                std::unordered_map<std::string, std::shared_ptr<LoadedStructure const>> parsed;
                for (std::size_t i = 0; i < result->files.size(); ++i)
                    if (result->files[i].loaded)
                        parsed[paths[i]] = result->files[i].loaded;
                for (std::size_t i = 0; i < result->files.size(); ++i) {
                    auto& file = result->files[i];
                    if (!file.visible || file.loaded)
                        continue;
                    if (auto found = parsed.find(paths[i]); found != parsed.end()) {
                        file.loaded = found->second;
                        continue;
                    }
                    result->errorFileId = file.id;
                    file.loaded = loadStructureFile(pathFromUtf8(paths[i]), result->error);
                    if (!file.loaded)
                        return;
                    parsed[paths[i]] = file.loaded;
                }
                result->errorFileId.clear();
                normalizeCompositionLayers(result->files);
                result->composition =
                    composeTaskProjection(result->files, [](Block const* block, int mirror, int rotation) -> Block const* {
                        if (!block || (mirror == 0 && rotation == 0))
                            return block;
                        constexpr Rotation rotations[]{Rotation::None, Rotation::Clockwise90,
                                                       Rotation::Clockwise180, Rotation::CounterClockwise90};
                        constexpr Mirror mirrors[]{Mirror::None, Mirror::Z, Mirror::X};
                        return VanillaBlockStateTransformUtils::transformBlock(*block, rotations[rotation],
                                                                               mirrors[mirror]);
                    });
                std::array<int, 3> base{result->request.origin.x, result->request.origin.y,
                                        result->request.origin.z};
                auto const& composed = *result->composition.loaded;
                std::array<int, 3> size{composed.sizeX, composed.sizeY, composed.sizeZ};
                for (int axis = 0; axis < 3; ++axis) {
                    auto minimum = static_cast<long long>(base[axis]) + result->composition.origin[axis];
                    auto maximum = minimum + std::max(0, size[axis] - 1);
                    if (minimum < std::numeric_limits<int>::min() || maximum > std::numeric_limits<int>::max())
                        throw std::runtime_error("Task world coordinates exceed supported range");
                }
                if (composed.independentFiles) for (auto const& f:composed.independentFiles->files) {
                    if (!f.visible || !f.loaded) continue;
                    std::array<int,3> offset{f.x,f.y,f.z};
                    bool rotated=(f.rotation&1)!=0;
                    std::array<int,3> extent{rotated?f.loaded->sizeZ:f.loaded->sizeX,f.loaded->sizeY,
                        rotated?f.loaded->sizeX:f.loaded->sizeZ};
                    for (int axis=0;axis<3;++axis) {
                        auto lo=static_cast<long long>(base[axis])+offset[axis];
                        auto hi=lo+extent[axis]-1;
                        if (lo<std::numeric_limits<int>::min() || hi>std::numeric_limits<int>::max())
                            throw std::runtime_error("Task world coordinates exceed supported range");
                    }
                }
            } catch (std::exception const& error) {
                result->error = error.what();
            }
        });
    } catch (std::exception const& error) {
        ui.finishLoadingHint(hint);
        loader.compositionResult.reset();
        StructureSession::getInstance().failFileLoad(result->request,
            i18n::Message{i18n::TextKey::StatusLoadFailed, {error.what()}});
        logger().error("Could not start background task composition: {}", error.what());
        return false;
    }
    return true;
}
std::shared_ptr<CompositionLoadResult> takeTaskCompositionResult() {
    auto& loader = fileLoader();
    std::lock_guard lock(loader.mutex);
    if (!loader.compositionResult || !loader.task.valid() ||
        loader.task.wait_for(std::chrono::seconds{0}) != std::future_status::ready)
        return {};
    loader.task.get();
    auto result = std::exchange(loader.compositionResult, {});
    if (result->error.empty()) {
        loader.cache.clear();
        for (auto const& file : result->files)
            if (file.loaded)
                loader.cache[pathToUtf8(file.loaded->sourcePath)] = file.loaded;
    }
    return result;
}
std::shared_ptr<LoadedStructure const> cachedTaskFile(std::string const& path) {
    auto& loader = fileLoader();
    std::lock_guard lock(loader.mutex);
    auto it = loader.cache.find(path);
    return it == loader.cache.end() ? nullptr : it->second;
}

bool startStructureRead(std::uint64_t id, std::string path) {
    auto& loader = fileLoader();
    std::lock_guard lock(loader.mutex);
    if (loader.stopped || !id) return false;
    auto level = ll::service::getMultiPlayerLevel();
    if (!level) return false;
    if (loader.level != &*level) {
        if (loader.level) loader.level->removeListener(loader);
        level->addListener(loader);
        loader.level = &*level;
    }
    return loader.reader.start(id,structureFileWorldRevision(),std::move(path));
}

void cancelStructureRead(std::uint64_t id) {
    auto& loader = fileLoader();
    std::lock_guard lock(loader.mutex);
    loader.reader.cancel(id);
}
std::shared_ptr<StructureReadResult const> takeStructureReadResult() {
    auto& loader = fileLoader();
    std::lock_guard lock(loader.mutex);
    return loader.reader.take();
}

} // namespace lholo::structure::detail
