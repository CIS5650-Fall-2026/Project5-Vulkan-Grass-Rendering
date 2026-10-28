#include <fstream>
#include <string>
#include <vulkan/vulkan.h>
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

namespace {
    void resizeCallback(GLFWwindow* window, int width, int height) {
        if (width == 0 || height == 0) return;

        vkDeviceWaitIdle(device->GetVkDevice());
        swapChain->Recreate();
        renderer->RecreateFrameResources();
    }

    bool leftMouseDown = false;
    bool rightMouseDown = false;
    bool draggingCollider = false;
    double previousX = 0.0;
    double previousY = 0.0;

    void mouseDownCallback(GLFWwindow* window, int button, int action, int mods) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            if (action == GLFW_PRESS) {
                double cursorX;
                double cursorY;
                glfwGetCursorPos(window, &cursorX, &cursorY);
                int width;
                int height;
                glfwGetWindowSize(window, &width, &height);
                Scene* scene = static_cast<Scene*>(glfwGetWindowUserPointer(window));
                draggingCollider = scene != nullptr && camera->ScreenRayIntersectsSphere(
                    cursorX, cursorY, width, height, scene->GetColliderPosition(), scene->GetColliderRadius());
                if (draggingCollider) {
                    scene->BeginColliderDrag();
                    leftMouseDown = false;
                } else {
                    leftMouseDown = true;
                    previousX = cursorX;
                    previousY = cursorY;
                }
            }
            else if (action == GLFW_RELEASE) {
                leftMouseDown = false;
                if (draggingCollider) {
                    Scene* scene = static_cast<Scene*>(glfwGetWindowUserPointer(window));
                    if (scene != nullptr) {
                        scene->EndColliderDrag();
                    }
                    draggingCollider = false;
                }
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
        if (draggingCollider) {
            Scene* scene = static_cast<Scene*>(glfwGetWindowUserPointer(window));
            int width;
            int height;
            glfwGetWindowSize(window, &width, &height);
            glm::vec3 dragPosition;
            if (scene != nullptr && camera->ScreenToWorldOnPlane(
                xPosition, yPosition, width, height, scene->GetColliderPosition().y, dragPosition)) {
                scene->DragColliderTo(dragPosition);
            }
        } else if (leftMouseDown) {
            double sensitivity = 0.5;
            float deltaX = static_cast<float>((previousX - xPosition) * sensitivity);
            float deltaY = static_cast<float>((previousY - yPosition) * sensitivity);

            camera->UpdateOrbit(deltaX, deltaY, 0.0f);

            previousX = xPosition;
            previousY = yPosition;
        } else if (rightMouseDown) {
            int height;
            glfwGetWindowSize(window, nullptr, &height);
            camera->UpdatePan(
                static_cast<float>(xPosition - previousX),
                static_cast<float>(yPosition - previousY),
                height);
            previousX = xPosition;
            previousY = yPosition;
        }
    }

    void scrollCallback(GLFWwindow* window, double, double yOffset) {
        camera->UpdateOrbit(0.0f, 0.0f, static_cast<float>(yOffset) * 0.5f);
    }

    void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
        if (action != GLFW_PRESS && action != GLFW_REPEAT) {
            return;
        }
        if ((key == GLFW_KEY_F1 || key == GLFW_KEY_F2 || key == GLFW_KEY_F3 || key == GLFW_KEY_P) &&
            action != GLFW_PRESS) {
            return;
        }

        Scene* scene = static_cast<Scene*>(glfwGetWindowUserPointer(window));
        if (scene == nullptr) {
            return;
        }

        const float directionStep = 0.1745329252f;
        switch (key) {
        case GLFW_KEY_W:
            scene->AdjustWind(0.25f, 0.0f);
            break;
        case GLFW_KEY_S:
            scene->AdjustWind(-0.25f, 0.0f);
            break;
        case GLFW_KEY_A:
            scene->AdjustWind(0.0f, -directionStep);
            break;
        case GLFW_KEY_D:
            scene->AdjustWind(0.0f, directionStep);
            break;
        case GLFW_KEY_R:
            scene->ResetWind();
            break;
        case GLFW_KEY_UP:
            scene->MoveCollider(0.0f, -0.25f);
            break;
        case GLFW_KEY_DOWN:
            scene->MoveCollider(0.0f, 0.25f);
            break;
        case GLFW_KEY_LEFT:
            scene->MoveCollider(-0.25f, 0.0f);
            break;
        case GLFW_KEY_RIGHT:
            scene->MoveCollider(0.25f, 0.0f);
            break;
        case GLFW_KEY_1:
            scene->SetActiveBladeCount(1024);
            break;
        case GLFW_KEY_2:
            scene->SetActiveBladeCount(2048);
            break;
        case GLFW_KEY_3:
            scene->SetActiveBladeCount(4096);
            break;
        case GLFW_KEY_4:
            scene->SetActiveBladeCount(NUM_BLADES);
            break;
        case GLFW_KEY_F1:
            scene->ToggleCullingTest(OrientationCulling);
            break;
        case GLFW_KEY_F2:
            scene->ToggleCullingTest(FrustumCulling);
            break;
        case GLFW_KEY_F3:
            scene->ToggleCullingTest(DistanceCulling);
            break;
        case GLFW_KEY_P:
            renderer->TogglePerformanceCapture();
            break;
        default:
            break;
        }
    }
}

