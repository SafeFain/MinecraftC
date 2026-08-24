#pragma once

#include <memory>
#include <stdexcept>
#include <string>

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

class CommandLineError : public std::runtime_error {
public:
    explicit CommandLineError(const std::string& message)
        : std::runtime_error(message) {}
};

// Returns null after handling a command-line request that exits immediately.
std::unique_ptr<ApplicationHost> createApplication(int argc, char** argv);
