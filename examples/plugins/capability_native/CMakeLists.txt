cmake_minimum_required(VERSION 3.16)
project(capability_native LANGUAGES CXX)
add_library(capability_native MODULE plugin.cpp)
target_include_directories(capability_native PRIVATE ../../../sdk)
set_target_properties(capability_native PROPERTIES PREFIX "" CXX_STANDARD 17)

if(APPLE)
    set_target_properties(capability_native PROPERTIES SUFFIX ".dylib")
endif()