namespace {
    std::string ResolveProjectPath(const std::string& relativePath) {
        const std::string candidates[] = {
            relativePath,
            std::string("../") + relativePath,
            std::string("../src/") + relativePath,
            std::string("../build/src/") + relativePath,
            std::string("src/") + relativePath,
            std::string("build/src/") + relativePath,
            std::string("../") + std::string("src/") + relativePath,
            std::string("../") + std::string("build/src/") + relativePath,
        };

        for (const std::string& candidate : candidates) {
            std::ifstream test(candidate.c_str());
            if (test.good()) {
                return candidate;
            }
        }

        return relativePath;
    }
}

int main() {
    static constexpr char* applicationName = "Vulkan Grass Rendering";
    InitializeWindow(640, 480, applicationName);

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

    camera = new Camera(device, 640.f / 480.f);

    VkCommandPoolCreateInfo transferPoolInfo = {};
    transferPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    transferPoolInfo.queueFamilyIndex = device->GetInstance()->GetQueueFamilyIndices()[QueueFlags::Transfer];
    transferPoolInfo.flags = 0;

    VkCommandPool transferCommandPool;
    if (vkCreateCommandPool(device->GetVkDevice(), &transferPoolInfo, nullptr, &transferCommandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool");
    }

    VkImage grassImage;
    VkDeviceMemory grassImageMemory;
    const std::string grassImagePath = ResolveProjectPath("src/images/grass.jpg");
    Image::FromFile(device,
        transferCommandPool,
        grassImagePath.c_str(),
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

    const uint32_t sphereRings = 16;
    const uint32_t sphereSegments = 24;
    std::vector<Vertex> sphereVertices;
    std::vector<uint32_t> sphereIndices;
    for (uint32_t ring = 0; ring <= sphereRings; ++ring) {
        const float theta = 3.14159265359f * static_cast<float>(ring) / sphereRings;
        for (uint32_t segment = 0; segment <= sphereSegments; ++segment) {
            const float phi = 6.28318530718f * static_cast<float>(segment) / sphereSegments;
            const glm::vec3 position(
                sinf(theta) * cosf(phi),
                cosf(theta),
                sinf(theta) * sinf(phi));
            sphereVertices.push_back({ position, glm::vec3(0.25f, 0.65f, 0.95f),
                glm::vec2(static_cast<float>(segment) / sphereSegments, static_cast<float>(ring) / sphereRings) });
        }
    }
    for (uint32_t ring = 0; ring < sphereRings; ++ring) {
        for (uint32_t segment = 0; segment < sphereSegments; ++segment) {
            const uint32_t first = ring * (sphereSegments + 1) + segment;
            const uint32_t nextRing = first + sphereSegments + 1;
            sphereIndices.insert(sphereIndices.end(), { first, first + 1, nextRing, first + 1, nextRing + 1, nextRing });
        }
    }
    Model* colliderSphere = new Model(device, transferCommandPool, sphereVertices, sphereIndices);
    colliderSphere->SetTexture(grassImage);
    colliderSphere->SetMaterialColor(glm::vec4(0.25f, 0.65f, 0.95f, 0.0f));
    
    Blades* blades = new Blades(device, transferCommandPool, planeDim);

    vkDestroyCommandPool(device->GetVkDevice(), transferCommandPool, nullptr);

    Scene* scene = new Scene(device);
    scene->AddModel(plane);
    scene->AddModel(colliderSphere);
    scene->SetColliderModel(colliderSphere);
    scene->AddBlades(blades);

    renderer = new Renderer(device, swapChain, scene, camera);

    glfwSetWindowSizeCallback(GetGLFWWindow(), resizeCallback);
    glfwSetMouseButtonCallback(GetGLFWWindow(), mouseDownCallback);
    glfwSetCursorPosCallback(GetGLFWWindow(), mouseMoveCallback);
    glfwSetScrollCallback(GetGLFWWindow(), scrollCallback);
    glfwSetWindowUserPointer(GetGLFWWindow(), scene);
    glfwSetKeyCallback(GetGLFWWindow(), keyCallback);

    while (!ShouldQuit()) {
        glfwPollEvents();
        scene->UpdateTime();
        renderer->Frame();
    }

    vkDeviceWaitIdle(device->GetVkDevice());

    vkDestroyImage(device->GetVkDevice(), grassImage, nullptr);
    vkFreeMemory(device->GetVkDevice(), grassImageMemory, nullptr);

    delete scene;
    delete plane;
    delete colliderSphere;
    delete blades;
    delete camera;
    delete renderer;
    delete swapChain;
    delete device;
    delete instance;
    DestroyWindow();
    return 0;
}
