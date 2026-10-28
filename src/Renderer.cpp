#include "Renderer.h"

#define GLFW_INCLUDE_NONE
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <stb_easy_font.h>

#include "Scene.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef VULKAN_GRASS_SHADER_DIR
#define VULKAN_GRASS_SHADER_DIR "shaders"
#endif
#ifndef VULKAN_GRASS_BUILD_TYPE
#ifdef NDEBUG
#define VULKAN_GRASS_BUILD_TYPE "Release"
#else
#define VULKAN_GRASS_BUILD_TYPE "Debug"
#endif
#endif
#ifndef VULKAN_GRASS_SOURCE_COMMIT
#define VULKAN_GRASS_SOURCE_COMMIT "unknown"
#endif

namespace {

constexpr uint32_t kFramesInFlight = 2;
constexpr uint32_t kComputeGroupSize = 128;
constexpr uint32_t kReduceGroupSize = 8;
constexpr uint32_t kShadowExtent = 2048;
constexpr uint32_t kRenderControlShadows = static_cast<uint32_t>(RenderFeatureShadows);
static_assert(kRenderControlShadows == 64u, "Shadow render-control ABI changed");
constexpr VkDeviceSize kOverlayBufferBytes = 512u * 1024u;
constexpr size_t kEasyFontScratchBytes = 128u * 1024u;
constexpr uint32_t kQueryCount = 12;
constexpr uint32_t kQueryTotalBegin = 0;
constexpr uint32_t kQueryTotalEnd = 1;
constexpr uint32_t kQuerySimulationBegin = 2;
constexpr uint32_t kQuerySimulationEnd = 3;
constexpr uint32_t kQueryDepthBegin = 4;
constexpr uint32_t kQueryDepthEnd = 5;
constexpr uint32_t kQueryCullBegin = 6;
constexpr uint32_t kQueryCullEnd = 7;
constexpr uint32_t kQueryGraphicsTailBegin = 8;
constexpr uint32_t kQueryGraphicsTailEnd = 9;
constexpr uint32_t kQueryShadowBegin = 10;
constexpr uint32_t kQueryShadowEnd = 11;

void Check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        std::ostringstream message;
        message << operation << " failed with VkResult " << static_cast<int>(result);
        throw std::runtime_error(message.str());
    }
}

uint32_t DivideRoundUp(uint32_t value, uint32_t divisor) {
    return (value + divisor - 1u) / divisor;
}

VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment) {
    return alignment == 0 ? value : (value + alignment - 1) & ~(alignment - 1);
}

std::string VulkanVersion(uint32_t version) {
    std::ostringstream stream;
    stream << VK_VERSION_MAJOR(version) << '.' << VK_VERSION_MINOR(version) << '.'
           << VK_VERSION_PATCH(version);
    return stream.str();
}

std::optional<std::filesystem::path> ExecutableDirectory() {
#if defined(__linux__)
    std::error_code error;
    const std::filesystem::path executable =
        std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error && !executable.empty()) {
        return executable.parent_path();
    }
#elif defined(_WIN32)
    std::vector<wchar_t> path(512);
    constexpr size_t kMaximumWindowsPath = 32768;
    for (;;) {
        const DWORD length = GetModuleFileNameW(
            nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            break;
        }
        if (length < path.size()) {
            return std::filesystem::path(std::wstring(path.data(), length)).parent_path();
        }
        if (path.size() >= kMaximumWindowsPath) {
            break;
        }
        path.resize(std::min(path.size() * 2, kMaximumWindowsPath));
    }
#endif
    return std::nullopt;
}

std::vector<uint32_t> ReadSpirv(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Unable to open shader: " + path.string());
    }
    const std::streamsize size = file.tellg();
    if (size <= 0 || (size % 4) != 0) {
        throw std::runtime_error("Invalid SPIR-V file: " + path.string());
    }
    std::vector<uint32_t> words(static_cast<size_t>(size) / sizeof(uint32_t));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(words.data()), size)) {
        throw std::runtime_error("Unable to read shader: " + path.string());
    }
    return words;
}

bool HasLayer(const char* requested) {
    uint32_t count = 0;
    Check(vkEnumerateInstanceLayerProperties(&count, nullptr), "vkEnumerateInstanceLayerProperties");
    std::vector<VkLayerProperties> layers(count);
    Check(vkEnumerateInstanceLayerProperties(&count, layers.data()), "vkEnumerateInstanceLayerProperties");
    return std::any_of(layers.begin(), layers.end(), [requested](const VkLayerProperties& layer) {
        return std::strcmp(layer.layerName, requested) == 0;
    });
}

bool HasInstanceExtension(const char* requested) {
    uint32_t count = 0;
    Check(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr),
          "vkEnumerateInstanceExtensionProperties");
    std::vector<VkExtensionProperties> extensions(count);
    Check(vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()),
          "vkEnumerateInstanceExtensionProperties");
    return std::any_of(extensions.begin(), extensions.end(), [requested](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, requested) == 0;
    });
}

bool HasDeviceExtension(VkPhysicalDevice device, const char* requested) {
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS) {
        return false;
    }
    std::vector<VkExtensionProperties> extensions(count);
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()) != VK_SUCCESS) {
        return false;
    }
    return std::any_of(extensions.begin(), extensions.end(), [requested](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, requested) == 0;
    });
}

std::string OperatingSystemName() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

std::string CpuDescription() {
#if defined(__linux__)
    std::ifstream cpuInfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuInfo, line)) {
        const std::string label = "model name";
        if (line.compare(0, label.size(), label) == 0) {
            const size_t separator = line.find(':');
            if (separator != std::string::npos) {
                const size_t first = line.find_first_not_of(" \t", separator + 1);
                return first == std::string::npos ? "Unknown" : line.substr(first);
            }
        }
    }
#elif defined(_WIN32)
    if (const char* processor = std::getenv("PROCESSOR_IDENTIFIER")) {
        return processor;
    }
#endif
    const unsigned int threads = std::thread::hardware_concurrency();
    return threads == 0 ? "Unknown" : std::to_string(threads) + " hardware threads";
}

std::string DriverVersionString(const VkPhysicalDeviceProperties& properties) {
    std::ostringstream stream;
    if (properties.vendorID == 0x10deu) {
        stream << (properties.driverVersion >> 22u) << '.'
               << ((properties.driverVersion >> 14u) & 0xffu) << '.'
               << ((properties.driverVersion >> 6u) & 0xffu) << '.'
               << (properties.driverVersion & 0x3fu);
    } else {
        stream << properties.driverVersion;
    }
    return stream.str();
}

const char* DepthFormatName(VkFormat format) {
    switch (format) {
        case VK_FORMAT_D32_SFLOAT:
            return "VK_FORMAT_D32_SFLOAT";
        case VK_FORMAT_X8_D24_UNORM_PACK32:
            return "VK_FORMAT_X8_D24_UNORM_PACK32";
        case VK_FORMAT_D16_UNORM:
            return "VK_FORMAT_D16_UNORM";
        default:
            return "VK_FORMAT_UNDEFINED";
    }
}

struct OverlayVertex {
    glm::vec2 position{0.0f};
    glm::vec4 color{1.0f};
};

struct EasyFontVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    std::array<uint8_t, 4> color{};
};
static_assert(sizeof(EasyFontVertex) == 16, "stb_easy_font vertex layout changed");

struct FrameTimings {
    double simulationMs = 0.0;
    double cullMs = 0.0;
    double depthMs = 0.0;
    double shadowMs = 0.0;
    double graphicsTailMs = 0.0;
    double gpuMs = 0.0;
    double cpuMs = 0.0;
};

struct Buffer {
    VkBuffer handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* mapped = nullptr;
};

struct Image {
    VkImage handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    std::vector<VkImageView> mipViews;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 1;
};

struct SwapchainState {
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkSemaphore> renderFinished;
    std::vector<VkFence> imageFences;
};

struct FrameSlot {
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore imageAcquired = VK_NULL_HANDLE;
    VkQueryPool queryPool = VK_NULL_HANDLE;

    Buffer uniform;
    Buffer visibleBlades;
    Buffer indirect;
    Buffer stats;
    Buffer readback;
    Buffer bladeReadback;
    Buffer capture;
    Buffer overlay;
    uint32_t overlayVertexCount = 0;

    Image depth;
    Image hierarchy;
    Image shadow;
    VkFramebuffer depthFramebuffer = VK_NULL_HANDLE;
    VkFramebuffer shadowFramebuffer = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> colorFramebuffers;

    VkDescriptorSet frameSet = VK_NULL_HANDLE;
    VkDescriptorSet dataSet = VK_NULL_HANDLE;
    VkDescriptorSet depthSet = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> reductionSets;

    bool pending = false;
    bool measured = false;
    bool captureRequested = false;
    bool verifyRequested = false;
    uint32_t outputFrame = 0;
    uint32_t imageIndex = 0;
    uint32_t captureWidth = 0;
    uint32_t captureHeight = 0;
    VkFormat captureFormat = VK_FORMAT_UNDEFINED;
    double cpuMilliseconds = 0.0;
    uint64_t submissionIndex = 0;
    FrameUniform uniformSnapshot{};
};

struct DrawPushConstant {
    glm::vec4 translationMaterial{0.0f};
};
static_assert(sizeof(DrawPushConstant) == 16, "Draw push constant must be one vec4");

} // namespace

class Renderer::Impl {
public:
    explicit Impl(const RunOptions& options);
    ~Impl();
    int Run();

private:
    void Initialize();
    void Cleanup() noexcept;
    void CreateWindow();
    void CreateInstance();
    void CreateSurface();
    void PickPhysicalDevice();
    void CreateDevice();
    void CreateCommandPool();
    void CreateSceneResources();
    void CreateDescriptorLayouts();
    void CreateSamplers();
    void CreateSwapchain();
    void DestroySwapchain();
    void CreateRenderPasses();
    void DestroyRenderPasses();
    void CreatePipelines();
    void DestroyPipelines();
    void CreateFrameResources();
    void DestroyFrameResources();
    void RecreateSwapchain();

    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties, Buffer& buffer, bool map);
    void DestroyBuffer(Buffer& buffer) noexcept;
    void CreateImage(uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format,
                     VkImageUsageFlags usage, VkImageAspectFlags aspect, Image& image,
                     bool makeMipViews);
    void DestroyImage(Image& image) noexcept;
    VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect,
                                uint32_t baseMip, uint32_t mipCount);
    uint32_t FindMemoryType(uint32_t bits, VkMemoryPropertyFlags properties) const;
    void UploadBuffer(const void* data, VkDeviceSize size, VkBuffer destination);
    VkCommandBuffer BeginImmediateCommands();
    void EndImmediateCommands(VkCommandBuffer commandBuffer);

    VkShaderModule LoadShader(const char* name);
    VkPipeline CreateComputePipeline(const char* shader, VkPipelineLayout layout);
    VkPipeline CreateGraphicsPipeline(const std::vector<std::pair<const char*, VkShaderStageFlagBits>>& stages,
                                      VkPipelineLayout layout, VkRenderPass renderPass,
                                      const VkPipelineVertexInputStateCreateInfo& vertexInput,
                                      VkPrimitiveTopology topology, bool depthWrite, bool depthTest,
                                      VkCompareOp depthCompare, VkCullModeFlags cullMode,
                                      VkPolygonMode polygonMode, bool tessellated, bool hasColor,
                                      bool alphaBlend = false);

    void RecordFrame(FrameSlot& slot, uint32_t imageIndex, const FrameUniform& uniform,
                     bool captureFrame, bool verifyFrame);
    void BuildOverlay(FrameSlot& slot, const FrameUniform& uniform);
    bool DrawFrame(uint64_t submittedFrame, uint32_t automatedIndex, float delta, float simulationTime);
    void ProcessCompletedFrame(FrameSlot& slot);
    void ProcessAllCompletedFramesInOrder();
    void VerifyFrame(const FrameSlot& slot, const VisibilityStats& stats,
                     const DrawIndirect& indirect);
    void WriteCapture(const FrameSlot& slot);
    void InitializeOutputs();
    void WriteMetadata();
    void UploadOriginalBlades();
    void WaitForNonzeroFramebuffer();
    bool EffectiveOcclusion(const FrameUniform& uniform) const;
    bool EffectiveShadows(const FrameUniform& uniform) const;
    double TimestampMilliseconds(uint64_t begin, uint64_t end) const;
    glm::vec4 DrawTranslation(const SceneDraw& draw, const FrameUniform& uniform) const;

    static void FramebufferResizeCallback(GLFWwindow* window, int width, int height);
    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                        VkDebugUtilsMessageTypeFlagsEXT type,
                                                        const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                        void* userData);

    RunOptions options_;
    bool automated_ = false;
    bool glfwInitialized_ = false;
    bool framebufferResized_ = false;
    bool initialized_ = false;
    bool fillModeNonSolid_ = false;
    bool captureEnabled_ = false;
    bool readbackEnabled_ = false;
    bool benchmarkEnabled_ = false;

    GLFWwindow* window_ = nullptr;
    std::unique_ptr<Scene> scene_;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = std::numeric_limits<uint32_t>::max();
    uint32_t timestampValidBits_ = 0;
    VkPhysicalDeviceProperties deviceProperties_{};
    VkPhysicalDeviceMemoryProperties memoryProperties_{};
    std::atomic<uint32_t> validationErrors_{0};

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    SwapchainState swapchain_;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat shadowFormat_ = VK_FORMAT_UNDEFINED;

    Buffer persistentBlades_;
    Buffer sceneVertices_;
    Buffer sceneIndices_;

    VkDescriptorSetLayout frameSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout dataSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout depthSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout reductionSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkSampler depthSampler_ = VK_NULL_HANDLE;
    VkSampler shadowSampler_ = VK_NULL_HANDLE;

    VkRenderPass depthRenderPass_ = VK_NULL_HANDLE;
    VkRenderPass shadowRenderPass_ = VK_NULL_HANDLE;
    VkRenderPass colorClearRenderPass_ = VK_NULL_HANDLE;
    VkRenderPass colorLoadRenderPass_ = VK_NULL_HANDLE;

    VkPipelineLayout simulationLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout visibilityLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout reductionLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout opaqueLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout skyLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout grassLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout overlayLayout_ = VK_NULL_HANDLE;

    VkPipeline simulationPipeline_ = VK_NULL_HANDLE;
    VkPipeline visibilityPipeline_ = VK_NULL_HANDLE;
    VkPipeline reductionPipeline_ = VK_NULL_HANDLE;
    VkPipeline depthPipeline_ = VK_NULL_HANDLE;
    VkPipeline shadowPipeline_ = VK_NULL_HANDLE;
    VkPipeline opaquePipeline_ = VK_NULL_HANDLE;
    VkPipeline skyPipeline_ = VK_NULL_HANDLE;
    VkPipeline grassPipeline_ = VK_NULL_HANDLE;
    VkPipeline grassWirePipeline_ = VK_NULL_HANDLE;
    VkPipeline overlayPipeline_ = VK_NULL_HANDLE;

    std::array<FrameSlot, kFramesInFlight> frames_{};
    uint64_t frameCursor_ = 0;
    float simulationTime_ = 0.0f;
    std::chrono::steady_clock::time_point previousFrameTime_{};

    std::ofstream benchmarkFile_;
    std::ofstream physicsProbeFile_;
    std::vector<float> staticObstacleDistances_;
    std::vector<uint32_t> physicsProbeIndices_;
    uint32_t benchmarkRowsWritten_ = 0;
    uint32_t captureFramesWritten_ = 0;
    uint32_t verificationFramesProcessed_ = 0;
    VisibilityStats latestStats_{};
    FrameTimings latestTimings_{};
    FrameUniform latestUniformSnapshot_{};
    bool hasCompletedFrame_ = false;
    std::vector<std::string> verificationFailures_;
};

