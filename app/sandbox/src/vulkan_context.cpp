#include "vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <set>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>

namespace
{

constexpr const char* ApplicationName = "Kagutsuchi Sandbox";
constexpr const char* SwapchainExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

struct SwapchainSupport
{
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

SwapchainSupport querySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    SwapchainSupport support;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &support.capabilities) !=
        VK_SUCCESS)
    {
        throw std::runtime_error("Failed to query Vulkan surface capabilities");
    }

    std::uint32_t count = 0;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, nullptr) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to query Vulkan surface formats");
    }
    support.formats.resize(count);
    if (count > 0 && vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count,
                                                          support.formats.data()) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to query Vulkan surface formats");
    }

    if (vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, nullptr) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to query Vulkan present modes");
    }
    support.presentModes.resize(count);
    if (count > 0 && vkGetPhysicalDeviceSurfacePresentModesKHR(
                         device, surface, &count, support.presentModes.data()) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to query Vulkan present modes");
    }
    return support;
}

VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
{
    if (formats.size() == 1 && formats.front().format == VK_FORMAT_UNDEFINED)
    {
        return {VK_FORMAT_B8G8R8A8_SRGB, formats.front().colorSpace};
    }
    for (const VkSurfaceFormatKHR& format : formats)
    {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return format;
        }
    }
    return formats.front();
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& presentModes)
{
    for (VkPresentModeKHR presentMode : presentModes)
    {
        if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return presentMode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window)
{
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max())
    {
        return capabilities.currentExtent;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    VkExtent2D extent = {static_cast<std::uint32_t>(std::max(width, 0)),
                         static_cast<std::uint32_t>(std::max(height, 0))};
    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width,
                              capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height,
                               capabilities.maxImageExtent.height);
    return extent;
}

} // namespace

VulkanContext::VulkanContext(GLFWwindow* window) : window_(window)
{
    if (window_ == nullptr)
    {
        throw std::invalid_argument("Vulkan context requires a GLFW window");
    }
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_,
                                   [](GLFWwindow* resizedWindow, int, int)
                                   {
                                       auto* context = static_cast<VulkanContext*>(
                                           glfwGetWindowUserPointer(resizedWindow));
                                       if (context != nullptr)
                                       {
                                           context->framebufferResized_ = true;
                                       }
                                   });

    try
    {
        createInstance();
        createSurface(window_);
        selectPhysicalDevice();
        createLogicalDevice();
        createSwapchain();
        createImageViews();
        createRenderPass();
        createGraphicsPipeline();
        createFramebuffers();
        createCommandPool();
        createCommandBuffers();
        createSyncObjects();
        createImGui();
    }
    catch (...)
    {
        cleanup();
        glfwSetFramebufferSizeCallback(window_, nullptr);
        glfwSetWindowUserPointer(window_, nullptr);
        throw;
    }
}

VulkanContext::~VulkanContext()
{
    cleanup();
    glfwSetFramebufferSizeCallback(window_, nullptr);
    glfwSetWindowUserPointer(window_, nullptr);
}

void VulkanContext::checkResult(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error(std::string(operation) + " failed with VkResult " +
                                 std::to_string(result));
    }
}

