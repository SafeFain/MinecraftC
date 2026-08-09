#pragma once

enum class GraphicsApi {
    OpenGL33,
    OpenGLES30,
    Vulkan
};

struct GraphicsCapabilities {
    GraphicsApi api = GraphicsApi::OpenGL33;
    int majorVersion = 0;
    int minorVersion = 0;
    int samples = 0;
    bool framebufferSrgb = false;
    bool instancing = false;
};