Renderer::Impl::Impl(const RunOptions& options)
    : options_(options),
      automated_(!options.benchmarkPath.empty() || !options.captureDir.empty() || options.verify),
      captureEnabled_(!options.captureDir.empty()),
      readbackEnabled_(!options.benchmarkPath.empty() || options.verify || options.captureDir.empty()),
      benchmarkEnabled_(!options.benchmarkPath.empty()) {
    if (!options_.physicsProbePath.empty() && !options_.verify) {
        throw std::invalid_argument("A physics probe requires verification to be enabled");
    }
    try {
        Initialize();
        initialized_ = true;
    } catch (...) {
        Cleanup();
        throw;
    }
}

Renderer::Impl::~Impl() {
    Cleanup();
}

Renderer::Renderer(const RunOptions& options) : impl_(std::make_unique<Impl>(options)) {}
Renderer::~Renderer() = default;
int Renderer::Run() { return impl_->Run(); }

void Renderer::Impl::Initialize() {
    CreateWindow();
    CreateInstance();
    CreateSurface();
    PickPhysicalDevice();
    CreateDevice();
    CreateCommandPool();

    scene_ = std::make_unique<Scene>(options_);
    if (scene_->Blades().size() != options_.bladeCount) {
        std::ostringstream message;
        message << "Scene blade count (" << scene_->Blades().size()
                << ") does not match RunOptions.bladeCount (" << options_.bladeCount << ')';
        throw std::runtime_error(message.str());
    }

    CreateSceneResources();
    CreateDescriptorLayouts();
    CreateSamplers();
    CreateSwapchain();
    CreateRenderPasses();
    CreatePipelines();
    CreateFrameResources();
    InitializeOutputs();

    std::cout << "GPU: " << deviceProperties_.deviceName << '\n'
              << "Vendor/device: 0x" << std::hex << deviceProperties_.vendorID << "/0x"
              << deviceProperties_.deviceID << std::dec << '\n'
              << "Driver: " << DriverVersionString(deviceProperties_)
              << " (raw " << deviceProperties_.driverVersion << ")\n"
              << "Vulkan API: " << VulkanVersion(deviceProperties_.apiVersion) << '\n'
              << "Queue family: " << queueFamily_ << " (graphics + compute + present), timestamp bits: "
              << timestampValidBits_ << '\n'
              << "Limits: max tessellation level " << deviceProperties_.limits.maxTessellationGenerationLevel
              << ", max compute work-group count X " << deviceProperties_.limits.maxComputeWorkGroupCount[0]
              << ", max storage buffer range " << deviceProperties_.limits.maxStorageBufferRange << '\n'
              << "Resolution: " << swapchain_.extent.width << 'x' << swapchain_.extent.height
              << ", blades: " << options_.bladeCount << '\n';
}

void Renderer::Impl::CreateWindow() {
    if (glfwInit() != GLFW_TRUE) {
        throw std::runtime_error("GLFW initialization failed (a real display is required)");
    }
    glfwInitialized_ = true;
    if (glfwVulkanSupported() != GLFW_TRUE) {
        throw std::runtime_error("GLFW reports that Vulkan is unavailable");
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, options_.hidden ? GLFW_FALSE : GLFW_TRUE);
    window_ = glfwCreateWindow(static_cast<int>(options_.width), static_cast<int>(options_.height),
                               "Vulkan Grass Rendering", nullptr, nullptr);
    if (!window_) {
        throw std::runtime_error("GLFW window creation failed");
    }
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, FramebufferResizeCallback);
}

void Renderer::Impl::CreateInstance() {
    uint32_t loaderVersion = VK_API_VERSION_1_0;
    const auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    if (enumerateVersion) {
        Check(enumerateVersion(&loaderVersion), "vkEnumerateInstanceVersion");
    }
    if (loaderVersion < VK_API_VERSION_1_1) {
        throw std::runtime_error("This renderer requires a Vulkan 1.1 or newer loader");
    }

    const char* validationLayer = "VK_LAYER_KHRONOS_validation";
    if (options_.validation && !HasLayer(validationLayer)) {
        throw std::runtime_error(
            "Validation was requested, but VK_LAYER_KHRONOS_validation is not installed");
    }

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (!glfwExtensions || glfwExtensionCount == 0) {
        throw std::runtime_error("GLFW supplied no Vulkan surface extensions");
    }
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    if (options_.validation) {
        if (!HasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            throw std::runtime_error("Validation was requested, but VK_EXT_debug_utils is unavailable");
        }
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "Vulkan Grass Rendering";
    application.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    application.pEngineName = "Original CIS 5650 Renderer";
    application.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    application.apiVersion = VK_API_VERSION_1_1;

    VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
    debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugInfo.pfnUserCallback = DebugCallback;
    debugInfo.pUserData = this;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &application;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    if (options_.validation) {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = &validationLayer;
        createInfo.pNext = &debugInfo;
    }
    Check(vkCreateInstance(&createInfo, nullptr, &instance_), "vkCreateInstance");

    if (options_.validation) {
        const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        if (!createMessenger) {
            throw std::runtime_error("vkCreateDebugUtilsMessengerEXT is unavailable");
        }
        Check(createMessenger(instance_, &debugInfo, nullptr, &debugMessenger_),
              "vkCreateDebugUtilsMessengerEXT");
    }
}

void Renderer::Impl::CreateSurface() {
    Check(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_),
          "glfwCreateWindowSurface");
}

void Renderer::Impl::PickPhysicalDevice() {
    uint32_t deviceCount = 0;
    Check(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr), "vkEnumeratePhysicalDevices");
    if (deviceCount == 0) {
        throw std::runtime_error("No Vulkan physical devices were found");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    Check(vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()),
          "vkEnumeratePhysicalDevices");

    int bestScore = -1;
    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties properties{};
        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        vkGetPhysicalDeviceFeatures(candidate, &features);
        if (properties.apiVersion < VK_API_VERSION_1_1 || !features.tessellationShader ||
            !HasDeviceExtension(candidate, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            continue;
        }

        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        std::optional<uint32_t> commonFamily;
        for (uint32_t family = 0; family < familyCount; ++family) {
            VkBool32 present = VK_FALSE;
            if (vkGetPhysicalDeviceSurfaceSupportKHR(candidate, family, surface_, &present) != VK_SUCCESS) {
                continue;
            }
            const VkQueueFlags required = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            if ((families[family].queueFlags & required) == required && present == VK_TRUE &&
                families[family].queueCount > 0) {
                commonFamily = family;
                break;
            }
        }
        if (!commonFamily) {
            continue;
        }

        uint32_t formatCount = 0;
        uint32_t modeCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface_, &formatCount, nullptr);
        vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface_, &modeCount, nullptr);
        if (formatCount == 0 || modeCount == 0) {
            continue;
        }

        const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 : 100;
        if (score > bestScore) {
            bestScore = score;
            physicalDevice_ = candidate;
            queueFamily_ = *commonFamily;
            deviceProperties_ = properties;
            timestampValidBits_ = families[*commonFamily].timestampValidBits;
            fillModeNonSolid_ = features.fillModeNonSolid == VK_TRUE;
        }
    }
    if (physicalDevice_ == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "No Vulkan 1.1 device has tessellation and one graphics+compute+present queue");
    }
    if (timestampValidBits_ == 0 || !deviceProperties_.limits.timestampComputeAndGraphics) {
        throw std::runtime_error(
            "Selected device/queue does not support graphics and compute timestamps");
    }
    if (options_.preset == "wireframe" && !fillModeNonSolid_) {
        throw std::runtime_error("The wireframe preset requires fillModeNonSolid support");
    }
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties_);

    const VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                        VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    const std::array<VkFormat, 3> depthCandidates{
        VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D16_UNORM};
    for (VkFormat candidate : depthCandidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, candidate, &properties);
        if ((properties.optimalTilingFeatures & needed) == needed) {
            depthFormat_ = candidate;
            break;
        }
    }
    if (depthFormat_ == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("No supported sampled depth-attachment format was found");
    }
    shadowFormat_ = depthFormat_;
    if (deviceProperties_.limits.maxImageDimension2D < kShadowExtent ||
        deviceProperties_.limits.maxFramebufferWidth < kShadowExtent ||
        deviceProperties_.limits.maxFramebufferHeight < kShadowExtent) {
        throw std::runtime_error("The selected device cannot create a 2048x2048 shadow map");
    }
    VkFormatProperties hierarchyProperties{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice_, VK_FORMAT_R32_SFLOAT,
                                        &hierarchyProperties);
    const VkFormatFeatureFlags hierarchyNeeded = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
                                                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((hierarchyProperties.optimalTilingFeatures & hierarchyNeeded) != hierarchyNeeded) {
        throw std::runtime_error(
            "VK_FORMAT_R32_SFLOAT does not support sampled storage images");
    }
    const VkDeviceSize bladeBytes = std::max<VkDeviceSize>(
        sizeof(Blade), static_cast<VkDeviceSize>(options_.bladeCount) * sizeof(Blade));
    if (bladeBytes > deviceProperties_.limits.maxStorageBufferRange) {
        throw std::runtime_error("Requested blade state exceeds maxStorageBufferRange");
    }
    const uint32_t workgroups = DivideRoundUp(options_.bladeCount, kComputeGroupSize);
    if (workgroups > deviceProperties_.limits.maxComputeWorkGroupCount[0] ||
        kComputeGroupSize > deviceProperties_.limits.maxComputeWorkGroupSize[0] ||
        kComputeGroupSize > deviceProperties_.limits.maxComputeWorkGroupInvocations) {
        throw std::runtime_error("Requested blade dispatch exceeds compute work-group limits");
    }
}

void Renderer::Impl::CreateDevice() {
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkPhysicalDeviceFeatures features{};
    features.tessellationShader = VK_TRUE;
    features.fillModeNonSolid = fillModeNonSolid_ ? VK_TRUE : VK_FALSE;

    const char* extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueInfo;
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = &extension;
    createInfo.pEnabledFeatures = &features;
    Check(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_), "vkCreateDevice");
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
}

void Renderer::Impl::CreateCommandPool() {
    VkCommandPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.queueFamilyIndex = queueFamily_;
    info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
                 VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    Check(vkCreateCommandPool(device_, &info, nullptr, &commandPool_), "vkCreateCommandPool");
}

void Renderer::Impl::FramebufferResizeCallback(GLFWwindow* window, int, int) {
    auto* renderer = static_cast<Impl*>(glfwGetWindowUserPointer(window));
    if (renderer) {
        renderer->framebufferResized_ = true;
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL Renderer::Impl::DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData) {
    auto* renderer = static_cast<Impl*>(userData);
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        renderer->validationErrors_.fetch_add(1, std::memory_order_relaxed);
    }
    std::cerr << "Vulkan "
              << ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "error: " : "warning: ")
              << (data && data->pMessage ? data->pMessage : "(no message)") << '\n';
    return VK_FALSE;
}

uint32_t Renderer::Impl::FindMemoryType(uint32_t bits, VkMemoryPropertyFlags properties) const {
    for (uint32_t index = 0; index < memoryProperties_.memoryTypeCount; ++index) {
        if ((bits & (1u << index)) != 0 &&
            (memoryProperties_.memoryTypes[index].propertyFlags & properties) == properties) {
            return index;
        }
    }
    throw std::runtime_error("No Vulkan memory type satisfies the requested properties");
}

void Renderer::Impl::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                  VkMemoryPropertyFlags properties, Buffer& buffer, bool map) {
    buffer.size = size;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    Check(vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer.handle), "vkCreateBuffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, buffer.handle, &requirements);
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, properties);
    Check(vkAllocateMemory(device_, &allocation, nullptr, &buffer.memory), "vkAllocateMemory(buffer)");
    Check(vkBindBufferMemory(device_, buffer.handle, buffer.memory, 0), "vkBindBufferMemory");
    if (map) {
        Check(vkMapMemory(device_, buffer.memory, 0, size, 0, &buffer.mapped), "vkMapMemory");
    }
}

void Renderer::Impl::DestroyBuffer(Buffer& buffer) noexcept {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    if (buffer.mapped && buffer.memory) {
        vkUnmapMemory(device_, buffer.memory);
    }
    if (buffer.handle) {
        vkDestroyBuffer(device_, buffer.handle, nullptr);
    }
    if (buffer.memory) {
        vkFreeMemory(device_, buffer.memory, nullptr);
    }
    buffer = {};
}

VkImageView Renderer::Impl::CreateImageView(VkImage image, VkFormat format,
                                             VkImageAspectFlags aspect, uint32_t baseMip,
                                             uint32_t mipCount) {
    VkImageViewCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.subresourceRange.aspectMask = aspect;
    info.subresourceRange.baseMipLevel = baseMip;
    info.subresourceRange.levelCount = mipCount;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;
    VkImageView view = VK_NULL_HANDLE;
    Check(vkCreateImageView(device_, &info, nullptr, &view), "vkCreateImageView");
    return view;
}

void Renderer::Impl::CreateImage(uint32_t width, uint32_t height, uint32_t mipLevels,
                                 VkFormat format, VkImageUsageFlags usage,
                                 VkImageAspectFlags aspect, Image& image, bool makeMipViews) {
    image.width = width;
    image.height = height;
    image.mipLevels = mipLevels;
    image.format = format;

    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1};
    info.mipLevels = mipLevels;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    Check(vkCreateImage(device_, &info, nullptr, &image.handle), "vkCreateImage");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, image.handle, &requirements);
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits,
                                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(device_, &allocation, nullptr, &image.memory), "vkAllocateMemory(image)");
    Check(vkBindImageMemory(device_, image.handle, image.memory, 0), "vkBindImageMemory");
    image.view = CreateImageView(image.handle, format, aspect, 0, mipLevels);
    if (makeMipViews) {
        image.mipViews.reserve(mipLevels);
        for (uint32_t mip = 0; mip < mipLevels; ++mip) {
            image.mipViews.push_back(CreateImageView(image.handle, format, aspect, mip, 1));
        }
    }
}

