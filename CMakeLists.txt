cmake_minimum_required(VERSION 3.24)

project(kagutsuchi
    VERSION 0.1.0
    DESCRIPTION "C++ and Vulkan physical simulation foundation"
    LANGUAGES CXX
)

option(KAGUTSUCHI_BUILD_APPS "Build Kagutsuchi applications" ON)
option(KAGUTSUCHI_BUILD_TESTS "Build Kagutsuchi tests" OFF)

add_subdirectory(lib)

if(KAGUTSUCHI_BUILD_APPS)
    add_subdirectory(app)
endif()

if(KAGUTSUCHI_BUILD_TESTS)
    enable_testing()
endif()