void VulkanContext::cleanup() noexcept
{
    if (device_ != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device_);
        destroyImGui();
        if (graphicsPipeline_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device_, graphicsPipeline_, nullptr);
            graphicsPipeline_ = VK_NULL_HANDLE;
        }
        if (pipelineLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
            pipelineLayout_ = VK_NULL_HANDLE;
        }
        for (std::uint32_t index = 0; index < MaxFramesInFlight; ++index)
        {
            if (inFlightFences_[index] != VK_NULL_HANDLE)
            {
                vkDestroyFence(device_, inFlightFences_[index], nullptr);
            }
            if (imageAvailableSemaphores_[index] != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(device_, imageAvailableSemaphores_[index], nullptr);
            }
        }
        for (VkSemaphore semaphore : renderFinishedSemaphores_)
        {
            if (semaphore != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(device_, semaphore, nullptr);
            }
        }
        renderFinishedSemaphores_.clear();
        cleanupSwapchain();
        if (commandPool_ != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(device_, commandPool_, nullptr);
            commandPool_ = VK_NULL_HANDLE;
        }
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (surface_ != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE)
    {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

void VulkanContext::createImGui()
{
    constexpr std::array<VkDescriptorPoolSize, 11> PoolSizes = {{
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
    }};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 11000;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(PoolSizes.size());
    poolInfo.pPoolSizes = PoolSizes.data();
    checkResult(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &imguiDescriptorPool_),
                "Creating ImGui descriptor pool");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    if (!ImGui_ImplGlfw_InitForVulkan(window_, true))
    {
        throw std::runtime_error("Initializing ImGui GLFW backend failed");
    }
    imguiGlfwInitialized_ = true;

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = instance_;
    initInfo.PhysicalDevice = physicalDevice_;
    initInfo.Device = device_;
    initInfo.QueueFamily = queueFamilies_.graphics;
    initInfo.Queue = graphicsQueue_;
    initInfo.DescriptorPool = imguiDescriptorPool_;
    initInfo.MinImageCount = std::min(
        std::max(2U, static_cast<std::uint32_t>(swapchainImages_.size())),
        std::max(2U, querySwapchainSupport(physicalDevice_, surface_).capabilities.minImageCount));
    initInfo.ImageCount = static_cast<std::uint32_t>(swapchainImages_.size());
    initInfo.RenderPass = renderPass_;
    if (!ImGui_ImplVulkan_Init(&initInfo))
    {
        throw std::runtime_error("Initializing ImGui Vulkan backend failed");
    }
    imguiVulkanInitialized_ = true;
}

void VulkanContext::destroyImGui() noexcept
{
    if (imguiVulkanInitialized_)
    {
        ImGui_ImplVulkan_Shutdown();
        imguiVulkanInitialized_ = false;
    }
    if (imguiGlfwInitialized_)
    {
        ImGui_ImplGlfw_Shutdown();
        imguiGlfwInitialized_ = false;
    }
    if (imguiGlfwInitialized_ || imguiVulkanInitialized_)
    {
        return;
    }
    if (ImGui::GetCurrentContext() != nullptr)
    {
        ImGui::DestroyContext();
    }
    if (imguiDescriptorPool_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device_, imguiDescriptorPool_, nullptr);
        imguiDescriptorPool_ = VK_NULL_HANDLE;
    }
}

void VulkanContext::renderImGui(VkCommandBuffer commandBuffer)
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGui::Begin("Sandbox status");
    ImGui::Text("Renderer: Vulkan");
    ImGui::Text("Swapchain: %u x %u", swapchainExtent_.width, swapchainExtent_.height);
    ImGui::Text("Images: %u", static_cast<std::uint32_t>(swapchainImages_.size()));
    ImGui::End();
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}

