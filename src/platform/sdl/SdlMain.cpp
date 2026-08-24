#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_messagebox.h>

#include "core/ApplicationHost.h"
#include "debug/Log.h"

#include <exception>
#include <iostream>
#include <memory>
#include <string>

namespace {
void reportFatal(const char* context, const std::exception& error) {
    const std::string message = std::string("MinecraftC ") + context +
                                " failed:\n" + error.what();
    std::cerr << message << '\n';
    LOG_FATAL(message);
    if (!SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR, "MinecraftC", message.c_str(), nullptr))
        std::cerr << "Could not show SDL error message: " << SDL_GetError() << '\n';
}

ApplicationEvent translateLifecycle(Uint32 type) {
    switch (type) {
        case SDL_EVENT_WILL_ENTER_BACKGROUND:
            return ApplicationEvent::EnterBackground;
        case SDL_EVENT_DID_ENTER_FOREGROUND:
            return ApplicationEvent::EnterForeground;
        case SDL_EVENT_RENDER_DEVICE_RESET:
            return ApplicationEvent::GraphicsReset;
        case SDL_EVENT_LOW_MEMORY:
            return ApplicationEvent::LowMemory;
        case SDL_EVENT_TERMINATING:
            return ApplicationEvent::Terminating;
        default:
            return ApplicationEvent::Input;
    }
}
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    try {
        auto app = createApplication(argc, argv);
        if (!app) return SDL_APP_SUCCESS;
        *appstate = app.release();
        return SDL_APP_CONTINUE;
    } catch (const CommandLineError& error) {
        std::cerr << "MinecraftC command line error: " << error.what() << '\n';
        return SDL_APP_FAILURE;
    } catch (const std::exception& error) {
        reportFatal("startup", error);
        return SDL_APP_FAILURE;
    }
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    if (!appstate) return SDL_APP_SUCCESS;
    try {
        static_cast<ApplicationHost*>(appstate)->event(
            translateLifecycle(event->type), event);
        return SDL_APP_CONTINUE;
    } catch (const std::exception& error) {
        reportFatal("event handling", error);
        return SDL_APP_FAILURE;
    }
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    if (!appstate) return SDL_APP_SUCCESS;
    try {
        return static_cast<ApplicationHost*>(appstate)->iterate()
            ? SDL_APP_CONTINUE : SDL_APP_SUCCESS;
    } catch (const std::exception& error) {
        reportFatal("runtime", error);
        return SDL_APP_FAILURE;
    }
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
    if (!appstate) return;
    try {
        auto app = std::unique_ptr<ApplicationHost>(
            static_cast<ApplicationHost*>(appstate));
        app->shutdown();
    } catch (const std::exception& error) {
        reportFatal("shutdown", error);
    }
}
