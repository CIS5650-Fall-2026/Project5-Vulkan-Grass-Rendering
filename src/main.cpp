#include <vulkan/vulkan.h>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include "Instance.h"
#include "Window.h"
#include "Renderer.h"
#include "Camera.h"
#include "Scene.h"
#include "Image.h"

Device* device;
SwapChain* swapChain;
Renderer* renderer;
Camera* camera;
Scene* scene;

namespace {
    // Restart timing when the scene changes
    bool restartMeasurements = false;

    void resizeCallback(GLFWwindow* window, int width, int height) {
        if (width == 0 || height == 0) return;

        renderer->RecreateFrameResources();
        restartMeasurements = true;
    }

    bool leftMouseDown = false;
    bool rightMouseDown = false;
    double previousX = 0.0;
    double previousY = 0.0;

    void mouseDownCallback(GLFWwindow* window, int button, int action, int mods) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            if (action == GLFW_PRESS) {
                leftMouseDown = true;
                glfwGetCursorPos(window, &previousX, &previousY);
            }
            else if (action == GLFW_RELEASE) {
                leftMouseDown = false;
            }
        } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            if (action == GLFW_PRESS) {
                rightMouseDown = true;
                glfwGetCursorPos(window, &previousX, &previousY);
            }
            else if (action == GLFW_RELEASE) {
                rightMouseDown = false;
            }
        }
    }

    void mouseMoveCallback(GLFWwindow* window, double xPosition, double yPosition) {
        if (leftMouseDown) {
            double sensitivity = 0.5;
            float deltaX = static_cast<float>((previousX - xPosition) * sensitivity);
            float deltaY = static_cast<float>((previousY - yPosition) * sensitivity);

            camera->UpdateOrbit(deltaX, deltaY, 0.0f);
            restartMeasurements = true;

            previousX = xPosition;
            previousY = yPosition;
        } else if (rightMouseDown) {
            double deltaZ = static_cast<float>((previousY - yPosition) * 0.05);

            camera->UpdateOrbit(0.0f, 0.0f, deltaZ);
            restartMeasurements = true;

            previousY = yPosition;
        }
    }

    void printCullingSettings() {
        uint32_t mask = scene->GetCullingMask();
        std::cout << "Culling: orientation=" << ((mask & OrientationCulling) ? "on" : "off")
            << ", frustum=" << ((mask & FrustumCulling) ? "on" : "off")
            << ", distance=" << ((mask & DistanceCulling) ? "on" : "off") << '\n';
    }

    void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
        if (action == GLFW_PRESS && key >= GLFW_KEY_1 && key <= GLFW_KEY_3) {
            scene->ToggleCulling(static_cast<CullingTest>(1u << (key - GLFW_KEY_1)));
            printCullingSettings();
            restartMeasurements = true;
        }
    }
}

