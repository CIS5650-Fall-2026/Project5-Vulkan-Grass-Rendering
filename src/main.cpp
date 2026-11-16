#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
#include "Instance.h"
#include "Window.h"
#include "Renderer.h"
#include "Camera.h"
#include "Scene.h"
#include "Terrain.h"
#include "Options.h"

Device* device;
SwapChain* swapChain;
Renderer* renderer;
Camera* camera;

namespace {
    // the default fly speed crosses the terrain in this time
    const float SECONDS_TO_CROSS_TERRAIN = 8.0f;
    // the longest fly step a stalled frame may take
    const float MAX_FRAME_SECONDS = 0.25f;
    const float FIXED_STEP_SECONDS = 1.0f / 60.0f;

    bool leftMouseDown = false;
    bool rightMouseDown = false;
    bool middleMouseDown = false;
    double lastX = 0.0;
    double lastY = 0.0;

    void resizeCallback(GLFWwindow* window, int width, int height) {
        if (width == 0 || height == 0) return;

        vkDeviceWaitIdle(device->GetVkDevice());
        swapChain->Recreate();
        renderer->RecreateFrameResources();
        camera->SetViewport(width, height);
    }

    bool altHeld(GLFWwindow* window) {
        return glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
    }

    // keys are polled every frame so a held key moves smoothly; not with Alt, where RMB dollies
    void flyFromKeys(GLFWwindow* window, float seconds) {
        if (!rightMouseDown || altHeld(window)) {
            return;
        }
        auto held = [window](int key) { return glfwGetKey(window, key) == GLFW_PRESS ? 1 : 0; };
        camera->Fly(held(GLFW_KEY_W) - held(GLFW_KEY_S), held(GLFW_KEY_D) - held(GLFW_KEY_A),
            held(GLFW_KEY_E) - held(GLFW_KEY_Q), seconds);
    }

    void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
        if (action != GLFW_PRESS) {
            return;
        }
        if (key == GLFW_KEY_ESCAPE) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        // not while flying, where F sits next to the movement keys
        else if (key == GLFW_KEY_F && !rightMouseDown) {
            camera->ResetToHome();
        }
        else if (key == GLFW_KEY_P && !rightMouseDown) {
            fprintf(stderr, "--pose %s\n", FormatPose(camera->GetPose()).c_str());
        }
    }

    void mouseDownCallback(GLFWwindow* window, int button, int action, int mods) {
        bool press = action == GLFW_PRESS;
        switch (button) {
            case GLFW_MOUSE_BUTTON_LEFT:
                leftMouseDown = press;
                break;
            case GLFW_MOUSE_BUTTON_RIGHT:
                rightMouseDown = press;
                break;
            case GLFW_MOUSE_BUTTON_MIDDLE:
                middleMouseDown = press;
                break;
            default:
                return;
        }
        bool dragging = leftMouseDown || rightMouseDown || middleMouseDown;
        glfwSetInputMode(window, GLFW_CURSOR, dragging ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        glfwGetCursorPos(window, &lastX, &lastY);
    }

    void mouseMoveCallback(GLFWwindow* window, double xPosition, double yPosition) {
        double dx = xPosition - lastX;
        double dy = yPosition - lastY;
        lastX = xPosition;
        lastY = yPosition;
        if (dx == 0.0 && dy == 0.0) {
            return;
        }

        bool alt = altHeld(window);
        if (alt && leftMouseDown) {
            camera->Orbit(dx, dy);
        } else if (alt && rightMouseDown) {
            camera->DollyToPivot(dx, dy);
        } else if (middleMouseDown || (leftMouseDown && rightMouseDown)) {
            camera->Pan(dx, dy);
        } else if (rightMouseDown) {
            camera->Turn(dx, dy);
        } else if (leftMouseDown) {
            camera->MoveAlongGround(dx, dy);
        }
    }

    void scrollCallback(GLFWwindow* window, double xOffset, double yOffset) {
        if (rightMouseDown) {
            camera->ScaleFlySpeed(static_cast<float>(yOffset));
        } else {
            camera->MoveAlongView(static_cast<float>(yOffset));
        }
    }

    double median(std::vector<double> values) {
        std::sort(values.begin(), values.end());
        size_t n = values.size();
        return n % 2 ? values[n / 2] : 0.5 * (values[n / 2 - 1] + values[n / 2]);
    }

    // returns false if the window was closed
    bool runFrames(Scene* scene, int count) {
        for (int i = 0; i < count; ++i) {
            if (ShouldQuit()) {
                return false;
            }
            glfwPollEvents();
            scene->UpdateTime(FIXED_STEP_SECONDS);
            renderer->Frame();
        }
        return true;
    }

    // one CSV row of median GPU pass times; presents are vsync-capped, so wall-clock frame time says nothing here
    void runBench(Scene* scene, Blades* blades, const Options& options) {
        if (!runFrames(scene, options.warmupFrames)) {
            return;
        }

        std::vector<double> compute, terrain, grass, total;
        for (int i = 0; i < options.timedFrames; ++i) {
            if (!runFrames(scene, 1)) {
                return;
            }
            vkDeviceWaitIdle(device->GetVkDevice());
            GpuTimings t = renderer->ReadTimings();
            compute.push_back(t.compute);
            terrain.push_back(t.terrain);
            grass.push_back(t.grass);
            total.push_back(t.compute + t.terrain + t.grass);
        }
        uint32_t drawn = renderer->ReadDrawnBladeCount();

        char row[512];
        snprintf(row, sizeof(row), "\"%s\",%u,%d,%d,%d,%d,%d,0x%x,\"%s\",%u,%.4f,%.4f,%.4f,%.4f",
            options.tag.c_str(), blades->GetCount(), options.subgroupCompaction, options.orientationCulling,
            options.frustumCulling, options.distanceCulling, options.hostVisibleCulled, blades->GetCulledMemoryFlags(),
            FormatPose(camera->GetPose()).c_str(), drawn, median(compute), median(terrain), median(grass), median(total));
        const char* header = "tag,blades,compaction,orientation,frustum,distance,host_visible_culled,culled_memory_flags,"
            "pose,drawn,compute_ms,terrain_ms,grass_ms,gpu_ms";

        printf("%s\n%s\n", header, row);
        if (!options.csvPath.empty()) {
            FILE* file = fopen(options.csvPath.c_str(), "a+");
            if (file == nullptr) {
                throw std::runtime_error("Failed to open " + options.csvPath);
            }
            fseek(file, 0, SEEK_END);
            if (ftell(file) == 0) {
                fprintf(file, "%s\n", header);
            }
            fprintf(file, "%s\n", row);
            fclose(file);
        }
    }

    void runCapture(Scene* scene, const Options& options) {
        if (!runFrames(scene, options.warmupFrames)) {
            return;
        }
        if (!options.capturePath.empty()) {
            glfwPollEvents();
            scene->UpdateTime(FIXED_STEP_SECONDS);
            renderer->Frame(options.capturePath.c_str());
        }
        for (int i = 0; i < options.recordFrames && !ShouldQuit(); ++i) {
            char path[512];
            snprintf(path, sizeof(path), "%s/frame_%04d.png", options.recordDir.c_str(), i);
            glfwPollEvents();
            scene->UpdateTime(FIXED_STEP_SECONDS);
            renderer->Frame(path);
        }
    }
}

