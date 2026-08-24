#pragma once

#include "core/ApplicationHost.h"
#include "core/Platform.h"

#include <memory>

std::unique_ptr<ApplicationHost> createGameApplication(RuntimePaths paths);

std::unique_ptr<ApplicationHost> createRenderDemoApplication(
    RuntimePaths paths, bool texturedDemo, int benchmarkFrames);
