#pragma once

#include <memory>

enum class ApplicationEvent {
    Input,
    EnterBackground,
    EnterForeground,
    GraphicsReset,
    LowMemory,
    Terminating
};

class ApplicationHost {
public:
    virtual ~ApplicationHost() = default;
    virtual bool iterate() = 0;
    virtual void event(ApplicationEvent event, const void* nativeEvent) = 0;
    virtual void shutdown() = 0;
};

// Returns null after handling a command-line request that exits immediately.
std::unique_ptr<ApplicationHost> createApplication(int argc, char** argv);