void Renderer::Impl::DestroyImage(Image& image) noexcept {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    for (VkImageView view : image.mipViews) {
        if (view) {
            vkDestroyImageView(device_, view, nullptr);
        }
    }
    if (image.view) {
        vkDestroyImageView(device_, image.view, nullptr);
    }
    if (image.handle) {
        vkDestroyImage(device_, image.handle, nullptr);
    }
    if (image.memory) {
        vkFreeMemory(device_, image.memory, nullptr);
    }
    image = {};
}

VkCommandBuffer Renderer::Impl::BeginImmediateCommands() {
    VkCommandBufferAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocation.commandPool = commandPool_;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    Check(vkAllocateCommandBuffers(device_, &allocation, &commandBuffer),
          "vkAllocateCommandBuffers(immediate)");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(vkBeginCommandBuffer(commandBuffer, &begin), "vkBeginCommandBuffer(immediate)");
    return commandBuffer;
}

void Renderer::Impl::EndImmediateCommands(VkCommandBuffer commandBuffer) {
    Check(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer(immediate)");
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    Check(vkCreateFence(device_, &fenceInfo, nullptr, &fence), "vkCreateFence(immediate)");
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer;
    Check(vkQueueSubmit(queue_, 1, &submit, fence), "vkQueueSubmit(immediate)");
    Check(vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences(immediate)");
    vkDestroyFence(device_, fence, nullptr);
    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
}

void Renderer::Impl::UploadBuffer(const void* data, VkDeviceSize size, VkBuffer destination) {
    Buffer staging;
    CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 staging, true);
    std::memcpy(staging.mapped, data, static_cast<size_t>(size));
    VkCommandBuffer commandBuffer = BeginImmediateCommands();
    VkBufferCopy copy{0, 0, size};
    vkCmdCopyBuffer(commandBuffer, staging.handle, destination, 1, &copy);
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                            VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT |
                            VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = destination;
    barrier.offset = 0;
    barrier.size = size;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                             VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                         0, 0, nullptr, 1, &barrier, 0, nullptr);
    EndImmediateCommands(commandBuffer);
    DestroyBuffer(staging);
}

void Renderer::Impl::CreateSceneResources() {
    const std::vector<Blade>& originalBlades = scene_->Blades();
    staticObstacleDistances_.clear();
    physicsProbeIndices_.clear();
    if (options_.verify) {
        staticObstacleDistances_.reserve(originalBlades.size());
        for (const Blade& blade : originalBlades) {
            staticObstacleDistances_.push_back(
                scene_->StaticObstacleDistance(glm::vec3(blade.v0)));
        }
    }
    if (!options_.physicsProbePath.empty()) {
        physicsProbeIndices_.reserve(originalBlades.size());
        for (size_t index = 0; index < originalBlades.size(); ++index) {
            const glm::vec3 root(originalBlades[index].v0);
            if (std::abs(root.x) <= 0.35f && std::abs(root.z) <= 0.25f) {
                physicsProbeIndices_.push_back(static_cast<uint32_t>(index));
            }
        }
    }

    const VkDeviceSize bladeBytes = std::max<VkDeviceSize>(
        sizeof(Blade), static_cast<VkDeviceSize>(originalBlades.size()) * sizeof(Blade));
    CreateBuffer(bladeBytes,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, persistentBlades_, false);
    UploadOriginalBlades();

    if (!scene_->Vertices().empty()) {
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(scene_->Vertices().size()) * sizeof(SceneVertex);
        CreateBuffer(bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sceneVertices_, false);
        UploadBuffer(scene_->Vertices().data(), bytes, sceneVertices_.handle);
    }
    if (!scene_->Indices().empty()) {
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(scene_->Indices().size()) * sizeof(uint32_t);
        CreateBuffer(bytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sceneIndices_, false);
        UploadBuffer(scene_->Indices().data(), bytes, sceneIndices_.handle);
    }
}

void Renderer::Impl::UploadOriginalBlades() {
    if (device_ == VK_NULL_HANDLE || persistentBlades_.handle == VK_NULL_HANDLE || !scene_) {
        return;
    }
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(scene_->Blades().size()) * sizeof(Blade);
    if (bytes == 0) {
        return;
    }
    UploadBuffer(scene_->Blades().data(), bytes, persistentBlades_.handle);
}

void Renderer::Impl::CreateDescriptorLayouts() {
    std::array<VkDescriptorSetLayoutBinding, 2> frameBindings{};
    frameBindings[0].binding = 0;
    frameBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    frameBindings[0].descriptorCount = 1;
    frameBindings[0].stageFlags = VK_SHADER_STAGE_ALL;
    frameBindings[1].binding = 1;
    frameBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    frameBindings[1].descriptorCount = 1;
    frameBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo frameInfo{};
    frameInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    frameInfo.bindingCount = static_cast<uint32_t>(frameBindings.size());
    frameInfo.pBindings = frameBindings.data();
    Check(vkCreateDescriptorSetLayout(device_, &frameInfo, nullptr, &frameSetLayout_),
          "vkCreateDescriptorSetLayout(frame)");

    std::array<VkDescriptorSetLayoutBinding, 4> dataBindings{};
    for (uint32_t index = 0; index < dataBindings.size(); ++index) {
        dataBindings[index].binding = index;
        dataBindings[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        dataBindings[index].descriptorCount = 1;
        dataBindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dataInfo{};
    dataInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dataInfo.bindingCount = static_cast<uint32_t>(dataBindings.size());
    dataInfo.pBindings = dataBindings.data();
    Check(vkCreateDescriptorSetLayout(device_, &dataInfo, nullptr, &dataSetLayout_),
          "vkCreateDescriptorSetLayout(data)");

    VkDescriptorSetLayoutBinding depthBinding{};
    depthBinding.binding = 0;
    depthBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthBinding.descriptorCount = 1;
    depthBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo depthInfo{};
    depthInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    depthInfo.bindingCount = 1;
    depthInfo.pBindings = &depthBinding;
    Check(vkCreateDescriptorSetLayout(device_, &depthInfo, nullptr, &depthSetLayout_),
          "vkCreateDescriptorSetLayout(depth)");

    std::array<VkDescriptorSetLayoutBinding, 2> reduceBindings{};
    reduceBindings[0] = depthBinding;
    reduceBindings[1].binding = 1;
    reduceBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    reduceBindings[1].descriptorCount = 1;
    reduceBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo reduceInfo{};
    reduceInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    reduceInfo.bindingCount = static_cast<uint32_t>(reduceBindings.size());
    reduceInfo.pBindings = reduceBindings.data();
    Check(vkCreateDescriptorSetLayout(device_, &reduceInfo, nullptr, &reductionSetLayout_),
          "vkCreateDescriptorSetLayout(reduction)");
}

void Renderer::Impl::CreateSamplers() {
    VkSamplerCreateInfo depthInfo{};
    depthInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    depthInfo.magFilter = VK_FILTER_NEAREST;
    depthInfo.minFilter = VK_FILTER_NEAREST;
    depthInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    depthInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    depthInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    depthInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    depthInfo.minLod = 0.0f;
    depthInfo.maxLod = VK_LOD_CLAMP_NONE;
    Check(vkCreateSampler(device_, &depthInfo, nullptr, &depthSampler_),
          "vkCreateSampler(depth)");

    VkSamplerCreateInfo shadowInfo{};
    shadowInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    shadowInfo.magFilter = VK_FILTER_NEAREST;
    shadowInfo.minFilter = VK_FILTER_NEAREST;
    shadowInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    shadowInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadowInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadowInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadowInfo.compareEnable = VK_TRUE;
    shadowInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    shadowInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    shadowInfo.minLod = 0.0f;
    shadowInfo.maxLod = 0.0f;
    Check(vkCreateSampler(device_, &shadowInfo, nullptr, &shadowSampler_),
          "vkCreateSampler(shadow)");
}

void Renderer::Impl::WaitForNonzeroFramebuffer() {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    while ((width == 0 || height == 0) && glfwWindowShouldClose(window_) == GLFW_FALSE) {
        glfwWaitEvents();
        glfwGetFramebufferSize(window_, &width, &height);
    }
}

void Renderer::Impl::CreateSwapchain() {
    WaitForNonzeroFramebuffer();
    VkSurfaceCapabilitiesKHR capabilities{};
    Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &capabilities),
          "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    uint32_t formatCount = 0;
    Check(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr),
          "vkGetPhysicalDeviceSurfaceFormatsKHR");
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    Check(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, formats.data()),
          "vkGetPhysicalDeviceSurfaceFormatsKHR");
    if (formats.empty()) {
        throw std::runtime_error("Surface exposes no swapchain formats");
    }

    const std::array<VkFormat, 4> preferred{
        VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB,
        VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
    VkSurfaceFormatKHR chosen = formats.front();
    if (formats.size() == 1 && formats.front().format == VK_FORMAT_UNDEFINED) {
        chosen.format = preferred.front();
        chosen.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    } else {
        for (VkFormat candidate : preferred) {
            const auto found = std::find_if(formats.begin(), formats.end(),
                                            [candidate](const auto& format) {
                return format.format == candidate &&
                       format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            });
            if (found != formats.end()) {
                chosen = *found;
                break;
            }
        }
    }
    if (captureEnabled_ && chosen.format != VK_FORMAT_B8G8R8A8_SRGB &&
        chosen.format != VK_FORMAT_B8G8R8A8_UNORM && chosen.format != VK_FORMAT_R8G8B8A8_SRGB &&
        chosen.format != VK_FORMAT_R8G8B8A8_UNORM) {
        throw std::runtime_error("Capture requires an RGBA8 or BGRA8 swapchain format");
    }
    if (captureEnabled_ && !(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
        throw std::runtime_error("Capture requested, but swapchain images do not support TRANSFER_SRC");
    }
    if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
        throw std::runtime_error("Surface swapchain images do not support COLOR_ATTACHMENT usage");
    }

    VkExtent2D extent{};
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        extent = capabilities.currentExtent;
    } else {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        extent.width = std::clamp(static_cast<uint32_t>(std::max(width, 1)),
                                  capabilities.minImageExtent.width,
                                  capabilities.maxImageExtent.width);
        extent.height = std::clamp(static_cast<uint32_t>(std::max(height, 1)),
                                   capabilities.minImageExtent.height,
                                   capabilities.maxImageExtent.height);
    }

    uint32_t imageCount = std::max(capabilities.minImageCount + 1u, 2u);
    if (capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }
    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    const std::array<VkCompositeAlphaFlagBitsKHR, 4> alphaModes{
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
    for (auto mode : alphaModes) {
        if (capabilities.supportedCompositeAlpha & mode) {
            compositeAlpha = mode;
            break;
        }
    }

    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = surface_;
    info.minImageCount = imageCount;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (captureEnabled_) {
        imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    info.imageUsage = imageUsage;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = capabilities.currentTransform;
    info.compositeAlpha = compositeAlpha;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    Check(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_.handle), "vkCreateSwapchainKHR");

    swapchain_.format = chosen.format;
    swapchain_.colorSpace = chosen.colorSpace;
    swapchain_.extent = extent;
    Check(vkGetSwapchainImagesKHR(device_, swapchain_.handle, &imageCount, nullptr),
          "vkGetSwapchainImagesKHR");
    swapchain_.images.resize(imageCount);
    Check(vkGetSwapchainImagesKHR(device_, swapchain_.handle, &imageCount, swapchain_.images.data()),
          "vkGetSwapchainImagesKHR");
    swapchain_.views.reserve(imageCount);
    swapchain_.renderFinished.reserve(imageCount);
    swapchain_.imageFences.assign(imageCount, VK_NULL_HANDLE);
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (VkImage image : swapchain_.images) {
        swapchain_.views.push_back(CreateImageView(image, swapchain_.format,
                                                   VK_IMAGE_ASPECT_COLOR_BIT, 0, 1));
        VkSemaphore semaphore = VK_NULL_HANDLE;
        Check(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &semaphore),
              "vkCreateSemaphore(render finished)");
        swapchain_.renderFinished.push_back(semaphore);
    }
}

void Renderer::Impl::DestroySwapchain() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    for (VkSemaphore semaphore : swapchain_.renderFinished) {
        if (semaphore) {
            vkDestroySemaphore(device_, semaphore, nullptr);
        }
    }
    for (VkImageView view : swapchain_.views) {
        if (view) {
            vkDestroyImageView(device_, view, nullptr);
        }
    }
    if (swapchain_.handle) {
        vkDestroySwapchainKHR(device_, swapchain_.handle, nullptr);
    }
    swapchain_ = {};
}

