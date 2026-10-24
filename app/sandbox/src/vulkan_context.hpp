#pragma once

#include <cstdint>
#include <limits>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class VulkanContext
{
  public:
    explicit VulkanContext(GLFWwindow* window);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    void drawFrame();

  private:
    struct QueueFamilies
    {
        static constexpr std::uint32_t Invalid = std::numeric_limits<std::uint32_t>::max();

        std::uint32_t graphics = Invalid;
        std::uint32_t present = Invalid;

        bool isComplete() const
        {
            return graphics != Invalid && present != Invalid;
        }
    };

    static std::vector<const char*> getValidationLayers();
    static bool hasValidationLayers(const std::vector<const char*>& layers);
    static void checkResult(VkResult result, const char* operation);
    QueueFamilies findQueueFamilies(VkPhysicalDevice device) const;
    bool supportsSwapchain(VkPhysicalDevice device) const;
    void createInstance();
    void createSurface(GLFWwindow* window);
    void selectPhysicalDevice();
    void createLogicalDevice();
    void createSwapchain();
    void createImageViews();
    void createRenderPass();
    void createFramebuffers();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    void createImGui();
    void destroyImGui() noexcept;
    void renderImGui(VkCommandBuffer commandBuffer);
    void createRenderFinishedSemaphores();
    void recordCommandBuffer(VkCommandBuffer commandBuffer, std::uint32_t imageIndex);
    void recreateSwapchain();
    void cleanupSwapchain() noexcept;
    void cleanup() noexcept;

    GLFWwindow* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    QueueFamilies queueFamilies_;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainImageFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;
    static constexpr std::uint32_t MaxFramesInFlight = 2;
    VkSemaphore imageAvailableSemaphores_[MaxFramesInFlight]{};
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    VkFence inFlightFences_[MaxFramesInFlight]{};
    VkDescriptorPool imguiDescriptorPool_ = VK_NULL_HANDLE;
    bool imguiGlfwInitialized_ = false;
    bool imguiVulkanInitialized_ = false;
    std::uint32_t currentFrame_ = 0;
    bool framebufferResized_ = false;
};
