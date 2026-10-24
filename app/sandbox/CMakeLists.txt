include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.3
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw spdlog)

FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

find_package(Vulkan REQUIRED)

find_program(GLSLANG_VALIDATOR glslangValidator HINTS
    "$ENV{VULKAN_SDK}/Bin"
    "$ENV{VULKAN_SDK}/bin"
    "C:/VulkanSDK/1.4.363.0/Bin"
)
if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator was not found. Install the Vulkan SDK or add it to PATH.")
endif()

set(SANDBOX_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(SANDBOX_SHADER_OUTPUTS)
foreach(SHADER triangle.vert triangle.frag)
    set(SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}")
    set(SHADER_OUTPUT "${SANDBOX_SHADER_OUTPUT_DIR}/${SHADER}.spv")
    add_custom_command(
        OUTPUT "${SHADER_OUTPUT}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SANDBOX_SHADER_OUTPUT_DIR}"
        COMMAND "${GLSLANG_VALIDATOR}" -V "${SHADER_SOURCE}" -o "${SHADER_OUTPUT}"
        DEPENDS "${SHADER_SOURCE}"
        VERBATIM
    )
    list(APPEND SANDBOX_SHADER_OUTPUTS "${SHADER_OUTPUT}")
endforeach()
add_custom_target(sandbox_shaders DEPENDS ${SANDBOX_SHADER_OUTPUTS})

add_library(imgui_vulkan STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui_vulkan PUBLIC ${imgui_SOURCE_DIR})
target_link_libraries(imgui_vulkan PUBLIC glfw Vulkan::Vulkan)

option(KAGUTSUCHI_SANDBOX_VALIDATION_LAYERS "Enable Vulkan validation layers" OFF)

add_executable(sandbox
    src/main.cpp
    src/vulkan_context.cpp
)
add_dependencies(sandbox sandbox_shaders)
target_compile_definitions(sandbox PRIVATE KAGUTSUCHI_SANDBOX_SHADER_DIR="${SANDBOX_SHADER_OUTPUT_DIR}")
target_compile_features(sandbox PRIVATE cxx_std_20)
target_link_libraries(sandbox PRIVATE glfw spdlog::spdlog Vulkan::Vulkan imgui_vulkan)
if(KAGUTSUCHI_SANDBOX_VALIDATION_LAYERS)
    target_compile_definitions(sandbox PRIVATE KAGUTSUCHI_SANDBOX_VALIDATION_LAYERS)
endif()