void Renderer::Impl::CreateRenderPasses() {
    VkAttachmentDescription shadowAttachment{};
    shadowAttachment.format = shadowFormat_;
    shadowAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    shadowAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    shadowAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    shadowAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    shadowAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    shadowAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    shadowAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkAttachmentReference shadowReference{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription shadowSubpass{};
    shadowSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    shadowSubpass.pDepthStencilAttachment = &shadowReference;
    std::array<VkSubpassDependency, 2> shadowDependencies{};
    shadowDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    shadowDependencies[0].dstSubpass = 0;
    shadowDependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    shadowDependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                         VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    shadowDependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    shadowDependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    shadowDependencies[1].srcSubpass = 0;
    shadowDependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    shadowDependencies[1].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                         VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    shadowDependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    shadowDependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    shadowDependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo shadowInfo{};
    shadowInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    shadowInfo.attachmentCount = 1;
    shadowInfo.pAttachments = &shadowAttachment;
    shadowInfo.subpassCount = 1;
    shadowInfo.pSubpasses = &shadowSubpass;
    shadowInfo.dependencyCount = static_cast<uint32_t>(shadowDependencies.size());
    shadowInfo.pDependencies = shadowDependencies.data();
    Check(vkCreateRenderPass(device_, &shadowInfo, nullptr, &shadowRenderPass_),
          "vkCreateRenderPass(shadow)");

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat_;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkAttachmentReference depthReference{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription depthSubpass{};
    depthSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    depthSubpass.pDepthStencilAttachment = &depthReference;
    std::array<VkSubpassDependency, 2> depthDependencies{};
    depthDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    depthDependencies[0].dstSubpass = 0;
    depthDependencies[0].srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    depthDependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    depthDependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
                                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthDependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthDependencies[1].srcSubpass = 0;
    depthDependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    depthDependencies[1].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    depthDependencies[1].dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    depthDependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthDependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo depthInfo{};
    depthInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    depthInfo.attachmentCount = 1;
    depthInfo.pAttachments = &depthAttachment;
    depthInfo.subpassCount = 1;
    depthInfo.pSubpasses = &depthSubpass;
    depthInfo.dependencyCount = static_cast<uint32_t>(depthDependencies.size());
    depthInfo.pDependencies = depthDependencies.data();
    Check(vkCreateRenderPass(device_, &depthInfo, nullptr, &depthRenderPass_),
          "vkCreateRenderPass(depth)");

    auto makeColorPass = [this](bool loadDepth, VkRenderPass& renderPass) {
        std::array<VkAttachmentDescription, 2> attachments{};
        attachments[0].format = swapchain_.format;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = captureEnabled_ ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                                     : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        attachments[1].format = depthFormat_;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = loadDepth ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = loadDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                 : VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference colorReference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;
        subpass.pDepthStencilAttachment = &depthRef;
        std::array<VkSubpassDependency, 2> dependencies{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].dstStageMask = dependencies[0].srcStageMask;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = captureEnabled_ ? VK_PIPELINE_STAGE_TRANSFER_BIT
                                                       : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = captureEnabled_
            ? static_cast<VkAccessFlags>(VK_ACCESS_TRANSFER_READ_BIT)
            : VkAccessFlags{0};
        VkRenderPassCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = static_cast<uint32_t>(attachments.size());
        info.pAttachments = attachments.data();
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = static_cast<uint32_t>(dependencies.size());
        info.pDependencies = dependencies.data();
        Check(vkCreateRenderPass(device_, &info, nullptr, &renderPass),
              loadDepth ? "vkCreateRenderPass(color load)" : "vkCreateRenderPass(color clear)");
    };
    makeColorPass(false, colorClearRenderPass_);
    makeColorPass(true, colorLoadRenderPass_);
}

void Renderer::Impl::DestroyRenderPasses() {
    if (!device_) {
        return;
    }
    if (colorLoadRenderPass_) vkDestroyRenderPass(device_, colorLoadRenderPass_, nullptr);
    if (colorClearRenderPass_) vkDestroyRenderPass(device_, colorClearRenderPass_, nullptr);
    if (depthRenderPass_) vkDestroyRenderPass(device_, depthRenderPass_, nullptr);
    if (shadowRenderPass_) vkDestroyRenderPass(device_, shadowRenderPass_, nullptr);
    colorLoadRenderPass_ = colorClearRenderPass_ = depthRenderPass_ =
        shadowRenderPass_ = VK_NULL_HANDLE;
}

VkShaderModule Renderer::Impl::LoadShader(const char* name) {
    const std::filesystem::path filename = std::string(name) + ".spv";
    std::vector<std::filesystem::path> candidates;
    if (const std::optional<std::filesystem::path> executableDirectory = ExecutableDirectory()) {
        candidates.push_back(*executableDirectory / "shaders" / filename);
    }
    const std::filesystem::path configuredPath =
        std::filesystem::path(VULKAN_GRASS_SHADER_DIR) / filename;
    if (std::find(candidates.begin(), candidates.end(), configuredPath) == candidates.end()) {
        candidates.push_back(configuredPath);
    }

    std::optional<std::filesystem::path> shaderPath;
    for (const std::filesystem::path& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error) && !error) {
            shaderPath = candidate;
            break;
        }
    }
    if (!shaderPath) {
        std::ostringstream message;
        message << "Unable to locate shader " << filename.string() << "; searched";
        for (const std::filesystem::path& candidate : candidates) {
            message << " '" << candidate.string() << '\'';
        }
        throw std::runtime_error(message.str());
    }

    const std::vector<uint32_t> code = ReadSpirv(*shaderPath);
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size() * sizeof(uint32_t);
    info.pCode = code.data();
    VkShaderModule module = VK_NULL_HANDLE;
    Check(vkCreateShaderModule(device_, &info, nullptr, &module), "vkCreateShaderModule");
    return module;
}

VkPipeline Renderer::Impl::CreateComputePipeline(const char* shader, VkPipelineLayout layout) {
    const VkShaderModule module = LoadShader(shader);
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";
    VkComputePipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage = stage;
    info.layout = layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(device_, module, nullptr);
    Check(result, "vkCreateComputePipelines");
    return pipeline;
}

VkPipeline Renderer::Impl::CreateGraphicsPipeline(
    const std::vector<std::pair<const char*, VkShaderStageFlagBits>>& shaderStages,
    VkPipelineLayout layout, VkRenderPass renderPass,
    const VkPipelineVertexInputStateCreateInfo& vertexInput, VkPrimitiveTopology topology,
    bool depthWrite, bool depthTest, VkCompareOp depthCompare, VkCullModeFlags cullMode,
    VkPolygonMode polygonMode, bool tessellated, bool hasColor, bool alphaBlend) {
    std::vector<VkShaderModule> modules;
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    modules.reserve(shaderStages.size());
    stages.reserve(shaderStages.size());
    try {
        for (const auto& [name, stageFlag] : shaderStages) {
            modules.push_back(LoadShader(name));
            VkPipelineShaderStageCreateInfo stage{};
            stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stage.stage = stageFlag;
            stage.module = modules.back();
            stage.pName = "main";
            stages.push_back(stage);
        }

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = topology;
        VkPipelineViewportStateCreateInfo viewport{};
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rasterization{};
        rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterization.polygonMode = polygonMode;
        rasterization.cullMode = cullMode;
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterization.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = depthTest ? VK_TRUE : VK_FALSE;
        depth.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        depth.depthCompareOp = depthCompare;
        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (alphaBlend) {
            blendAttachment.blendEnable = VK_TRUE;
            blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = hasColor ? 1u : 0u;
        blend.pAttachments = hasColor ? &blendAttachment : nullptr;
        const std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT,
                                                         VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamic.pDynamicStates = dynamicStates.data();
        VkPipelineTessellationStateCreateInfo tessellation{};
        tessellation.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
        tessellation.patchControlPoints = 1;

        VkGraphicsPipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount = static_cast<uint32_t>(stages.size());
        info.pStages = stages.data();
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &inputAssembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &rasterization;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.pTessellationState = tessellated ? &tessellation : nullptr;
        info.layout = layout;
        info.renderPass = renderPass;
        info.subpass = 0;
        VkPipeline pipeline = VK_NULL_HANDLE;
        const VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info,
                                                          nullptr, &pipeline);
        for (VkShaderModule module : modules) {
            vkDestroyShaderModule(device_, module, nullptr);
        }
        modules.clear();
        Check(result, "vkCreateGraphicsPipelines");
        return pipeline;
    } catch (...) {
        for (VkShaderModule module : modules) {
            vkDestroyShaderModule(device_, module, nullptr);
        }
        throw;
    }
}

void Renderer::Impl::CreatePipelines() {
    auto makeLayout = [this](const std::vector<VkDescriptorSetLayout>& sets,
                             const VkPushConstantRange* push, VkPipelineLayout& layout) {
        VkPipelineLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        info.setLayoutCount = static_cast<uint32_t>(sets.size());
        info.pSetLayouts = sets.data();
        if (push) {
            info.pushConstantRangeCount = 1;
            info.pPushConstantRanges = push;
        }
        Check(vkCreatePipelineLayout(device_, &info, nullptr, &layout), "vkCreatePipelineLayout");
    };
    makeLayout({frameSetLayout_, dataSetLayout_}, nullptr, simulationLayout_);
    makeLayout({frameSetLayout_, dataSetLayout_, depthSetLayout_}, nullptr, visibilityLayout_);
    makeLayout({reductionSetLayout_}, nullptr, reductionLayout_);
    VkPushConstantRange drawPush{};
    drawPush.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    drawPush.offset = 0;
    drawPush.size = sizeof(DrawPushConstant);
    makeLayout({frameSetLayout_}, &drawPush, opaqueLayout_);
    makeLayout({frameSetLayout_}, nullptr, skyLayout_);
    makeLayout({frameSetLayout_}, nullptr, grassLayout_);
    makeLayout({frameSetLayout_}, nullptr, overlayLayout_);

    simulationPipeline_ = CreateComputePipeline("compute.comp", simulationLayout_);
    visibilityPipeline_ = CreateComputePipeline("visibility.comp", visibilityLayout_);
    reductionPipeline_ = CreateComputePipeline("depth_reduce.comp", reductionLayout_);

    const VkVertexInputBindingDescription sceneBinding{0, sizeof(SceneVertex),
                                                        VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array<VkVertexInputAttributeDescription, 3> sceneAttributes{{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(SceneVertex, position))},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(SceneVertex, normal))},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(SceneVertex, color))}}};
    VkPipelineVertexInputStateCreateInfo sceneVertexInput{};
    sceneVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    sceneVertexInput.vertexBindingDescriptionCount = 1;
    sceneVertexInput.pVertexBindingDescriptions = &sceneBinding;
    sceneVertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(sceneAttributes.size());
    sceneVertexInput.pVertexAttributeDescriptions = sceneAttributes.data();
    opaquePipeline_ = CreateGraphicsPipeline(
        {{"graphics.vert", VK_SHADER_STAGE_VERTEX_BIT}, {"graphics.frag", VK_SHADER_STAGE_FRAGMENT_BIT}},
        opaqueLayout_, colorClearRenderPass_, sceneVertexInput, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        true, true, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_BACK_BIT, VK_POLYGON_MODE_FILL,
        false, true);
    depthPipeline_ = CreateGraphicsPipeline({{"graphics.vert", VK_SHADER_STAGE_VERTEX_BIT}},
                                            opaqueLayout_, depthRenderPass_, sceneVertexInput,
                                            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, true,
                                            VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_BACK_BIT,
                                            VK_POLYGON_MODE_FILL, false, false);
    const std::array<VkVertexInputAttributeDescription, 1> shadowAttributes{{
        sceneAttributes[0]}};
    VkPipelineVertexInputStateCreateInfo shadowVertexInput{};
    shadowVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    shadowVertexInput.vertexBindingDescriptionCount = 1;
    shadowVertexInput.pVertexBindingDescriptions = &sceneBinding;
    shadowVertexInput.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(shadowAttributes.size());
    shadowVertexInput.pVertexAttributeDescriptions = shadowAttributes.data();
    shadowPipeline_ = CreateGraphicsPipeline({{"shadow.vert", VK_SHADER_STAGE_VERTEX_BIT}},
                                             opaqueLayout_, shadowRenderPass_, shadowVertexInput,
                                             VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, true,
                                             VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_BACK_BIT,
                                             VK_POLYGON_MODE_FILL, false, false);

    VkPipelineVertexInputStateCreateInfo emptyVertexInput{};
    emptyVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    skyPipeline_ = CreateGraphicsPipeline(
        {{"sky.vert", VK_SHADER_STAGE_VERTEX_BIT}, {"sky.frag", VK_SHADER_STAGE_FRAGMENT_BIT}},
        skyLayout_, colorClearRenderPass_, emptyVertexInput, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        false, false, VK_COMPARE_OP_ALWAYS, VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL, false, true);

    const VkVertexInputBindingDescription bladeBinding{0, sizeof(Blade),
                                                        VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array<VkVertexInputAttributeDescription, 4> bladeAttributes{{
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(offsetof(Blade, v0))},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(offsetof(Blade, v1))},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(offsetof(Blade, v2))},
        {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(offsetof(Blade, up))}}};
    VkPipelineVertexInputStateCreateInfo bladeVertexInput{};
    bladeVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    bladeVertexInput.vertexBindingDescriptionCount = 1;
    bladeVertexInput.pVertexBindingDescriptions = &bladeBinding;
    bladeVertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(bladeAttributes.size());
    bladeVertexInput.pVertexAttributeDescriptions = bladeAttributes.data();
    const std::vector<std::pair<const char*, VkShaderStageFlagBits>> grassStages{
        {"grass.vert", VK_SHADER_STAGE_VERTEX_BIT},
        {"grass.tesc", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT},
        {"grass.tese", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT},
        {"grass.frag", VK_SHADER_STAGE_FRAGMENT_BIT}};
    grassPipeline_ = CreateGraphicsPipeline(grassStages, grassLayout_, colorClearRenderPass_,
                                            bladeVertexInput, VK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
                                            true, true, VK_COMPARE_OP_LESS_OR_EQUAL,
                                            VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL, true, true);
    if (fillModeNonSolid_) {
        grassWirePipeline_ = CreateGraphicsPipeline(grassStages, grassLayout_, colorClearRenderPass_,
                                                    bladeVertexInput, VK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
                                                    true, true, VK_COMPARE_OP_LESS_OR_EQUAL,
                                                    VK_CULL_MODE_NONE, VK_POLYGON_MODE_LINE, true, true);
    }

    const VkVertexInputBindingDescription overlayBinding{0, sizeof(OverlayVertex),
                                                          VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array<VkVertexInputAttributeDescription, 2> overlayAttributes{{
        {0, 0, VK_FORMAT_R32G32_SFLOAT,
         static_cast<uint32_t>(offsetof(OverlayVertex, position))},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
         static_cast<uint32_t>(offsetof(OverlayVertex, color))}}};
    VkPipelineVertexInputStateCreateInfo overlayVertexInput{};
    overlayVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    overlayVertexInput.vertexBindingDescriptionCount = 1;
    overlayVertexInput.pVertexBindingDescriptions = &overlayBinding;
    overlayVertexInput.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(overlayAttributes.size());
    overlayVertexInput.pVertexAttributeDescriptions = overlayAttributes.data();
    overlayPipeline_ = CreateGraphicsPipeline(
        {{"overlay.vert", VK_SHADER_STAGE_VERTEX_BIT},
         {"overlay.frag", VK_SHADER_STAGE_FRAGMENT_BIT}},
        overlayLayout_, colorClearRenderPass_, overlayVertexInput,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false, false, VK_COMPARE_OP_ALWAYS,
        VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL, false, true, true);
}

void Renderer::Impl::DestroyPipelines() {
    if (!device_) return;
    const std::array<VkPipeline*, 10> pipelines{{&overlayPipeline_, &grassWirePipeline_,
        &grassPipeline_, &skyPipeline_, &opaquePipeline_, &shadowPipeline_, &depthPipeline_,
        &reductionPipeline_, &visibilityPipeline_, &simulationPipeline_}};
    for (VkPipeline* pipeline : pipelines) {
        if (*pipeline) vkDestroyPipeline(device_, *pipeline, nullptr);
        *pipeline = VK_NULL_HANDLE;
    }
    const std::array<VkPipelineLayout*, 7> layouts{{&overlayLayout_, &grassLayout_, &skyLayout_,
        &opaqueLayout_, &reductionLayout_, &visibilityLayout_, &simulationLayout_}};
    for (VkPipelineLayout* layout : layouts) {
        if (*layout) vkDestroyPipelineLayout(device_, *layout, nullptr);
        *layout = VK_NULL_HANDLE;
    }
}