void VulkanContext::cleanupSwapchain() noexcept
{
    if (device_ == VK_NULL_HANDLE)
    {
        return;
    }
    if (!commandBuffers_.empty() && commandPool_ != VK_NULL_HANDLE)
    {
        vkFreeCommandBuffers(device_, commandPool_,
                             static_cast<std::uint32_t>(commandBuffers_.size()),
                             commandBuffers_.data());
        commandBuffers_.clear();
    }
    for (VkFramebuffer framebuffer : framebuffers_)
    {
        vkDestroyFramebuffer(device_, framebuffer, nullptr);
    }
    framebuffers_.clear();
    if (graphicsPipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device_, graphicsPipeline_, nullptr);
        graphicsPipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    if (renderPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
    for (VkImageView imageView : swapchainImageViews_)
    {
        vkDestroyImageView(device_, imageView, nullptr);
    }
    swapchainImageViews_.clear();
    if (swapchain_ != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    swapchainImages_.clear();
}

VkShaderModule VulkanContext::createShaderModule(const std::vector<char>& code) const
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const std::uint32_t*>(code.data());
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    checkResult(vkCreateShaderModule(device_, &createInfo, nullptr, &shaderModule),
                "Creating Vulkan shader module");
    return shaderModule;
}

void VulkanContext::createGraphicsPipeline()
{
    const auto readShader = [](const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file)
        {
            throw std::runtime_error("Failed to open shader: " + path.string());
        }
        const std::streamsize size = file.tellg();
        if (size <= 0 || size % 4 != 0)
        {
            throw std::runtime_error("Shader has an invalid SPIR-V size: " + path.string());
        }
        std::vector<char> code(static_cast<std::size_t>(size));
        file.seekg(0);
        file.read(code.data(), size);
        if (!file)
        {
            throw std::runtime_error("Failed to read shader: " + path.string());
        }
        return code;
    };

    const std::filesystem::path shaderDirectory = KAGUTSUCHI_SANDBOX_SHADER_DIR;
    const std::vector<char> vertexCode = readShader(shaderDirectory / "triangle.vert.spv");
    const std::vector<char> fragmentCode = readShader(shaderDirectory / "triangle.frag.spv");
    const VkShaderModule vertexModule = createShaderModule(vertexCode);
    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    try
    {
        fragmentModule = createShaderModule(fragmentCode);
        const VkPipelineShaderStageCreateInfo vertexStage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            VK_SHADER_STAGE_VERTEX_BIT,
            vertexModule,
            "main",
            nullptr};
        const VkPipelineShaderStageCreateInfo fragmentStage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            fragmentModule,
            "main",
            nullptr};
        const std::array<VkPipelineShaderStageCreateInfo, 2> stages = {vertexStage, fragmentStage};

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewport{};
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        const std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                             VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0F;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState colorAttachment{};
        colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorAttachment;

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        checkResult(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_),
                    "Creating Vulkan pipeline layout");

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewport;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipelineLayout_;
        pipelineInfo.renderPass = renderPass_;
        pipelineInfo.subpass = 0;
        const VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo,
                                                          nullptr, &graphicsPipeline_);
        if (result != VK_SUCCESS)
        {
            checkResult(result, "Creating Vulkan graphics pipeline");
        }
    }
    catch (...)
    {
        vkDestroyShaderModule(device_, vertexModule, nullptr);
        if (fragmentModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(device_, fragmentModule, nullptr);
        }
        throw;
    }
    vkDestroyShaderModule(device_, vertexModule, nullptr);
    vkDestroyShaderModule(device_, fragmentModule, nullptr);
}

std::vector<const char*> VulkanContext::getValidationLayers()
{
    return {"VK_LAYER_KHRONOS_validation"};
}

bool VulkanContext::hasValidationLayers(const std::vector<const char*>& layers)
{
    std::uint32_t layerCount = 0;
    checkResult(vkEnumerateInstanceLayerProperties(&layerCount, nullptr),
                "Enumerating Vulkan layers");
    std::vector<VkLayerProperties> availableLayers(layerCount);
    checkResult(vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data()),
                "Enumerating Vulkan layers");
    for (const char* requestedLayer : layers)
    {
        if (std::none_of(availableLayers.begin(), availableLayers.end(),
                         [requestedLayer](const VkLayerProperties& layer)
                         { return std::string(layer.layerName) == requestedLayer; }))
        {
            return false;
        }
    }
    return true;
}

void VulkanContext::createInstance()
{
    const std::vector<const char*> validationLayers = getValidationLayers();
#ifdef KAGUTSUCHI_SANDBOX_VALIDATION_LAYERS
    if (!hasValidationLayers(validationLayers))
    {
        throw std::runtime_error("Requested Vulkan validation layer is unavailable");
    }
#endif
    std::uint32_t extensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (glfwExtensions == nullptr || extensionCount == 0)
    {
        throw std::runtime_error("GLFW did not provide Vulkan instance extensions");
    }

    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = ApplicationName;
    applicationInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    applicationInfo.pEngineName = "Kagutsuchi";
    applicationInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    applicationInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &applicationInfo;
    createInfo.enabledExtensionCount = extensionCount;
    createInfo.ppEnabledExtensionNames = glfwExtensions;
#ifdef KAGUTSUCHI_SANDBOX_VALIDATION_LAYERS
    createInfo.enabledLayerCount = static_cast<std::uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();
#endif
    checkResult(vkCreateInstance(&createInfo, nullptr, &instance_), "Creating Vulkan instance");
    spdlog::info("Vulkan instance created");
}

