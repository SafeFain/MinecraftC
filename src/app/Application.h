#pragma once

#include "core/ApplicationHost.h"
#include "core/GraphicsApi.h"
#include "core/Platform.h"

#include <memory>

std::unique_ptr<ApplicationHost> createGameApplication(
    RuntimePaths paths, GraphicsApi api);

std::unique_ptr<ApplicationHost> createRenderDemoApplication(
    RuntimePaths paths, GraphicsApi api, bool texturedDemo,
    int benchmarkFrames);
