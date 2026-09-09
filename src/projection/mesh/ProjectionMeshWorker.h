// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Single-thread projection mesh executor and completion queue. Worker tasks are
// supplied by Projection.cpp and must not access active projection state.

#pragma once

#include "projection/core/ProjectionInternalTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "mc/deps/minecraft_renderer/renderer/Mesh.h"

namespace lholo::projection::detail {

struct AsyncSectionBuildResult {
    std::uint64_t workerGeneration{};
    std::uint64_t structureGeneration{};
    std::uint64_t revision{};
    std::uint64_t snapshotPrepareMicros{};
    std::uint64_t snapshotDataMicros{};
    std::uint64_t chunkViewMicros{};
    std::uint64_t workerBuildMicros{};
    std::size_t   section{};
    bool          success{};
    std::string   failureReason;
    std::uint64_t expectedVertexCount{};
    std::uint64_t actualVertexCount{};
    std::array<std::unique_ptr<mce::Mesh>, static_cast<std::size_t>(RenderBucket::Count)>
        sectionMeshes;
    std::unique_ptr<mce::Mesh> warningFillMesh;
    std::unique_ptr<mce::Mesh> correctionOutlineMesh;
    std::unique_ptr<mce::Mesh> wrongFillMesh;
    std::unique_ptr<mce::Mesh> wrongOutlineMesh;
    std::unique_ptr<mce::Mesh> liquidProxyMesh;
    std::unique_ptr<mce::Mesh> blockEntityPlaceholderMesh;
};

std::uint64_t startMeshWorker();
void          stopMeshWorker();

bool submitMeshWorkerTask(
    std::uint64_t workerGeneration,
    std::function<AsyncSectionBuildResult()> task
);

std::vector<AsyncSectionBuildResult> takeCompletedSectionBuilds(std::size_t limit);

bool meshWorkerIsBusy();
bool meshWorkerIsDisabledForSession();
void disableMeshWorkerForSession();

} // namespace lholo::projection::detail