void VulkanContext::createSurface(GLFWwindow* window)
{
    checkResult(glfwCreateWindowSurface(instance_, window, nullptr, &surface_),
                "Creating Vulkan window surface");
}

VulkanContext::QueueFamilies VulkanContext::findQueueFamilies(VkPhysicalDevice device) const
{
    std::uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());
    QueueFamilies result;
    for (std::uint32_t index = 0; index < queueFamilyCount; ++index)
    {
        if ((queueFamilies[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
        {
            result.graphics = index;
        }
        VkBool32 supportsPresent = VK_FALSE;
        checkResult(vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface_, &supportsPresent),
                    "Querying presentation support");
        if (supportsPresent == VK_TRUE)
        {
            result.present = index;
        }
        if (result.isComplete())
        {
            break;
        }
    }
    return result;
}

bool VulkanContext::supportsSwapchain(VkPhysicalDevice device) const
{
    std::uint32_t extensionCount = 0;
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr) !=
        VK_SUCCESS)
    {
        return false;
    }
    std::vector<VkExtensionProperties> extensions(extensionCount);
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data()) !=
        VK_SUCCESS)
    {
        return false;
    }
    const bool hasExtension =
        std::any_of(extensions.begin(), extensions.end(), [](const VkExtensionProperties& extension)
                    { return std::string(extension.extensionName) == SwapchainExtension; });
    if (!hasExtension)
    {
        return false;
    }
    const SwapchainSupport support = querySwapchainSupport(device, surface_);
    return !support.formats.empty() && !support.presentModes.empty() &&
           (support.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0;
}

void VulkanContext::selectPhysicalDevice()
{
    std::uint32_t deviceCount = 0;
    checkResult(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr),
                "Enumerating Vulkan devices");
    if (deviceCount == 0)
    {
        throw std::runtime_error("No Vulkan-capable physical device was found");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    checkResult(vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()),
                "Enumerating Vulkan devices");
    for (VkPhysicalDevice device : devices)
    {
        const QueueFamilies families = findQueueFamilies(device);
        if (families.isComplete() && supportsSwapchain(device))
        {
            physicalDevice_ = device;
            queueFamilies_ = families;
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(device, &properties);
            spdlog::info("Selected Vulkan device: {}", properties.deviceName);
            return;
        }
    }
    throw std::runtime_error(
        "No Vulkan device supports graphics, presentation, and swapchain creation");
}

void VulkanContext::createLogicalDevice()
{
    const std::set<std::uint32_t> uniqueQueueFamilies = {queueFamilies_.graphics,
                                                         queueFamilies_.present};
    constexpr float QueuePriority = 1.0F;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    for (std::uint32_t queueFamily : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &QueuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }
    VkPhysicalDeviceFeatures deviceFeatures{};
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = &SwapchainExtension;
#ifdef KAGUTSUCHI_SANDBOX_VALIDATION_LAYERS
    const std::vector<const char*> validationLayers = getValidationLayers();
    createInfo.enabledLayerCount = static_cast<std::uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();
#endif
    checkResult(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_),
                "Creating Vulkan logical device");
    vkGetDeviceQueue(device_, queueFamilies_.graphics, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, queueFamilies_.present, 0, &presentQueue_);
}