void Renderer::Impl::CreateFrameResources() {
    const uint32_t maximumDimension = std::max(swapchain_.extent.width, swapchain_.extent.height);
    uint32_t hierarchyMips = 1;
    for (uint32_t dimension = maximumDimension; dimension > 1; dimension >>= 1u) {
        ++hierarchyMips;
    }
    const VkDeviceSize bladeBytes = std::max<VkDeviceSize>(
        sizeof(Blade), static_cast<VkDeviceSize>(options_.bladeCount) * sizeof(Blade));
    const VkDeviceSize resultBytes = AlignUp(sizeof(VisibilityStats), 16) + sizeof(DrawIndirect);

    VkCommandBufferAllocateInfo commandAllocation{};
    commandAllocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandAllocation.commandPool = commandPool_;
    commandAllocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandAllocation.commandBufferCount = kFramesInFlight;
    std::array<VkCommandBuffer, kFramesInFlight> commandBuffers{};
    Check(vkAllocateCommandBuffers(device_, &commandAllocation, commandBuffers.data()),
          "vkAllocateCommandBuffers(frames)");

    for (uint32_t frameIndex = 0; frameIndex < kFramesInFlight; ++frameIndex) {
        FrameSlot& frame = frames_[frameIndex];
        frame.commandBuffer = commandBuffers[frameIndex];
        CreateBuffer(sizeof(FrameUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     frame.uniform, true);
        CreateBuffer(kOverlayBufferBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     frame.overlay, true);
        CreateBuffer(bladeBytes,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, frame.visibleBlades, false);
        CreateBuffer(sizeof(DrawIndirect),
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, frame.indirect, false);
        const DrawIndirect initialIndirect{};
        UploadBuffer(&initialIndirect, sizeof(initialIndirect), frame.indirect.handle);
        CreateBuffer(sizeof(VisibilityStats),
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, frame.stats, false);
        if (readbackEnabled_) {
            CreateBuffer(resultBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         frame.readback, true);
        }
        if (options_.verify) {
            CreateBuffer(bladeBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         frame.bladeReadback, true);
        }
        if (captureEnabled_) {
            const VkDeviceSize captureBytes = static_cast<VkDeviceSize>(swapchain_.extent.width) *
                                              swapchain_.extent.height * 4;
            CreateBuffer(captureBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         frame.capture, true);
        }

        CreateImage(swapchain_.extent.width, swapchain_.extent.height, 1, depthFormat_,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT, frame.depth, false);
        CreateImage(kShadowExtent, kShadowExtent, 1, shadowFormat_,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT, frame.shadow, false);
        CreateImage(swapchain_.extent.width, swapchain_.extent.height, hierarchyMips,
                    VK_FORMAT_R32_SFLOAT,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, frame.hierarchy, true);

        VkCommandBuffer initializeImage = BeginImmediateCommands();
        VkImageMemoryBarrier shadowToTransfer{};
        shadowToTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        shadowToTransfer.srcAccessMask = 0;
        shadowToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        shadowToTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        shadowToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        shadowToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        shadowToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        shadowToTransfer.image = frame.shadow.handle;
        shadowToTransfer.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(initializeImage, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                             1, &shadowToTransfer);
        const VkClearDepthStencilValue shadowClear{1.0f, 0};
        vkCmdClearDepthStencilImage(initializeImage, frame.shadow.handle,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &shadowClear, 1,
                                    &shadowToTransfer.subresourceRange);
        VkImageMemoryBarrier shadowReady = shadowToTransfer;
        shadowReady.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        shadowReady.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        shadowReady.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        shadowReady.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(initializeImage, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                             1, &shadowReady);

        VkImageMemoryBarrier toGeneral{};
        toGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toGeneral.srcAccessMask = 0;
        toGeneral.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toGeneral.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.image = frame.hierarchy.handle;
        toGeneral.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, hierarchyMips, 0, 1};
        vkCmdPipelineBarrier(initializeImage, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                             1, &toGeneral);
        const VkClearColorValue farDepth{{1.0f, 1.0f, 1.0f, 1.0f}};
        vkCmdClearColorImage(initializeImage, frame.hierarchy.handle, VK_IMAGE_LAYOUT_GENERAL,
                             &farDepth, 1, &toGeneral.subresourceRange);
        VkImageMemoryBarrier ready = toGeneral;
        ready.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        ready.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        ready.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        ready.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        vkCmdPipelineBarrier(initializeImage, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                             1, &ready);
        EndImmediateCommands(initializeImage);

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        Check(vkCreateFence(device_, &fenceInfo, nullptr, &frame.fence), "vkCreateFence(frame)");
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        Check(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &frame.imageAcquired),
              "vkCreateSemaphore(image acquired)");
        VkQueryPoolCreateInfo queryInfo{};
        queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryInfo.queryCount = kQueryCount;
        Check(vkCreateQueryPool(device_, &queryInfo, nullptr, &frame.queryPool),
              "vkCreateQueryPool");

        VkFramebufferCreateInfo depthFramebufferInfo{};
        depthFramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        depthFramebufferInfo.renderPass = depthRenderPass_;
        depthFramebufferInfo.attachmentCount = 1;
        depthFramebufferInfo.pAttachments = &frame.depth.view;
        depthFramebufferInfo.width = swapchain_.extent.width;
        depthFramebufferInfo.height = swapchain_.extent.height;
        depthFramebufferInfo.layers = 1;
        Check(vkCreateFramebuffer(device_, &depthFramebufferInfo, nullptr, &frame.depthFramebuffer),
              "vkCreateFramebuffer(depth)");

        VkFramebufferCreateInfo shadowFramebufferInfo{};
        shadowFramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        shadowFramebufferInfo.renderPass = shadowRenderPass_;
        shadowFramebufferInfo.attachmentCount = 1;
        shadowFramebufferInfo.pAttachments = &frame.shadow.view;
        shadowFramebufferInfo.width = kShadowExtent;
        shadowFramebufferInfo.height = kShadowExtent;
        shadowFramebufferInfo.layers = 1;
        Check(vkCreateFramebuffer(device_, &shadowFramebufferInfo, nullptr,
                                  &frame.shadowFramebuffer),
              "vkCreateFramebuffer(shadow)");
        frame.colorFramebuffers.reserve(swapchain_.views.size());
        for (VkImageView colorView : swapchain_.views) {
            const std::array<VkImageView, 2> attachments{colorView, frame.depth.view};
            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = colorClearRenderPass_;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = swapchain_.extent.width;
            framebufferInfo.height = swapchain_.extent.height;
            framebufferInfo.layers = 1;
            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            Check(vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &framebuffer),
                  "vkCreateFramebuffer(color)");
            frame.colorFramebuffers.push_back(framebuffer);
        }
    }

    const uint32_t totalReductionSets = hierarchyMips * kFramesInFlight;
    const std::array<VkDescriptorPoolSize, 4> poolSizes{{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kFramesInFlight * 4},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         kFramesInFlight * 2 + totalReductionSets},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, totalReductionSets}}};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kFramesInFlight * 3 + totalReductionSets;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    Check(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_),
          "vkCreateDescriptorPool");

    for (FrameSlot& frame : frames_) {
        std::array<VkDescriptorSetLayout, 3> layouts{frameSetLayout_, dataSetLayout_, depthSetLayout_};
        std::array<VkDescriptorSet, 3> sets{};
        VkDescriptorSetAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocation.descriptorPool = descriptorPool_;
        allocation.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        allocation.pSetLayouts = layouts.data();
        Check(vkAllocateDescriptorSets(device_, &allocation, sets.data()),
              "vkAllocateDescriptorSets(frame)");
        frame.frameSet = sets[0];
        frame.dataSet = sets[1];
        frame.depthSet = sets[2];

        VkDescriptorBufferInfo uniformInfo{frame.uniform.handle, 0, sizeof(FrameUniform)};
        std::array<VkDescriptorBufferInfo, 4> dataInfos{{
            {persistentBlades_.handle, 0, VK_WHOLE_SIZE},
            {frame.visibleBlades.handle, 0, VK_WHOLE_SIZE},
            {frame.indirect.handle, 0, sizeof(DrawIndirect)},
            {frame.stats.handle, 0, sizeof(VisibilityStats)}}};
        VkDescriptorImageInfo shadowInfo{shadowSampler_, frame.shadow.view,
                                         VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo hierarchyInfo{depthSampler_, frame.hierarchy.view, VK_IMAGE_LAYOUT_GENERAL};
        std::array<VkWriteDescriptorSet, 7> writes{};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, frame.frameSet, 0, 0, 1,
                     VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uniformInfo, nullptr};
        writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, frame.frameSet, 1, 0, 1,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &shadowInfo, nullptr, nullptr};
        for (uint32_t binding = 0; binding < dataInfos.size(); ++binding) {
            writes[2 + binding] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, frame.dataSet,
                                   binding, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr,
                                   &dataInfos[binding], nullptr};
        }
        writes[6] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, frame.depthSet, 0, 0, 1,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &hierarchyInfo, nullptr, nullptr};
        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        std::vector<VkDescriptorSetLayout> reductionLayouts(hierarchyMips, reductionSetLayout_);
        frame.reductionSets.resize(hierarchyMips);
        VkDescriptorSetAllocateInfo reductionAllocation{};
        reductionAllocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        reductionAllocation.descriptorPool = descriptorPool_;
        reductionAllocation.descriptorSetCount = hierarchyMips;
        reductionAllocation.pSetLayouts = reductionLayouts.data();
        Check(vkAllocateDescriptorSets(device_, &reductionAllocation, frame.reductionSets.data()),
              "vkAllocateDescriptorSets(reduction)");
        for (uint32_t mip = 0; mip < hierarchyMips; ++mip) {
            VkDescriptorImageInfo sourceInfo{};
            sourceInfo.sampler = depthSampler_;
            sourceInfo.imageView = mip == 0 ? frame.depth.view : frame.hierarchy.mipViews[mip - 1];
            sourceInfo.imageLayout = mip == 0 ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                              : VK_IMAGE_LAYOUT_GENERAL;
            VkDescriptorImageInfo destinationInfo{};
            destinationInfo.imageView = frame.hierarchy.mipViews[mip];
            destinationInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            std::array<VkWriteDescriptorSet, 2> reductionWrites{};
            reductionWrites[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                                  frame.reductionSets[mip], 0, 0, 1,
                                  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &sourceInfo, nullptr,
                                  nullptr};
            reductionWrites[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                                  frame.reductionSets[mip], 1, 0, 1,
                                  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &destinationInfo, nullptr,
                                  nullptr};
            vkUpdateDescriptorSets(device_, static_cast<uint32_t>(reductionWrites.size()),
                                   reductionWrites.data(), 0, nullptr);
        }
    }
}

