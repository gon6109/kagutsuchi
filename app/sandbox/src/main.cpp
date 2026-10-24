#include <exception>
#include <memory>
#include <spdlog/spdlog.h>
#include <stdexcept>

#include "vulkan_context.hpp"

namespace
{

struct GlfwDeleter
{
    void operator()(GLFWwindow* window) const
    {
        if (window != nullptr)
        {
            glfwDestroyWindow(window);
        }
    }
};

void logGlfwError(int errorCode, const char* description)
{
    spdlog::error("GLFW error {}: {}", errorCode, description);
}

class GlfwSession
{
  public:
    GlfwSession()
    {
        if (glfwInit() != GLFW_TRUE)
        {
            throw std::runtime_error("Failed to initialize GLFW");
        }
    }

    ~GlfwSession()
    {
        glfwTerminate();
    }

    GlfwSession(const GlfwSession&) = delete;
    GlfwSession& operator=(const GlfwSession&) = delete;
};

} // namespace

int main()
{
    try
    {
        spdlog::info("Starting Kagutsuchi sandbox");

        glfwSetErrorCallback(logGlfwError);
        GlfwSession glfwSession;
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        std::unique_ptr<GLFWwindow, GlfwDeleter> window(
            glfwCreateWindow(1280, 720, "Kagutsuchi Sandbox", nullptr, nullptr));
        if (!window)
        {
            throw std::runtime_error("Failed to create GLFW window");
        }

        spdlog::info("Sandbox window created");
        VulkanContext vulkanContext(window.get());
        while (glfwWindowShouldClose(window.get()) == GLFW_FALSE)
        {
            glfwPollEvents();
            vulkanContext.drawFrame();
        }
    }
    catch (const std::exception& exception)
    {
        spdlog::critical("Sandbox stopped: {}", exception.what());
        return 1;
    }

    spdlog::info("Shutting down Kagutsuchi sandbox");
    return 0;
}