void VulkanContext::createSwapchain()
{
    const SwapchainSupport support = querySwapchainSupport(physicalDevice_, surface_);
    if (support.formats.empty() || support.presentModes.empty())
    {
        throw std::runtime_error("Vulkan surface has no usable swapchain formats or present modes");
    }
    if ((support.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0)
    {
        throw std::runtime_error(
            "Vulkan surface does not support color attachment swapchain images");
    }
    const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.formats);
    const VkPresentModeKHR presentMode = choosePresentMode(support.presentModes);
    const VkExtent2D extent = chooseExtent(support.capabilities, window_);
    if (extent.width == 0 || extent.height == 0)
    {
        throw std::runtime_error("Cannot create Vulkan swapchain for a zero-sized framebuffer");
    }
    std::uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount)
    {
        imageCount = support.capabilities.maxImageCount;
    }
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface_;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    const std::array<std::uint32_t, 2> indices = {queueFamilies_.graphics, queueFamilies_.present};
    if (queueFamilies_.graphics != queueFamilies_.present)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = static_cast<std::uint32_t>(indices.size());
        createInfo.pQueueFamilyIndices = indices.data();
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    createInfo.preTransform = support.capabilities.currentTransform;
    constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> CompositeAlphaModes = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
    const auto compositeAlpha =
        std::find_if(CompositeAlphaModes.begin(), CompositeAlphaModes.end(), [&support](auto mode)
                     { return (support.capabilities.supportedCompositeAlpha & mode) != 0; });
    if (compositeAlpha == CompositeAlphaModes.end())
    {
        throw std::runtime_error("Vulkan surface supports no composite alpha mode");
    }
    createInfo.compositeAlpha = *compositeAlpha;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    checkResult(vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_),
                "Creating Vulkan swapchain");
    swapchainImageFormat_ = surfaceFormat.format;
    swapchainExtent_ = extent;
    checkResult(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr),
                "Querying swapchain images");
    swapchainImages_.resize(imageCount);
    checkResult(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, swapchainImages_.data()),
                "Querying swapchain images");
}

void VulkanContext::createImageViews()
{
    swapchainImageViews_.resize(swapchainImages_.size(), VK_NULL_HANDLE);
    for (std::size_t index = 0; index < swapchainImages_.size(); ++index)
    {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapchainImages_[index];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapchainImageFormat_;
        createInfo.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                 VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;
        checkResult(vkCreateImageView(device_, &createInfo, nullptr, &swapchainImageViews_[index]),
                    "Creating swapchain image view");
    }
}

void VulkanContext::createRenderPass()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainImageFormat_;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = 1;
    createInfo.pAttachments = &colorAttachment;
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;
    checkResult(vkCreateRenderPass(device_, &createInfo, nullptr, &renderPass_),
                "Creating Vulkan render pass");
}

void VulkanContext::createFramebuffers()
{
    framebuffers_.resize(swapchainImageViews_.size(), VK_NULL_HANDLE);
    for (std::size_t index = 0; index < swapchainImageViews_.size(); ++index)
    {
        VkFramebufferCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        createInfo.renderPass = renderPass_;
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = &swapchainImageViews_[index];
        createInfo.width = swapchainExtent_.width;
        createInfo.height = swapchainExtent_.height;
        createInfo.layers = 1;
        checkResult(vkCreateFramebuffer(device_, &createInfo, nullptr, &framebuffers_[index]),
                    "Creating Vulkan framebuffer");
    }
}

void VulkanContext::createCommandPool()
{
    VkCommandPoolCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    createInfo.queueFamilyIndex = queueFamilies_.graphics;
    checkResult(vkCreateCommandPool(device_, &createInfo, nullptr, &commandPool_),
                "Creating Vulkan command pool");
}

void VulkanContext::createCommandBuffers()
{
    commandBuffers_.resize(framebuffers_.size(), VK_NULL_HANDLE);
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = commandPool_;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = static_cast<std::uint32_t>(commandBuffers_.size());
    checkResult(vkAllocateCommandBuffers(device_, &allocateInfo, commandBuffers_.data()),
                "Allocating Vulkan command buffers");
}