void Renderer::Impl::DestroyFrameResources() {
    if (!device_) return;
    if (descriptorPool_) {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
    std::vector<VkCommandBuffer> commands;
    for (FrameSlot& frame : frames_) {
        for (VkFramebuffer framebuffer : frame.colorFramebuffers) {
            if (framebuffer) vkDestroyFramebuffer(device_, framebuffer, nullptr);
        }
        if (frame.shadowFramebuffer) vkDestroyFramebuffer(device_, frame.shadowFramebuffer, nullptr);
        if (frame.depthFramebuffer) vkDestroyFramebuffer(device_, frame.depthFramebuffer, nullptr);
        if (frame.queryPool) vkDestroyQueryPool(device_, frame.queryPool, nullptr);
        if (frame.imageAcquired) vkDestroySemaphore(device_, frame.imageAcquired, nullptr);
        if (frame.fence) vkDestroyFence(device_, frame.fence, nullptr);
        DestroyBuffer(frame.capture);
        DestroyBuffer(frame.bladeReadback);
        DestroyBuffer(frame.readback);
        DestroyBuffer(frame.stats);
        DestroyBuffer(frame.indirect);
        DestroyBuffer(frame.visibleBlades);
        DestroyBuffer(frame.overlay);
        DestroyBuffer(frame.uniform);
        DestroyImage(frame.hierarchy);
        DestroyImage(frame.shadow);
        DestroyImage(frame.depth);
        if (frame.commandBuffer) commands.push_back(frame.commandBuffer);
        frame = {};
    }
    if (!commands.empty() && commandPool_) {
        vkFreeCommandBuffers(device_, commandPool_, static_cast<uint32_t>(commands.size()),
                             commands.data());
    }
}

bool Renderer::Impl::EffectiveOcclusion(const FrameUniform& uniform) const {
    return (uniform.flags.y & CullOcclusion) != 0u;
}

bool Renderer::Impl::EffectiveShadows(const FrameUniform& uniform) const {
    return (uniform.flags.w & BetterShading) != 0u &&
           (uniform.renderControl.x & kRenderControlShadows) != 0u;
}

glm::vec4 Renderer::Impl::DrawTranslation(const SceneDraw& draw,
                                           const FrameUniform& uniform) const {
    const glm::vec3 translation = draw.movable ? glm::vec3(uniform.collider) : glm::vec3(0.0f);
    return glm::vec4(translation, static_cast<float>(draw.material));
}

void Renderer::Impl::BuildOverlay(FrameSlot& slot, const FrameUniform& uniform) {
    slot.overlayVertexCount = 0;
    if (automated_ || (uniform.flags.w & ShowOverlay) == 0u) {
        return;
    }

    std::ostringstream textStream;
    textStream << "VULKAN GRASS  [H] overlay\n"
               << "visible " << latestStats_.visibleCount << " / " << uniform.flags.x
               << "   GPU " << std::fixed << std::setprecision(2) << latestTimings_.gpuMs
               << " ms   CPU " << latestTimings_.cpuMs << " ms\n"
               << "wind " << std::setprecision(1) << uniform.wind.x << ' ' << uniform.wind.y
               << ' ' << uniform.wind.z << "   cull 0x" << std::hex << uniform.flags.y << std::dec
               << "   LOD " << (uniform.flags.z != 0u ? "on" : "off")
               << "   OCC " << (EffectiveOcclusion(uniform) ? "on" : "off") << "\n"
               << "SHADE " << ((uniform.flags.w & BetterShading) != 0u ? "improved" : "basic")
               << "   SHADOW " << (EffectiveShadows(uniform) ? "on" : "off") << "\n"
               << "1-4 cull   L LOD   B improved-shading   F wire   SPACE pause   R reset\n"
               << "LMB orbit   RMB zoom   WASD/QE sphere   arrows wind";
    std::string text = textStream.str();

    std::vector<EasyFontVertex> easyVertices(kEasyFontScratchBytes /
                                              sizeof(EasyFontVertex));
    unsigned char fontColor[4]{230u, 242u, 218u, 255u};
    stb_easy_font_spacing(-0.25f);
    const int quadCount = stb_easy_font_print(
        0.0f, 0.0f, text.data(), fontColor, easyVertices.data(),
        static_cast<int>(easyVertices.size() * sizeof(EasyFontVertex)));

    constexpr float scale = 1.25f;
    constexpr float textX = 20.0f;
    constexpr float textY = 18.0f;
    constexpr float padding = 10.0f;
    const float textWidth = static_cast<float>(stb_easy_font_width(text.data())) * scale;
    const float textHeight = static_cast<float>(stb_easy_font_height(text.data())) * scale;
    const float left = textX - padding;
    const float top = textY - padding;
    const float right = textX + textWidth + padding;
    const float bottom = textY + textHeight + padding;

    std::vector<OverlayVertex> vertices;
    vertices.reserve(6u + static_cast<size_t>(std::max(quadCount, 0)) * 6u);
    const glm::vec4 backingColor(0.018f, 0.030f, 0.026f, 0.76f);
    vertices.push_back({{left, top}, backingColor});
    vertices.push_back({{right, top}, backingColor});
    vertices.push_back({{right, bottom}, backingColor});
    vertices.push_back({{left, top}, backingColor});
    vertices.push_back({{right, bottom}, backingColor});
    vertices.push_back({{left, bottom}, backingColor});

    const auto convert = [&](const EasyFontVertex& source) {
        OverlayVertex vertex;
        vertex.position = glm::vec2(textX + source.x * scale,
                                    textY + source.y * scale);
        vertex.color = glm::vec4(static_cast<float>(source.color[0]),
                                 static_cast<float>(source.color[1]),
                                 static_cast<float>(source.color[2]),
                                 static_cast<float>(source.color[3])) / 255.0f;
        return vertex;
    };
    for (int quad = 0; quad < quadCount; ++quad) {
        const EasyFontVertex* source = easyVertices.data() + quad * 4;
        vertices.push_back(convert(source[0]));
        vertices.push_back(convert(source[1]));
        vertices.push_back(convert(source[2]));
        vertices.push_back(convert(source[0]));
        vertices.push_back(convert(source[2]));
        vertices.push_back(convert(source[3]));
    }

    const VkDeviceSize bytes = static_cast<VkDeviceSize>(vertices.size()) * sizeof(OverlayVertex);
    if (bytes > slot.overlay.size) {
        throw std::runtime_error("Overlay vertex data exceeded its per-frame buffer");
    }
    if (bytes > 0) {
        std::memcpy(slot.overlay.mapped, vertices.data(), static_cast<size_t>(bytes));
    }
    slot.overlayVertexCount = static_cast<uint32_t>(vertices.size());
}

void Renderer::Impl::RecordFrame(FrameSlot& slot, uint32_t imageIndex,
                                 const FrameUniform& uniform, bool captureFrame,
                                 bool verifyFrame) {
    Check(vkResetCommandBuffer(slot.commandBuffer, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(vkBeginCommandBuffer(slot.commandBuffer, &begin), "vkBeginCommandBuffer");
    VkCommandBuffer command = slot.commandBuffer;
    vkCmdResetQueryPool(command, slot.queryPool, 0, kQueryCount);
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, slot.queryPool,
                        kQueryTotalBegin);

    VkBufferMemoryBarrier persistentDependency{};
    persistentDependency.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    persistentDependency.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    persistentDependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    persistentDependency.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    persistentDependency.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    persistentDependency.buffer = persistentBlades_.handle;
    persistentDependency.offset = 0;
    persistentDependency.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(command,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
                         &persistentDependency, 0, nullptr);

    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, slot.queryPool,
                        kQuerySimulationBegin);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, simulationPipeline_);
    const std::array<VkDescriptorSet, 2> simulationSets{slot.frameSet, slot.dataSet};
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, simulationLayout_, 0,
                            static_cast<uint32_t>(simulationSets.size()), simulationSets.data(),
                            0, nullptr);
    if (options_.bladeCount > 0) {
        vkCmdDispatch(command, DivideRoundUp(options_.bladeCount, kComputeGroupSize), 1, 1);
    }
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, slot.queryPool,
                        kQuerySimulationEnd);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
                         0, nullptr);

    const bool useShadows = EffectiveShadows(uniform);
    if (useShadows) {
        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, slot.queryPool,
                            kQueryShadowBegin);
        VkClearValue shadowClear{};
        shadowClear.depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo shadowBegin{};
        shadowBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        shadowBegin.renderPass = shadowRenderPass_;
        shadowBegin.framebuffer = slot.shadowFramebuffer;
        shadowBegin.renderArea.extent = {kShadowExtent, kShadowExtent};
        shadowBegin.clearValueCount = 1;
        shadowBegin.pClearValues = &shadowClear;
        vkCmdBeginRenderPass(command, &shadowBegin, VK_SUBPASS_CONTENTS_INLINE);
        const VkViewport shadowViewport{0.0f, 0.0f, static_cast<float>(kShadowExtent),
                                        static_cast<float>(kShadowExtent), 0.0f, 1.0f};
        const VkRect2D shadowScissor{{0, 0}, {kShadowExtent, kShadowExtent}};
        vkCmdSetViewport(command, 0, 1, &shadowViewport);
        vkCmdSetScissor(command, 0, 1, &shadowScissor);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, opaqueLayout_, 0, 1,
                                &slot.frameSet, 0, nullptr);
        if (sceneVertices_.handle && sceneIndices_.handle) {
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(command, 0, 1, &sceneVertices_.handle, &offset);
            vkCmdBindIndexBuffer(command, sceneIndices_.handle, 0, VK_INDEX_TYPE_UINT32);
            for (const SceneDraw& draw : scene_->Draws()) {
                if (draw.material == 0 || draw.indexCount == 0) {
                    continue;
                }
                const DrawPushConstant push{DrawTranslation(draw, uniform)};
                vkCmdPushConstants(command, opaqueLayout_,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(push), &push);
                vkCmdDrawIndexed(command, draw.indexCount, 1, draw.firstIndex, 0, 0);
            }
        }
        vkCmdEndRenderPass(command);
        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, slot.queryPool,
                            kQueryShadowEnd);
    } else {
        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, slot.queryPool,
                            kQueryShadowBegin);
        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, slot.queryPool,
                            kQueryShadowEnd);
    }

    const bool useOcclusion = EffectiveOcclusion(uniform);
    if (useOcclusion) {
        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, slot.queryPool,
                            kQueryDepthBegin);
        VkClearValue depthClear{};
        depthClear.depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo pass{};
        pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        pass.renderPass = depthRenderPass_;
        pass.framebuffer = slot.depthFramebuffer;
        pass.renderArea.extent = swapchain_.extent;
        pass.clearValueCount = 1;
        pass.pClearValues = &depthClear;
        vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
        const VkViewport viewport{0.0f, 0.0f, static_cast<float>(swapchain_.extent.width),
                                  static_cast<float>(swapchain_.extent.height), 0.0f, 1.0f};
        const VkRect2D scissor{{0, 0}, swapchain_.extent};
        vkCmdSetViewport(command, 0, 1, &viewport);
        vkCmdSetScissor(command, 0, 1, &scissor);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, depthPipeline_);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, opaqueLayout_, 0, 1,
                                &slot.frameSet, 0, nullptr);
        if (sceneVertices_.handle && sceneIndices_.handle) {
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(command, 0, 1, &sceneVertices_.handle, &offset);
            vkCmdBindIndexBuffer(command, sceneIndices_.handle, 0, VK_INDEX_TYPE_UINT32);
            for (const SceneDraw& draw : scene_->Draws()) {
                if (draw.material == 0 || draw.indexCount == 0) {
                    continue;
                }
                const DrawPushConstant push{DrawTranslation(draw, uniform)};
                vkCmdPushConstants(command, opaqueLayout_,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(push), &push);
                vkCmdDrawIndexed(command, draw.indexCount, 1, draw.firstIndex, 0, 0);
            }
        }
        vkCmdEndRenderPass(command);

        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, reductionPipeline_);
        for (uint32_t mip = 0; mip < slot.hierarchy.mipLevels; ++mip) {
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, reductionLayout_,
                                    0, 1, &slot.reductionSets[mip], 0, nullptr);
            const uint32_t width = std::max(1u, swapchain_.extent.width >> mip);
            const uint32_t height = std::max(1u, swapchain_.extent.height >> mip);
            vkCmdDispatch(command, DivideRoundUp(width, kReduceGroupSize),
                          DivideRoundUp(height, kReduceGroupSize), 1);
            VkImageMemoryBarrier mipReady{};
            mipReady.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            mipReady.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            mipReady.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            mipReady.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            mipReady.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            mipReady.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            mipReady.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            mipReady.image = slot.hierarchy.handle;
            mipReady.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1};
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                                 nullptr, 1, &mipReady);
        }

        VkImageMemoryBarrier depthForColor{};
        depthForColor.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        depthForColor.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        depthForColor.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthForColor.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        depthForColor.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthForColor.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthForColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthForColor.image = slot.depth.handle;
        depthForColor.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &depthForColor);
        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, slot.queryPool,
                            kQueryDepthEnd);
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT |
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 0, nullptr);
    } else {
        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, slot.queryPool,
                            kQueryDepthBegin);
        vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, slot.queryPool,
                            kQueryDepthEnd);
    }

    vkCmdFillBuffer(command, slot.indirect.handle, offsetof(DrawIndirect, vertexCount),
                    sizeof(uint32_t), 0);
    vkCmdFillBuffer(command, slot.stats.handle, 0, sizeof(VisibilityStats), 0);
    std::array<VkBufferMemoryBarrier, 3> cullInputs{};
    cullInputs[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    cullInputs[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    cullInputs[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    cullInputs[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    cullInputs[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    cullInputs[0].buffer = slot.indirect.handle;
    cullInputs[0].size = VK_WHOLE_SIZE;
    cullInputs[1] = cullInputs[0];
    cullInputs[1].buffer = slot.stats.handle;
    cullInputs[2] = persistentDependency;
    cullInputs[2].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    cullInputs[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command,
                         VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                         static_cast<uint32_t>(cullInputs.size()), cullInputs.data(), 0, nullptr);

    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, slot.queryPool,
                        kQueryCullBegin);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, visibilityPipeline_);
    const std::array<VkDescriptorSet, 3> visibilitySets{slot.frameSet, slot.dataSet, slot.depthSet};
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, visibilityLayout_, 0,
                            static_cast<uint32_t>(visibilitySets.size()), visibilitySets.data(),
                            0, nullptr);
    if (options_.bladeCount > 0) {
        vkCmdDispatch(command, DivideRoundUp(options_.bladeCount, kComputeGroupSize), 1, 1);
    }
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, slot.queryPool,
                        kQueryCullEnd);

    std::array<VkBufferMemoryBarrier, 3> renderInputs{};
    for (auto& barrier : renderInputs) {
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
    }
    renderInputs[0].dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    renderInputs[0].buffer = slot.visibleBlades.handle;
    const VkAccessFlags readbackAccess = readbackEnabled_
        ? static_cast<VkAccessFlags>(VK_ACCESS_TRANSFER_READ_BIT)
        : VkAccessFlags{0};
    const VkPipelineStageFlags readbackStage = readbackEnabled_
        ? static_cast<VkPipelineStageFlags>(VK_PIPELINE_STAGE_TRANSFER_BIT)
        : VkPipelineStageFlags{0};
    renderInputs[1].srcAccessMask |= VK_ACCESS_TRANSFER_WRITE_BIT;
    renderInputs[1].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | readbackAccess;
    renderInputs[1].buffer = slot.indirect.handle;
    renderInputs[2].srcAccessMask |= VK_ACCESS_TRANSFER_WRITE_BIT;
    renderInputs[2].dstAccessMask = readbackAccess;
    renderInputs[2].buffer = slot.stats.handle;
    vkCmdPipelineBarrier(command,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                             readbackStage,
                         0, 0, nullptr, static_cast<uint32_t>(renderInputs.size()),
                         renderInputs.data(), 0, nullptr);

    const VkRenderPass colorPass = useOcclusion ? colorLoadRenderPass_ : colorClearRenderPass_;
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.02f, 0.04f, 0.07f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo colorBegin{};
    colorBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    colorBegin.renderPass = colorPass;
    colorBegin.framebuffer = slot.colorFramebuffers[imageIndex];
    colorBegin.renderArea.extent = swapchain_.extent;
    colorBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
    colorBegin.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(command, &colorBegin, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(swapchain_.extent.width),
                              static_cast<float>(swapchain_.extent.height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, swapchain_.extent};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);

    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline_);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, skyLayout_, 0, 1,
                            &slot.frameSet, 0, nullptr);
    vkCmdDraw(command, 3, 1, 0, 0);

    if (sceneVertices_.handle && sceneIndices_.handle) {
        const VkDeviceSize offset = 0;
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, opaquePipeline_);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, opaqueLayout_, 0, 1,
                                &slot.frameSet, 0, nullptr);
        vkCmdBindVertexBuffers(command, 0, 1, &sceneVertices_.handle, &offset);
        vkCmdBindIndexBuffer(command, sceneIndices_.handle, 0, VK_INDEX_TYPE_UINT32);
        for (const SceneDraw& draw : scene_->Draws()) {
            if (draw.indexCount == 0) continue;
            const DrawPushConstant push{DrawTranslation(draw, uniform)};
            vkCmdPushConstants(command, opaqueLayout_,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(push), &push);
            vkCmdDrawIndexed(command, draw.indexCount, 1, draw.firstIndex, 0, 0);
        }
    }

    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, slot.queryPool,
                        kQueryGraphicsTailBegin);
    const bool wireframe = (uniform.flags.w & Wireframe) != 0u && grassWirePipeline_;
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      wireframe ? grassWirePipeline_ : grassPipeline_);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, grassLayout_, 0, 1,
                            &slot.frameSet, 0, nullptr);
    const VkDeviceSize bladeOffset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &slot.visibleBlades.handle, &bladeOffset);
    vkCmdDrawIndirect(command, slot.indirect.handle, 0, 1, sizeof(DrawIndirect));
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, slot.queryPool,
                        kQueryGraphicsTailEnd);
    if (slot.overlayVertexCount > 0) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, overlayPipeline_);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, overlayLayout_, 0, 1,
                                &slot.frameSet, 0, nullptr);
        const VkDeviceSize overlayOffset = 0;
        vkCmdBindVertexBuffers(command, 0, 1, &slot.overlay.handle, &overlayOffset);
        vkCmdDraw(command, slot.overlayVertexCount, 1, 0, 0);
    }
    vkCmdEndRenderPass(command);
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, slot.queryPool,
                        kQueryTotalEnd);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                         0, nullptr);

    if (readbackEnabled_) {
        VkBufferCopy statsCopy{0, 0, sizeof(VisibilityStats)};
        vkCmdCopyBuffer(command, slot.stats.handle, slot.readback.handle, 1, &statsCopy);
        VkBufferCopy indirectCopy{0, AlignUp(sizeof(VisibilityStats), 16), sizeof(DrawIndirect)};
        vkCmdCopyBuffer(command, slot.indirect.handle, slot.readback.handle, 1, &indirectCopy);
    }
    if (verifyFrame) {
        VkBufferMemoryBarrier verifyBarrier{};
        verifyBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        verifyBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        verifyBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        verifyBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        verifyBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        verifyBarrier.buffer = persistentBlades_.handle;
        verifyBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1,
                             &verifyBarrier, 0, nullptr);
        if (options_.bladeCount > 0) {
            const VkBufferCopy bladeCopy{0, 0,
                static_cast<VkDeviceSize>(options_.bladeCount) * sizeof(Blade)};
            vkCmdCopyBuffer(command, persistentBlades_.handle, slot.bladeReadback.handle, 1,
                            &bladeCopy);
        }
    }

    if (captureEnabled_) {
        if (captureFrame) {
            VkBufferImageCopy copy{};
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent = {swapchain_.extent.width, swapchain_.extent.height, 1};
            vkCmdCopyImageToBuffer(command, swapchain_.images[imageIndex],
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, slot.capture.handle,
                                   1, &copy);
        }
        VkImageMemoryBarrier presentBarrier{};
        presentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        presentBarrier.srcAccessMask = captureFrame
            ? static_cast<VkAccessFlags>(VK_ACCESS_TRANSFER_READ_BIT)
            : VkAccessFlags{0};
        presentBarrier.dstAccessMask = 0;
        presentBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        presentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        presentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        presentBarrier.image = swapchain_.images[imageIndex];
        presentBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &presentBarrier);
    }
    Check(vkEndCommandBuffer(command), "vkEndCommandBuffer");
}