int main(int argc, char** argv) {
    uint32_t bladeCount = NUM_BLADES;
    constexpr uint32_t warmupFrames = 120;
    constexpr uint32_t sampleFrames = 1000;
    for (int i = 1; i < argc; ++i) {
        std::string option = argv[i];
        if (option == "--blades") {
            bladeCount = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
    }
    // Same grass on each launch
    std::srand(0);
    static constexpr char* applicationName = "Vulkan Grass Rendering";
    constexpr int windowWidth = 1280;
    constexpr int windowHeight = 720;
    InitializeWindow(windowWidth, windowHeight, applicationName);

    unsigned int glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    Instance* instance = new Instance(applicationName, glfwExtensionCount, glfwExtensions);

    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(instance->GetVkInstance(), GetGLFWWindow(), nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create window surface");
    }

    instance->PickPhysicalDevice({ VK_KHR_SWAPCHAIN_EXTENSION_NAME }, QueueFlagBit::GraphicsBit | QueueFlagBit::TransferBit | QueueFlagBit::ComputeBit | QueueFlagBit::PresentBit, surface);

    VkPhysicalDeviceFeatures deviceFeatures = {};
    deviceFeatures.tessellationShader = VK_TRUE;
    deviceFeatures.fillModeNonSolid = VK_TRUE;
    deviceFeatures.samplerAnisotropy = VK_TRUE;

    device = instance->CreateDevice(QueueFlagBit::GraphicsBit | QueueFlagBit::TransferBit | QueueFlagBit::ComputeBit | QueueFlagBit::PresentBit, deviceFeatures);

    swapChain = device->CreateSwapChain(surface, 5);

    camera = new Camera(device, static_cast<float>(windowWidth) / windowHeight);

    VkCommandPoolCreateInfo transferPoolInfo = {};
    transferPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    // Upload helpers use the graphics queue
    transferPoolInfo.queueFamilyIndex = device->GetQueueIndex(QueueFlags::Graphics);
    transferPoolInfo.flags = 0;

    VkCommandPool transferCommandPool;
    if (vkCreateCommandPool(device->GetVkDevice(), &transferPoolInfo, nullptr, &transferCommandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool");
    }

    VkImage grassImage;
    VkDeviceMemory grassImageMemory;
    Image::FromFile(device,
        transferCommandPool,
        "images/grass.jpg",
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        grassImage,
        grassImageMemory
    );

    float planeDim = 15.f;
    float halfWidth = planeDim * 0.5f;
    Model* plane = new Model(device, transferCommandPool,
        {
            { { -halfWidth, 0.0f, halfWidth }, { 1.0f, 0.0f, 0.0f },{ 1.0f, 0.0f } },
            { { halfWidth, 0.0f, halfWidth }, { 0.0f, 1.0f, 0.0f },{ 0.0f, 0.0f } },
            { { halfWidth, 0.0f, -halfWidth }, { 0.0f, 0.0f, 1.0f },{ 0.0f, 1.0f } },
            { { -halfWidth, 0.0f, -halfWidth }, { 1.0f, 1.0f, 1.0f },{ 1.0f, 1.0f } }
        },
        { 0, 1, 2, 2, 3, 0 }
    );
    plane->SetTexture(grassImage);
    
    Blades* blades = new Blades(device, transferCommandPool, planeDim, bladeCount);

    vkDestroyCommandPool(device->GetVkDevice(), transferCommandPool, nullptr);

    scene = new Scene(device);
    scene->AddModel(plane);
    scene->AddBlades(blades);

    renderer = new Renderer(device, swapChain, scene, camera);

    glfwSetWindowSizeCallback(GetGLFWWindow(), resizeCallback);
    glfwSetMouseButtonCallback(GetGLFWWindow(), mouseDownCallback);
    glfwSetCursorPosCallback(GetGLFWWindow(), mouseMoveCallback);
    glfwSetKeyCallback(GetGLFWWindow(), keyCallback);

    std::cout << "Grass blades=" << bladeCount << '\n';
    std::cout << "Keys: 1 orientation, 2 frustum, 3 distance culling.\n";
    printCullingSettings();
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(instance->GetPhysicalDevice(), &properties);
    VkPresentModeKHR presentMode = swapChain->GetPresentMode();
    VkExtent2D extent = swapChain->GetVkExtent();
    std::cout << "GPU=" << properties.deviceName << ", resolution=" << extent.width << 'x' << extent.height
        << ", present-mode=" << (presentMode == VK_PRESENT_MODE_MAILBOX_KHR ? "MAILBOX" :
            presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "IMMEDIATE" : "FIFO")
        << ", validation=" << (ENABLE_VALIDATION ? "on" : "off") << '\n';

    // Wall time, including GPU waits
    using FrameClock = std::chrono::steady_clock;
    auto previousFrameStart = FrameClock::now();
    bool framePending = false;
    uint64_t completedFrames = 0;
    uint32_t measuredFrames = 0;
    double measuredSeconds = 0.0;

    while (!ShouldQuit()) {
        renderer->BeginFrame();
        auto completionTime = FrameClock::now();
        if (framePending) {
            ++completedFrames;
            if (completedFrames > warmupFrames) {
                ++measuredFrames;
                measuredSeconds += std::chrono::duration<double>(completionTime - previousFrameStart).count();
            }
            if (measuredFrames == sampleFrames) {
                // Exclude reporting from the next sample
                std::cout << std::fixed << std::setprecision(3)
                    << "Stats: frames=" << completedFrames - measuredFrames + 1 << '-' << completedFrames
                    << ", FPS=" << measuredFrames / measuredSeconds
                    << ", mean_ms=" << 1000.0 * measuredSeconds / measuredFrames << '\n';
                measuredFrames = 0;
                measuredSeconds = 0.0;
            }
        }

        // Callbacks run after the fence wait
        previousFrameStart = FrameClock::now();
        glfwPollEvents();
        if (ShouldQuit()) break;
        if (restartMeasurements) {
            completedFrames = 0;
            measuredFrames = 0;
            measuredSeconds = 0.0;
            restartMeasurements = false;
        }
        scene->UpdateTime();
        framePending = renderer->Frame();
        if (!framePending) {
            // Restart timing after a resize
            restartMeasurements = true;
        }
    }

    vkDeviceWaitIdle(device->GetVkDevice());

    // Drop descriptors before their buffers
    delete renderer;
    delete scene;
    delete plane;
    delete blades;
    delete camera;
    vkDestroyImage(device->GetVkDevice(), grassImage, nullptr);
    vkFreeMemory(device->GetVkDevice(), grassImageMemory, nullptr);
    delete swapChain;
    delete device;
    delete instance;
    DestroyWindow();
    return 0;
}