void VulkanContext::createSyncObjects()
{
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (std::uint32_t index = 0; index < MaxFramesInFlight; ++index)
    {
        checkResult(
            vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &imageAvailableSemaphores_[index]),
            "Creating image-available semaphore");
        checkResult(vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFences_[index]),
                    "Creating in-flight fence");
    }
    createRenderFinishedSemaphores();
}

void VulkanContext::createRenderFinishedSemaphores()
{
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    renderFinishedSemaphores_.resize(swapchainImages_.size(), VK_NULL_HANDLE);
    for (VkSemaphore& semaphore : renderFinishedSemaphores_)
    {
        checkResult(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &semaphore),
                    "Creating render-finished semaphore");
    }
}

void VulkanContext::recordCommandBuffer(VkCommandBuffer commandBuffer, std::uint32_t imageIndex)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    checkResult(vkBeginCommandBuffer(commandBuffer, &beginInfo), "Beginning Vulkan command buffer");
    VkClearValue clearColor = {{{0.08F, 0.12F, 0.20F, 1.0F}}};
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass_;
    renderPassInfo.framebuffer = framebuffers_[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchainExtent_;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_);
    VkViewport viewport{};
    viewport.width = static_cast<float>(swapchainExtent_.width);
    viewport.height = static_cast<float>(swapchainExtent_.height);
    viewport.maxDepth = 1.0F;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    VkRect2D scissor{{0, 0}, swapchainExtent_};
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    renderImGui(commandBuffer);
    vkCmdEndRenderPass(commandBuffer);
    checkResult(vkEndCommandBuffer(commandBuffer), "Ending Vulkan command buffer");
}

void VulkanContext::drawFrame()
{
    checkResult(vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX),
                "Waiting for frame fence");
    std::uint32_t imageIndex = 0;
    const VkResult acquireResult = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                                         imageAvailableSemaphores_[currentFrame_],
                                                         VK_NULL_HANDLE, &imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        recreateSwapchain();
        return;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
    {
        checkResult(acquireResult, "Acquiring swapchain image");
    }
    checkResult(vkResetFences(device_, 1, &inFlightFences_[currentFrame_]),
                "Resetting frame fence");
    checkResult(vkResetCommandBuffer(commandBuffers_[imageIndex], 0),
                "Resetting Vulkan command buffer");
    recordCommandBuffer(commandBuffers_[imageIndex], imageIndex);

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailableSemaphores_[currentFrame_];
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers_[imageIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinishedSemaphores_[imageIndex];
    checkResult(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlightFences_[currentFrame_]),
                "Submitting Vulkan frame");

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphores_[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;
    const VkResult presentResult = vkQueuePresentKHR(presentQueue_, &presentInfo);
    if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR &&
        presentResult != VK_ERROR_OUT_OF_DATE_KHR)
    {
        checkResult(presentResult, "Presenting Vulkan frame");
    }
    if (framebufferResized_ || presentResult == VK_SUBOPTIMAL_KHR ||
        presentResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR)
    {
        framebufferResized_ = false;
        recreateSwapchain();
    }
    currentFrame_ = (currentFrame_ + 1) % MaxFramesInFlight;
}

void VulkanContext::recreateSwapchain()
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    while (width == 0 || height == 0)
    {
        if (glfwWindowShouldClose(window_) == GLFW_TRUE)
        {
            return;
        }
        glfwWaitEvents();
        glfwGetFramebufferSize(window_, &width, &height);
    }
    checkResult(vkDeviceWaitIdle(device_), "Waiting for Vulkan device before swapchain recreation");
    destroyImGui();
    for (VkSemaphore semaphore : renderFinishedSemaphores_)
    {
        vkDestroySemaphore(device_, semaphore, nullptr);
    }
    renderFinishedSemaphores_.clear();
    if (graphicsPipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device_, graphicsPipeline_, nullptr);
        graphicsPipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    cleanupSwapchain();
    createSwapchain();
    createImageViews();
    createRenderPass();
    createGraphicsPipeline();
    createFramebuffers();
    createCommandBuffers();
    createRenderFinishedSemaphores();
    createImGui();
    framebufferResized_ = false;
}