double Renderer::Impl::TimestampMilliseconds(uint64_t begin, uint64_t end) const {
    uint64_t delta = 0;
    if (timestampValidBits_ >= 64) {
        delta = end - begin;
    } else {
        const uint64_t mask = (uint64_t{1} << timestampValidBits_) - 1u;
        delta = (end - begin) & mask;
    }
    return static_cast<double>(delta) * static_cast<double>(deviceProperties_.limits.timestampPeriod) /
           1.0e6;
}

void Renderer::Impl::ProcessCompletedFrame(FrameSlot& slot) {
    if (!slot.pending) {
        return;
    }
    std::array<uint64_t, kQueryCount> timestamps{};
    Check(vkGetQueryPoolResults(device_, slot.queryPool, 0, kQueryCount,
                                sizeof(timestamps), timestamps.data(), sizeof(uint64_t),
                                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
          "vkGetQueryPoolResults");

    VisibilityStats stats{};
    DrawIndirect indirect{};
    if (readbackEnabled_) {
        std::memcpy(&stats, slot.readback.mapped, sizeof(stats));
        std::memcpy(&indirect,
                    static_cast<const std::byte*>(slot.readback.mapped) +
                        AlignUp(sizeof(VisibilityStats), 16),
                    sizeof(indirect));
    }
    FrameTimings timings;
    timings.simulationMs = TimestampMilliseconds(timestamps[kQuerySimulationBegin],
                                                  timestamps[kQuerySimulationEnd]);
    timings.cullMs = TimestampMilliseconds(timestamps[kQueryCullBegin],
                                            timestamps[kQueryCullEnd]);
    timings.depthMs = EffectiveOcclusion(slot.uniformSnapshot)
        ? TimestampMilliseconds(timestamps[kQueryDepthBegin], timestamps[kQueryDepthEnd])
        : 0.0;
    timings.shadowMs = EffectiveShadows(slot.uniformSnapshot)
        ? TimestampMilliseconds(timestamps[kQueryShadowBegin], timestamps[kQueryShadowEnd])
        : 0.0;
    timings.graphicsTailMs = TimestampMilliseconds(timestamps[kQueryGraphicsTailBegin],
                                                    timestamps[kQueryGraphicsTailEnd]);
    timings.gpuMs = TimestampMilliseconds(timestamps[kQueryTotalBegin],
                                           timestamps[kQueryTotalEnd]);
    timings.cpuMs = slot.cpuMilliseconds;
    latestStats_ = stats;
    latestTimings_ = timings;
    latestUniformSnapshot_ = slot.uniformSnapshot;
    hasCompletedFrame_ = true;

    if (slot.verifyRequested) {
        VerifyFrame(slot, stats, indirect);
        ++verificationFramesProcessed_;
    }
    if (slot.captureRequested) {
        WriteCapture(slot);
        ++captureFramesWritten_;
    }
    if (benchmarkEnabled_ && slot.measured) {
        benchmarkFile_ << slot.outputFrame << ',' << slot.uniformSnapshot.flags.x << ','
                       << stats.visibleCount << ',' << std::fixed << std::setprecision(4)
                       << timings.simulationMs << ',' << timings.cullMs << ',' << timings.depthMs
                       << ',' << timings.graphicsTailMs << ',' << timings.gpuMs << ','
                       << timings.cpuMs
                       << ',' << stats.orientationRejected << ',' << stats.frustumRejected << ','
                       << stats.distanceRejected << ',' << stats.occlusionRejected << ','
                       << timings.shadowMs << '\n';
        if (!benchmarkFile_) {
            throw std::runtime_error("Failed while writing benchmark CSV");
        }
        ++benchmarkRowsWritten_;
    }
    slot.pending = false;
    slot.measured = false;
    slot.captureRequested = false;
    slot.verifyRequested = false;
}

void Renderer::Impl::ProcessAllCompletedFramesInOrder() {
    std::vector<FrameSlot*> pending;
    pending.reserve(frames_.size());
    for (FrameSlot& frame : frames_) {
        if (frame.pending) pending.push_back(&frame);
    }
    std::sort(pending.begin(), pending.end(), [](const FrameSlot* left, const FrameSlot* right) {
        return left->submissionIndex < right->submissionIndex;
    });
    for (FrameSlot* frame : pending) {
        ProcessCompletedFrame(*frame);
    }
}

void Renderer::Impl::VerifyFrame(const FrameSlot& slot, const VisibilityStats& stats,
                                 const DrawIndirect& indirect) {
    auto fail = [this, &slot](const std::string& reason) {
        if (verificationFailures_.size() < 64) {
            std::ostringstream message;
            message << "frame " << slot.outputFrame << ": " << reason;
            verificationFailures_.push_back(message.str());
        }
    };
    if (stats.inputCount != options_.bladeCount) {
        fail("visibility inputCount does not equal blade count");
    }
    if (stats.visibleCount > stats.inputCount || indirect.vertexCount > options_.bladeCount) {
        fail("visible/indirect count exceeds input count");
    }
    if (stats.visibleCount != indirect.vertexCount) {
        fail("VisibilityStats.visibleCount differs from indirect vertexCount");
    }
    if (stats.orientationRejected > stats.inputCount ||
        stats.frustumRejected > stats.inputCount ||
        stats.distanceRejected > stats.inputCount ||
        stats.occlusionRejected > stats.inputCount) {
        fail("an independent rejection counter exceeds the input count");
    }
    if (indirect.instanceCount != 1 || indirect.firstVertex != 0 || indirect.firstInstance != 0) {
        fail("indirect draw invariant {instanceCount=1, firstVertex=0, firstInstance=0} failed");
    }
    const auto* current = static_cast<const Blade*>(slot.bladeReadback.mapped);
    const std::vector<Blade>& original = scene_->Blades();
    if (staticObstacleDistances_.size() != original.size()) {
        throw std::runtime_error("Verification clearance cache does not match the Scene");
    }
    if (physicsProbeFile_.is_open()) {
        for (uint32_t index : physicsProbeIndices_) {
            const Blade& blade = current[index];
            const glm::vec3 root(blade.v0);
            const glm::vec3 tip(blade.v2);
            physicsProbeFile_ << slot.outputFrame << ',' << std::setprecision(9)
                              << slot.uniformSnapshot.cameraTime.w << ',' << index << ','
                              << blade.v1.w << ',' << root.x << ',' << root.z << ','
                              << tip.x << ',' << tip.y << ',' << tip.z << ','
                              << slot.uniformSnapshot.collider.x << ','
                              << slot.uniformSnapshot.collider.y << ','
                              << slot.uniformSnapshot.collider.z << '\n';
        }
        if (!physicsProbeFile_) {
            throw std::runtime_error("Failed while writing physics probe CSV");
        }
    }
    auto finiteVec = [](const glm::vec4& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
               std::isfinite(value.z) && std::isfinite(value.w);
    };
    constexpr float geometryTolerance = 2.0e-4f;
    for (uint32_t index = 0; index < options_.bladeCount; ++index) {
        const Blade& blade = current[index];
        if (!finiteVec(blade.v0) || !finiteVec(blade.v1) || !finiteVec(blade.v2) ||
            !finiteVec(blade.up)) {
            fail("non-finite blade state at index " + std::to_string(index));
            break;
        }
        const glm::vec3 root(blade.v0);
        const glm::vec3 originalRoot(original[index].v0);
        if (glm::length(root - originalRoot) > 2.0e-3f || root.y < -2.0e-3f) {
            fail("blade root left its ground anchor at index " + std::to_string(index));
            break;
        }
        const float restLength = std::max(original[index].v1.w, 1.0e-3f);
        const glm::vec3 guide(blade.v1);
        const glm::vec3 tip(blade.v2);
        const glm::vec2 guideOffset(guide.x - root.x, guide.z - root.z);
        if (glm::length(guideOffset) > geometryTolerance) {
            fail("blade guide left root XZ at index " + std::to_string(index));
            break;
        }
        if (glm::length(tip - root) > restLength * 1.01f + geometryTolerance) {
            fail("blade tip reach exceeds rest-height bound at index " +
                 std::to_string(index));
            break;
        }
        const float requiredStaticClearance = restLength * 1.01f +
            std::abs(original[index].v2.w) * 0.5f + 0.01f;
        const float staticClearance = staticObstacleDistances_[index];
        if (!(staticClearance + geometryTolerance >= requiredStaticClearance)) {
            fail("static obstacle clearance is below the blade reach bound at index " +
                 std::to_string(index));
            break;
        }
        const float approximateLength = (2.0f * glm::length(tip - root) +
            glm::length(guide - root) + glm::length(tip - guide)) / 3.0f;
        if (!std::isfinite(approximateLength) ||
            std::abs(approximateLength - restLength) > restLength * 0.01f + 1.0e-4f) {
            fail("blade approximate length is outside tolerance at index " +
                 std::to_string(index));
            break;
        }
        const float horizontalLimit = options_.fieldSize * 0.5f + restLength * 4.0f +
                                      slot.uniformSnapshot.collider.w + 2.0f;
        const float verticalLimit = restLength * 5.0f + slot.uniformSnapshot.collider.w + 2.0f;
        if (std::abs(tip.x) > horizontalLimit || std::abs(tip.z) > horizontalLimit ||
            tip.y < -restLength || tip.y > verticalLimit || blade.v2.w <= 0.0f ||
            glm::dot(glm::vec3(blade.up), tip - root) < -2.0e-3f) {
            fail("blade state escaped conservative bounds at index " + std::to_string(index));
            break;
        }
    }
}

void Renderer::Impl::WriteCapture(const FrameSlot& slot) {
    const size_t pixelCount = static_cast<size_t>(slot.captureWidth) * slot.captureHeight;
    const auto* source = static_cast<const uint8_t*>(slot.capture.mapped);
    std::vector<uint8_t> rgba(pixelCount * 4);
    const bool bgra = slot.captureFormat == VK_FORMAT_B8G8R8A8_SRGB ||
                      slot.captureFormat == VK_FORMAT_B8G8R8A8_UNORM;
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        rgba[pixel * 4 + 0] = source[pixel * 4 + (bgra ? 2 : 0)];
        rgba[pixel * 4 + 1] = source[pixel * 4 + 1];
        rgba[pixel * 4 + 2] = source[pixel * 4 + (bgra ? 0 : 2)];
        rgba[pixel * 4 + 3] = source[pixel * 4 + 3];
    }
    std::ostringstream filename;
    filename << "frame_" << std::setw(6) << std::setfill('0') << slot.outputFrame << ".png";
    const std::filesystem::path path = std::filesystem::path(options_.captureDir) / filename.str();
    if (stbi_write_png(path.string().c_str(), static_cast<int>(slot.captureWidth),
                       static_cast<int>(slot.captureHeight), 4, rgba.data(),
                       static_cast<int>(slot.captureWidth * 4)) == 0) {
        throw std::runtime_error("Failed to write capture: " + path.string());
    }
}

void Renderer::Impl::InitializeOutputs() {
    if (captureEnabled_) {
        std::filesystem::create_directories(options_.captureDir);
    }
    if (!options_.physicsProbePath.empty()) {
        const std::filesystem::path path(options_.physicsProbePath);
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }
        physicsProbeFile_.open(path, std::ios::out | std::ios::trunc);
        if (!physicsProbeFile_) {
            throw std::runtime_error("Unable to open physics probe CSV: " + path.string());
        }
        physicsProbeFile_ << "frame,time,index,rest_height,root_x,root_z,tip_x,tip_y,tip_z,"
                             "collider_x,collider_y,collider_z\n";
        if (!physicsProbeFile_) {
            throw std::runtime_error("Failed while writing physics probe CSV header");
        }
    }
    if (benchmarkEnabled_) {
        const std::filesystem::path path(options_.benchmarkPath);
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }
        benchmarkFile_.open(path, std::ios::out | std::ios::trunc);
        if (!benchmarkFile_) {
            throw std::runtime_error("Unable to open benchmark CSV: " + path.string());
        }
        benchmarkFile_ << "frame,blade_count,visible,sim_ms,cull_ms,depth_ms,graphics_tail_ms,"
                          "gpu_ms,cpu_ms,orientation_rejected,frustum_rejected,"
                          "distance_rejected,occlusion_rejected,shadow_ms\n";
    }
}

