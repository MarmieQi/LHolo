// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#pragma once

#include <string>
#include "structure/TaskComposer.h"
#include "structure/StructureSession.h"
#include "structure/StructureReadQueue.h"

namespace lholo::structure::detail {

struct StructureLoadOrigin;

// Projection and read-only preview own separate background jobs. Normal close
// invalidates publication without waiting. Level destruction / shutdown drain
// both channels before the world's registry or this DLL can be released.
void enableStructureFileLoader();
void shutdownStructureFileLoader();
bool isStructureFileLoading();
// Advances on Level destruction or loader restart, even while tasks are closed.
std::uint64_t structureFileWorldRevision();
bool startStructureFileLoad(std::string path, StructureLoadOrigin origin);
bool startSavedProjectionLoad();
struct CompositionLoadResult {
    StructureLoadRequest request;
    std::uint64_t revision{};
    std::string taskId, error, errorFileId;
    std::vector<CompositionFile> files;
    Composition composition;
};
bool startTaskComposition(std::string taskId, std::uint64_t revision, StructureLoadOrigin,
    std::vector<CompositionFile>, std::vector<std::string> paths, bool reloadFiles);
std::shared_ptr<CompositionLoadResult> takeTaskCompositionResult();
std::shared_ptr<LoadedStructure const> cachedTaskFile(std::string const& path);

// Independent read-only job channel. Only native parser serialization and
// world lifetime are shared; projection busy state/cache/session are untouched.
bool startStructureRead(std::uint64_t id, std::string path);
void cancelStructureRead(std::uint64_t id);
std::shared_ptr<StructureReadResult const> takeStructureReadResult();

} // namespace lholo::structure::detail