int main(int argc, char** argv) {
    Options options;
    try {
        options = ParseOptions(argc, argv);
    } catch (const std::exception& e) {
        fprintf(stderr, "%s\n", e.what());
        return 1;
    }

    static constexpr char* applicationName = "Vulkan Grass Rendering";
    static constexpr int windowWidth = 1920;
    static constexpr int windowHeight = 1080;
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

    camera = new Camera(device, windowWidth, windowHeight);

    VkCommandPoolCreateInfo transferPoolInfo = {};
    transferPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    transferPoolInfo.queueFamilyIndex = device->GetInstance()->GetQueueFamilyIndices()[QueueFlags::Transfer];
    transferPoolInfo.flags = 0;

    VkCommandPool transferCommandPool;
    if (vkCreateCommandPool(device->GetVkDevice(), &transferPoolInfo, nullptr, &transferCommandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool");
    }

    // 128 m square, 0.5 m cells
    Terrain terrain(128.0f, 256);
    std::vector<Vertex> terrainVertices;
    std::vector<uint32_t> terrainIndices;
    terrain.BuildMesh(terrainVertices, terrainIndices);
    Model* ground = new Model(device, transferCommandPool, terrainVertices, terrainIndices);

    Blades* blades = new Blades(device, transferCommandPool, terrain, options.blades, options.hostVisibleCulled);

    glm::vec3 center(0.0f, terrain.HeightAt(0.0f, 0.0f), 0.0f);
    if (options.hasPose) {
        camera->SetHome(options.pose);
    } else {
        camera->SetHome(center + glm::vec3(0.0f, 11.0f, 28.0f), center);
    }
    camera->SetFlySpeed(terrain.GetSize() / SECONDS_TO_CROSS_TERRAIN);

    vkDestroyCommandPool(device->GetVkDevice(), transferCommandPool, nullptr);

    Scene* scene = new Scene(device);
    scene->AddModel(ground);
    scene->AddBlades(blades);

    renderer = new Renderer(device, swapChain, scene, camera, options);

    glfwSetWindowSizeCallback(GetGLFWWindow(), resizeCallback);
    glfwSetMouseButtonCallback(GetGLFWWindow(), mouseDownCallback);
    glfwSetCursorPosCallback(GetGLFWWindow(), mouseMoveCallback);
    glfwSetScrollCallback(GetGLFWWindow(), scrollCallback);
    glfwSetKeyCallback(GetGLFWWindow(), keyCallback);

    if (options.bench) {
        runBench(scene, blades, options);
    } else if (options.FixedStep()) {
        runCapture(scene, options);
    } else {
        double lastFrameTime = glfwGetTime();
        while (!ShouldQuit()) {
            glfwPollEvents();
            double now = glfwGetTime();
            flyFromKeys(GetGLFWWindow(), static_cast<float>(std::min(now - lastFrameTime, static_cast<double>(MAX_FRAME_SECONDS))));
            lastFrameTime = now;
            scene->UpdateTime();
            renderer->Frame();
        }
    }

    vkDeviceWaitIdle(device->GetVkDevice());

    delete scene;
    delete ground;
    delete blades;
    delete camera;
    delete renderer;
    delete swapChain;
    delete device;
    vkDestroySurfaceKHR(instance->GetVkInstance(), surface, nullptr);
    delete instance;
    DestroyWindow();
    return 0;
}