void Renderer::Impl::WriteMetadata() {
    if (!benchmarkEnabled_) return;
    const std::filesystem::path path = options_.benchmarkPath + ".meta.json";
    std::ofstream metadata(path, std::ios::out | std::ios::trunc);
    if (!metadata) {
        std::cerr << "Warning: unable to write benchmark metadata: " << path << '\n';
        return;
    }
    auto json = [](std::string value) {
        std::string escaped;
        for (char character : value) {
            if (character == '\\' || character == '\"') escaped += '\\';
            escaped += character;
        }
        return escaped;
    };
    const uint32_t width = hasCompletedFrame_
        ? static_cast<uint32_t>(std::max(latestUniformSnapshot_.viewport.x, 0.0f))
        : swapchain_.extent.width;
    const uint32_t height = hasCompletedFrame_
        ? static_cast<uint32_t>(std::max(latestUniformSnapshot_.viewport.y, 0.0f))
        : swapchain_.extent.height;
    uint32_t configuredCullMask = options_.culling;
    if (options_.occlusion) {
        configuredCullMask |= static_cast<uint32_t>(CullOcclusion);
    }
    const uint32_t actualCullMask = hasCompletedFrame_
        ? latestUniformSnapshot_.flags.y
        : configuredCullMask;
    const bool actualLod = hasCompletedFrame_ ? latestUniformSnapshot_.flags.z != 0u : options_.lod;
    const bool actualOcclusion = hasCompletedFrame_
        ? EffectiveOcclusion(latestUniformSnapshot_)
        : options_.occlusion;
    const uint32_t actualFeatureFlags = hasCompletedFrame_
        ? latestUniformSnapshot_.flags.w
        : 0u;
    const bool actualBetterShading = hasCompletedFrame_
        ? (actualFeatureFlags & BetterShading) != 0u
        : false;
    const uint32_t actualRenderControl = hasCompletedFrame_
        ? latestUniformSnapshot_.renderControl.x
        : options_.renderFeatureMask;
    const bool actualShadows = hasCompletedFrame_
        ? EffectiveShadows(latestUniformSnapshot_)
        : false;
    const bool actualShadowRequested =
        (actualRenderControl & kRenderControlShadows) != 0u;
    std::string buildType = VULKAN_GRASS_BUILD_TYPE;
    if (buildType.empty()) {
#ifdef NDEBUG
        buildType = "Release";
#else
        buildType = "Debug";
#endif
    }
    metadata << "{\n"
             << "  \"gpu\": \"" << json(deviceProperties_.deviceName) << "\",\n"
             << "  \"cpu\": \"" << json(CpuDescription()) << "\",\n"
             << "  \"driver\": \"" << DriverVersionString(deviceProperties_) << "\",\n"
             << "  \"driver_version\": \"" << DriverVersionString(deviceProperties_) << "\",\n"
             << "  \"driver_version_raw\": " << deviceProperties_.driverVersion << ",\n"
             << "  \"vulkan_api\": \"" << VulkanVersion(deviceProperties_.apiVersion) << "\",\n"
             << "  \"os\": \"" << OperatingSystemName() << "\",\n"
             << "  \"build_timestamp\": \"" << __DATE__ << ' ' << __TIME__ << "\",\n"
             << "  \"build_type\": \"" << json(buildType) << "\",\n"
             << "  \"source_commit\": \"" << json(VULKAN_GRASS_SOURCE_COMMIT) << "\",\n"
             << "  \"validation\": " << (options_.validation ? "true" : "false") << ",\n"
             << "  \"resolution\": \"" << width << 'x' << height << "\",\n"
             << "  \"width\": " << width << ",\n"
             << "  \"height\": " << height << ",\n"
             << "  \"blade_count\": " << options_.bladeCount << ",\n"
             << "  \"seed\": " << options_.seed << ",\n"
             << "  \"preset\": \"" << json(options_.preset) << "\",\n"
             << "  \"camera\": \"" << json(options_.camera) << "\",\n"
             << "  \"cull_mask\": " << actualCullMask << ",\n"
             << "  \"lod\": " << (actualLod ? "true" : "false") << ",\n"
             << "  \"occlusion\": " << (actualOcclusion ? "true" : "false") << ",\n"
             << "  \"feature_flags\": " << actualFeatureFlags << ",\n"
             << "  \"better_shading\": " << (actualBetterShading ? "true" : "false") << ",\n"
             << "  \"configured_render_control\": " << options_.renderFeatureMask << ",\n"
             << "  \"render_control\": " << actualRenderControl << ",\n"
             << "  \"shadow_control_bit\": " << kRenderControlShadows << ",\n"
             << "  \"shadow_requested\": " << (actualShadowRequested ? "true" : "false") << ",\n"
             << "  \"shadow_pass_enabled\": " << (actualShadows ? "true" : "false") << ",\n"
             << "  \"shadow_map_resolution\": " << kShadowExtent << ",\n"
             << "  \"shadow_map_width\": " << kShadowExtent << ",\n"
             << "  \"shadow_map_height\": " << kShadowExtent << ",\n"
             << "  \"shadow_depth_format\": \"" << DepthFormatName(shadowFormat_) << "\",\n"
             << "  \"shadow_depth_format_value\": " << static_cast<int>(shadowFormat_) << ",\n"
             << "  \"fixed_delta\": " << options_.fixedDelta << ",\n"
             << "  \"warmup_frames\": " << options_.warmup << ",\n"
             << "  \"measured_frames\": " << options_.frames << ",\n"
             << "  \"shadow_ms_definition\": "
                "\"GPU timestamp interval covering the 2048x2048 shadow-map clear, opaque "
                "caster draws, and transition to fragment sampling; zero when shadows are "
                "disabled by either the BetterShading master or render-control bit 64\",\n"
             << "  \"gpu_ms_definition\": "
                "\"Top-to-bottom GPU timestamp interval from command-buffer start through "
                "color and overlay rendering, including simulation, shadow rendering, camera "
                "depth/Hi-Z, and visibility; excludes later diagnostic readback and capture "
                "transfers\",\n"
             << "  \"graphics_tail_ms_definition\": "
                "\"Interval from a vertex-stage timestamp immediately before the grass draw "
                "to a bottom-of-pipe timestamp after it; may include unfinished earlier opaque "
                "graphics work\",\n"
             << "  \"cpu_ms_definition\": "
                "\"Entire DrawFrame wall time including delayed completion processing, frame-fence "
                "wait, image acquisition, recording, submission, and presentation\"\n"
             << "}\n";
}

bool Renderer::Impl::DrawFrame(uint64_t, uint32_t automatedIndex, float delta,
                               float simulationTime) {
    const auto cpuBegin = std::chrono::steady_clock::now();
    FrameSlot& slot = frames_[frameCursor_ % kFramesInFlight];
    Check(vkWaitForFences(device_, 1, &slot.fence, VK_TRUE, UINT64_MAX),
          "vkWaitForFences(frame)");
    ProcessCompletedFrame(slot);

    uint32_t imageIndex = 0;
    VkResult acquire = vkAcquireNextImageKHR(device_, swapchain_.handle, UINT64_MAX,
                                             slot.imageAcquired, VK_NULL_HANDLE, &imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return false;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        Check(acquire, "vkAcquireNextImageKHR");
    }
    if (swapchain_.imageFences[imageIndex] != VK_NULL_HANDLE &&
        swapchain_.imageFences[imageIndex] != slot.fence) {
        Check(vkWaitForFences(device_, 1, &swapchain_.imageFences[imageIndex], VK_TRUE,
                              UINT64_MAX),
              "vkWaitForFences(swapchain image)");
    }
    swapchain_.imageFences[imageIndex] = slot.fence;

    const FrameUniform uniform = scene_->MakeUniform(swapchain_.extent.width,
                                                      swapchain_.extent.height, delta,
                                                      simulationTime);
    std::memcpy(slot.uniform.mapped, &uniform, sizeof(uniform));
    BuildOverlay(slot, uniform);
    const bool measured = automated_ && automatedIndex >= options_.warmup;
    const uint32_t outputFrame = measured ? automatedIndex - options_.warmup : 0;
    const bool captureFrame = captureEnabled_ && measured;
    const bool verifyFrame = options_.verify && measured;

    RecordFrame(slot, imageIndex, uniform, captureFrame, verifyFrame);
    Check(vkResetFences(device_, 1, &slot.fence), "vkResetFences");
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (benchmarkEnabled_) {
        waitStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &slot.imageAcquired;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &slot.commandBuffer;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &swapchain_.renderFinished[imageIndex];
    Check(vkQueueSubmit(queue_, 1, &submit, slot.fence), "vkQueueSubmit(frame)");

    slot.pending = true;
    slot.measured = measured;
    slot.captureRequested = captureFrame;
    slot.verifyRequested = verifyFrame;
    slot.outputFrame = outputFrame;
    slot.imageIndex = imageIndex;
    slot.captureWidth = swapchain_.extent.width;
    slot.captureHeight = swapchain_.extent.height;
    slot.captureFormat = swapchain_.format;
    slot.uniformSnapshot = uniform;
    slot.submissionIndex = frameCursor_;

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &swapchain_.renderFinished[imageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_.handle;
    present.pImageIndices = &imageIndex;
    const VkResult presentResult = vkQueuePresentKHR(queue_, &present);
    slot.cpuMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - cpuBegin).count();
    ++frameCursor_;

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR ||
        acquire == VK_SUBOPTIMAL_KHR || framebufferResized_) {
        framebufferResized_ = false;
        RecreateSwapchain();
    } else if (presentResult != VK_SUCCESS) {
        Check(presentResult, "vkQueuePresentKHR");
    }
    return true;
}

int Renderer::Impl::Run() {
    try {
        previousFrameTime_ = std::chrono::steady_clock::now();
        const uint64_t automatedFrames = static_cast<uint64_t>(options_.warmup) + options_.frames;
        uint64_t submitted = 0;
        while (glfwWindowShouldClose(window_) == GLFW_FALSE &&
               (!automated_ || submitted < automatedFrames)) {
            glfwPollEvents();
            const auto now = std::chrono::steady_clock::now();
            float delta = options_.fixedDelta;
            if (!automated_) {
                delta = std::clamp(std::chrono::duration<float>(now - previousFrameTime_).count(),
                                   0.0f, 1.0f / 30.0f);
            }
            previousFrameTime_ = now;

            scene_->Update(window_, delta, simulationTime_);
            const float simulationDelta = scene_->IsPaused() ? 0.0f : delta;
            if (scene_->ConsumeResetRequested()) {
                Check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle(reset)");
                ProcessAllCompletedFramesInOrder();
                UploadOriginalBlades();
            }
            if (DrawFrame(submitted, static_cast<uint32_t>(submitted), simulationDelta,
                          simulationTime_)) {
                ++submitted;
                simulationTime_ += simulationDelta;
            }
        }

        Check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle(final)");
        ProcessAllCompletedFramesInOrder();
        if (benchmarkFile_) {
            benchmarkFile_.flush();
            if (!benchmarkFile_) {
                throw std::runtime_error("Failed while flushing benchmark CSV");
            }
        }
        if (physicsProbeFile_.is_open()) {
            physicsProbeFile_.flush();
            if (!physicsProbeFile_) {
                throw std::runtime_error("Failed while flushing physics probe CSV");
            }
        }
        WriteMetadata();

        const bool automatedComplete = !automated_ || submitted == automatedFrames;
        if (automated_) {
            std::ostream& report = automatedComplete ? std::cout : std::cerr;
            report << "Automated run submitted " << submitted << " of " << automatedFrames
                   << " requested frame(s) (" << options_.warmup << " warmup + "
                   << options_.frames << " measured)\n";
        }

        const bool benchmarkComplete = !benchmarkEnabled_ ||
            benchmarkRowsWritten_ == options_.frames;
        if (!benchmarkComplete) {
            std::cerr << "Benchmark wrote " << benchmarkRowsWritten_ << " measured row(s), expected "
                      << options_.frames << '\n';
        }
        const bool captureComplete = !captureEnabled_ ||
            captureFramesWritten_ == options_.frames;
        if (!captureComplete) {
            std::cerr << "Capture wrote " << captureFramesWritten_ << " measured frame(s), expected "
                      << options_.frames << '\n';
        }
        const bool verificationComplete = !options_.verify ||
            verificationFramesProcessed_ == options_.frames;

        if (!verificationFailures_.empty()) {
            std::cerr << "Verification processed " << verificationFramesProcessed_ << " of "
                      << options_.frames << " measured frame(s) and failed with "
                      << verificationFailures_.size() << " recorded violation(s):\n";
            for (const std::string& failure : verificationFailures_) {
                std::cerr << "  " << failure << '\n';
            }
        } else if (options_.verify && verificationComplete) {
            std::cout << "Verification passed for " << verificationFramesProcessed_
                      << " measured frame(s)\n";
        } else if (options_.verify) {
            std::cerr << "Verification processed " << verificationFramesProcessed_ << " of "
                      << options_.frames << " measured frame(s)\n";
        }
        const bool verificationFailed = !verificationFailures_.empty();
        Cleanup();
        const uint32_t validationErrorCount = validationErrors_.load(std::memory_order_relaxed);
        if (validationErrorCount != 0) {
            std::cerr << "Vulkan validation reported " << validationErrorCount << " error(s)\n";
        }
        return (verificationFailed || !automatedComplete || !benchmarkComplete ||
                !captureComplete || !verificationComplete || validationErrorCount != 0)
            ? 1
            : 0;
    } catch (const std::exception& error) {
        std::cerr << "Renderer error: " << error.what() << '\n';
        Cleanup();
        return 1;
    }
}

void Renderer::Impl::RecreateSwapchain() {
    WaitForNonzeroFramebuffer();
    if (glfwWindowShouldClose(window_) == GLFW_TRUE) {
        return;
    }
    Check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle(recreate swapchain)");
    framebufferResized_ = false;
    ProcessAllCompletedFramesInOrder();
    DestroyFrameResources();
    DestroyPipelines();
    DestroyRenderPasses();
    DestroySwapchain();
    CreateSwapchain();
    CreateRenderPasses();
    CreatePipelines();
    CreateFrameResources();
}

void Renderer::Impl::Cleanup() noexcept {
    if (device_) {
        vkDeviceWaitIdle(device_);
        DestroyFrameResources();
        DestroyPipelines();
        DestroyRenderPasses();
        DestroySwapchain();
        if (shadowSampler_) vkDestroySampler(device_, shadowSampler_, nullptr);
        shadowSampler_ = VK_NULL_HANDLE;
        if (depthSampler_) vkDestroySampler(device_, depthSampler_, nullptr);
        depthSampler_ = VK_NULL_HANDLE;
        if (reductionSetLayout_) vkDestroyDescriptorSetLayout(device_, reductionSetLayout_, nullptr);
        if (depthSetLayout_) vkDestroyDescriptorSetLayout(device_, depthSetLayout_, nullptr);
        if (dataSetLayout_) vkDestroyDescriptorSetLayout(device_, dataSetLayout_, nullptr);
        if (frameSetLayout_) vkDestroyDescriptorSetLayout(device_, frameSetLayout_, nullptr);
        reductionSetLayout_ = depthSetLayout_ = dataSetLayout_ = frameSetLayout_ = VK_NULL_HANDLE;
        DestroyBuffer(sceneIndices_);
        DestroyBuffer(sceneVertices_);
        DestroyBuffer(persistentBlades_);
        scene_.reset();
        if (commandPool_) vkDestroyCommandPool(device_, commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    } else {
        scene_.reset();
    }
    if (debugMessenger_ && instance_) {
        const auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyMessenger) destroyMessenger(instance_, debugMessenger_, nullptr);
        debugMessenger_ = VK_NULL_HANDLE;
    }
    if (surface_ && instance_) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (instance_) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    if (glfwInitialized_) {
        glfwTerminate();
        glfwInitialized_ = false;
    }
    if (physicsProbeFile_.is_open()) physicsProbeFile_.close();
    if (benchmarkFile_.is_open()) benchmarkFile_.close();
}
