#pragma once

#include "core/ApplicationHost.h"
#include "core/Platform.h"

#include <memory>

std::unique_ptr<ApplicationHost> createGameApplication(RuntimePaths paths, bool safeMode = false);

struct ModelBenchmarkOptions {
    int instances = 128;
    int parts = 1;
    bool batching = true;
    bool only = false;
};

std::unique_ptr<ApplicationHost> createRenderDemoApplication(
    RuntimePaths paths, bool texturedDemo, int benchmarkFrames,
    ModelBenchmarkOptions models = {});
