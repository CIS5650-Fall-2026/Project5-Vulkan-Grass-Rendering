#include <vulkan/vulkan.h>
#include "Instance.h"
#include "Window.h"
#include "Renderer.h"
#include "Camera.h"
#include "Scene.h"
#include "Image.h"
#include "Diagnostics.h"
#include "InteractionSphere.h"
#include <iostream>
#include <string>
#include <cmath>
#include <limits>
#include <sstream>
#include <algorithm>

Device* device;
SwapChain* swapChain;
Renderer* renderer;
Camera* camera;
Scene* scene;

namespace {
    struct Options {
        GrassPreset preset = GrassPreset::Field;
        std::string scene = "field";
        uint32_t frames = 0;
        bool wireframe = false;
        bool overrideYaw = false;
        float yaw = 0.0f;
        bool overridePitch = false, overrideRadius = false;
        float pitch = 0.0f, radius = 0.0f, viewTurn = 0.0f;
        float orbitStep = 0.0f, zoomStep = 0.0f;
        uint32_t fieldCount = NUM_BLADES;
        bool help = false;
        SimulationMode simulation = SimulationMode::Physics;
        std::string simulationName = "physics";
        double fixedDt = 0.0;
        uint32_t forces = Gravity | Recovery | Wind;
        float wind = 8.0f;
        float gravity = 3.0f;
        bool paused = false;
        std::string diagnostics;
        std::string dumpState;
        uint32_t sampleEvery = 30;
        uint32_t culling = AllCulling;
        bool explicitCulling = false;
        uint32_t buckets = 4;
        float maxDistance = 60.0f, orientationThreshold = 0.9f, frustumTolerance = 0.01f;
        std::string timings;
        std::string geometryStats;
        bool lod = true, lodColors = false;
        float lodNear = 15.0f, lodFar = 45.0f, lodMax = 8.0f, lodMin = 2.0f;
        bool interaction = false, sphereVisible = false;
        glm::vec3 spherePosition = glm::vec3(-4.0f, 1.2f, 0.0f);
        float sphereRadius = 1.1f, contactStrength = 35.0f, sphereSpeed = 3.0f, sphereTravel = 14.0f;
        SphereMotion sphereMotion = SphereMotion::Manual;
        std::string interactionStats;
        std::vector<std::pair<uint32_t, std::string>> events;
    };

    uint32_t positiveInteger(const std::string& text) {
        size_t end = 0;
        const unsigned long count = std::stoul(text, &end);
        if (text.empty() || text[0] == '-' || end != text.size() || count == 0 || count > std::numeric_limits<uint32_t>::max())
            throw std::runtime_error("Expected a positive integer");
        return static_cast<uint32_t>(count);
    }

    double boundedNumber(const std::string& text, double lower, double upper) {
        size_t end = 0;
        double result = std::stod(text, &end);
        if (end != text.size() || !std::isfinite(result) || result < lower || result > upper)
            throw std::runtime_error("Number is outside the supported range: " + text);
        return result;
    }

    glm::vec3 scriptedMovement(0.0f);
    bool isControl(const std::string& value) {
        return value == "pause" || value == "resume" || value == "step" || value == "reset"
            || value == "gravity" || value == "recovery" || value == "wind" || value == "culling"
            || value == "cull-none" || value == "cull-frustum" || value == "cull-orientation"
            || value == "cull-distance" || value == "cull-all" || value == "look-away" || value == "look-home"
            || value == "lod" || value == "lod-on" || value == "lod-off" || value == "lod-colors"
            || value == "interaction" || value == "contact-on" || value == "contact-off"
            || value == "sphere-auto" || value == "sphere-orbit" || value == "sphere-pass" || value == "sphere-manual"
            || value == "sphere-home" || value == "sphere-away"
            || value == "move-left" || value == "move-right" || value == "move-forward" || value == "move-back"
            || value == "move-up" || value == "move-down" || value == "move-stop";
    }

    const char* cullingName(uint32_t mask) {
        switch (mask) {
            case 0: return "none";
            case FrustumCulling: return "frustum";
            case OrientationCulling: return "orientation";
            case DistanceCulling: return "distance";
            case AllCulling: return "all";
            case 8: return "compact";
            default: return "custom";
        }
    }

    void applyControl(const std::string& value) {
        if (value == "pause") scene->SetPaused(true);
        else if (value == "resume") scene->SetPaused(false);
        else if (value == "step") scene->Step();
        else if (value == "reset") scene->Reset();
        else if (value == "gravity") scene->ToggleForce(Gravity);
        else if (value == "recovery") scene->ToggleForce(Recovery);
        else if (value == "wind") scene->ToggleForce(Wind);
        else if (value == "culling") scene->CycleCulling();
        else if (value == "cull-none") scene->SetCulling(0);
        else if (value == "cull-frustum") scene->SetCulling(FrustumCulling);
        else if (value == "cull-orientation") scene->SetCulling(OrientationCulling);
        else if (value == "cull-distance") scene->SetCulling(DistanceCulling);
        else if (value == "cull-all") scene->SetCulling(AllCulling);
        else if (value == "look-away") camera->SetViewTurn(180.0f);
        else if (value == "look-home") camera->SetViewTurn(0.0f);
        else if (value == "lod") scene->SetLod(scene->GetSimulation().lodControls.x == 0u);
        else if (value == "lod-on") scene->SetLod(true);
        else if (value == "lod-off") scene->SetLod(false);
        else if (value == "lod-colors") scene->ToggleLodColors();
        else if (value == "interaction") scene->SetInteraction(scene->GetSimulation().contactControls.x == 0u);
        else if (value == "contact-on") scene->SetInteraction(true);
        else if (value == "contact-off") scene->SetInteraction(false);
        else if (value == "sphere-auto") scene->SetSphereMotion(scene->GetSimulation().contactControls.z == 0u ? SphereMotion::Orbit : SphereMotion::Manual);
        else if (value == "sphere-orbit") scene->SetSphereMotion(SphereMotion::Orbit);
        else if (value == "sphere-pass") scene->SetSphereMotion(SphereMotion::Pass);
        else if (value == "sphere-manual") scene->SetSphereMotion(SphereMotion::Manual);
        else if (value == "sphere-home") scene->ResetSphere();
        else if (value == "sphere-away") { scriptedMovement = glm::vec3(0); scene->MoveSphereAway(); }
        else if (value == "move-left") scriptedMovement = glm::vec3(-1, 0, 0);
        else if (value == "move-right") scriptedMovement = glm::vec3(1, 0, 0);
        else if (value == "move-forward") scriptedMovement = glm::vec3(0, 0, -1);
        else if (value == "move-back") scriptedMovement = glm::vec3(0, 0, 1);
        else if (value == "move-up") scriptedMovement = glm::vec3(0, 1, 0);
        else if (value == "move-down") scriptedMovement = glm::vec3(0, -1, 0);
        else if (value == "move-stop") scriptedMovement = glm::vec3(0);
        std::cout << "Control: " << value << " | paused=" << scene->IsPaused()
                  << " | forces=" << scene->GetSimulation().forceMask
                  << " | culling=" << cullingName(scene->GetSimulation().cullingMask)
                  << " | lod=" << scene->GetSimulation().lodControls.x << " | lodColors=" << scene->GetSimulation().lodControls.y
                  << " | contact=" << scene->GetSimulation().contactControls.x << " | sphereMotion=" << scene->GetSimulation().contactControls.z << std::endl;
    }

    Options parseOptions(int argc, char** argv) {
        Options options;
        for (int i = 1; i < argc; ++i) {
            std::string argument(argv[i]);
            auto value = [&]() {
                if (i + 1 >= argc) throw std::runtime_error("Missing value for " + argument);
                return std::string(argv[++i]);
            };
            if (argument == "--scene") {
                options.scene = value();
                if (options.scene == "single") options.preset = GrassPreset::Single;
                else if (options.scene == "bent") options.preset = GrassPreset::Bent;
                else if (options.scene == "patch") options.preset = GrassPreset::Patch;
                else if (options.scene == "field") options.preset = GrassPreset::Field;
                else throw std::runtime_error("Scene must be single, bent, patch, or field");
            } else if (argument == "--frames") {
                options.frames = positiveInteger(value());
            } else if (argument == "--yaw") {
                const std::string text = value();
                size_t end = 0;
                options.yaw = std::stof(text, &end);
                if (end != text.size() || !std::isfinite(options.yaw)) throw std::runtime_error("Yaw must be finite");
                options.overrideYaw = true;
            } else if (argument == "--wireframe") {
                options.wireframe = true;
            } else if (argument == "--pitch") {
                options.pitch = static_cast<float>(boundedNumber(value(), -89, 89));
                options.overridePitch = true;
            } else if (argument == "--radius") {
                options.radius = static_cast<float>(boundedNumber(value(), 1, 50));
                options.overrideRadius = true;
            } else if (argument == "--view-turn") {
                options.viewTurn = static_cast<float>(boundedNumber(value(), -360, 360));
            } else if (argument == "--orbit-step") {
                options.orbitStep = static_cast<float>(boundedNumber(value(), -10, 10));
            } else if (argument == "--zoom-step") {
                options.zoomStep = static_cast<float>(boundedNumber(value(), -.25, .25));
            } else if (argument == "--blades") {
                options.fieldCount = positiveInteger(value());
                if (options.fieldCount > 1048576) throw std::runtime_error("Blade limit is 1048576");
            } else if (argument == "--culling") {
                const std::string mode = value();
                if (mode == "none") options.culling = 0;
                else if (mode == "frustum") options.culling = FrustumCulling;
                else if (mode == "orientation") options.culling = OrientationCulling;
                else if (mode == "distance") options.culling = DistanceCulling;
                else if (mode == "all") options.culling = AllCulling;
                else if (mode == "compact") options.culling = 8; // all-pass atomic compaction probe
                else throw std::runtime_error("Culling must be none, frustum, orientation, distance, all, or compact");
                options.explicitCulling = true;
            } else if (argument == "--distance-buckets") {
                options.buckets = positiveInteger(value());
                if (options.buckets > 64) throw std::runtime_error("Distance bucket limit is 64");
            } else if (argument == "--max-distance") {
                options.maxDistance = static_cast<float>(boundedNumber(value(), .01, 10000));
            } else if (argument == "--orientation-threshold") {
                options.orientationThreshold = static_cast<float>(boundedNumber(value(), 0, 1));
            } else if (argument == "--frustum-tolerance") {
                options.frustumTolerance = static_cast<float>(boundedNumber(value(), 0, 1));
            } else if (argument == "--timings") {
                options.timings = value();
            } else if (argument == "--geometry-stats") {
                options.geometryStats = value();
            } else if (argument == "--lod") {
                const std::string mode = value();
                if (mode != "on" && mode != "off") throw std::runtime_error("LOD must be on or off");
                options.lod = mode == "on";
            } else if (argument == "--lod-colors") {
                options.lodColors = true;
            } else if (argument == "--lod-near") {
                options.lodNear = static_cast<float>(boundedNumber(value(), 0, 10000));
            } else if (argument == "--lod-far") {
                options.lodFar = static_cast<float>(boundedNumber(value(), 0, 10000));
            } else if (argument == "--lod-max") {
                options.lodMax = static_cast<float>(boundedNumber(value(), 2, 64));
            } else if (argument == "--lod-min") {
                options.lodMin = static_cast<float>(boundedNumber(value(), 2, 64));
            } else if (argument == "--interactive") {
                options.interaction = options.sphereVisible = true;
            } else if (argument == "--interaction" || argument == "--sphere") {
                const std::string mode = value();
                if (mode != "on" && mode != "off") throw std::runtime_error("Interaction/sphere must be on or off");
                (argument == "--interaction" ? options.interaction : options.sphereVisible) = mode == "on";
            } else if (argument == "--sphere-x") {
                options.spherePosition.x = static_cast<float>(boundedNumber(value(), -30, 30));
            } else if (argument == "--sphere-y") {
                options.spherePosition.y = static_cast<float>(boundedNumber(value(), 0, 8));
            } else if (argument == "--sphere-z") {
                options.spherePosition.z = static_cast<float>(boundedNumber(value(), -30, 30));
            } else if (argument == "--sphere-radius") {
                options.sphereRadius = static_cast<float>(boundedNumber(value(), .1, 3));
            } else if (argument == "--contact-strength") {
                options.contactStrength = static_cast<float>(boundedNumber(value(), 0, 120));
            } else if (argument == "--sphere-speed") {
                options.sphereSpeed = static_cast<float>(boundedNumber(value(), 0, 20));
            } else if (argument == "--sphere-travel") {
                options.sphereTravel = static_cast<float>(boundedNumber(value(), 0, 40));
            } else if (argument == "--sphere-motion") {
                const std::string motion = value();
                if (motion == "manual") options.sphereMotion = SphereMotion::Manual;
                else if (motion == "orbit") options.sphereMotion = SphereMotion::Orbit;
                else if (motion == "pass") options.sphereMotion = SphereMotion::Pass;
                else throw std::runtime_error("Sphere motion must be manual, orbit, or pass");
            } else if (argument == "--interaction-stats") {
                options.interactionStats = value();
            } else if (argument == "--simulation") {
                options.simulationName = value();
                if (options.simulationName == "static") options.simulation = SimulationMode::Static;
                else if (options.simulationName == "copy") options.simulation = SimulationMode::Copy;
                else if (options.simulationName == "physics") options.simulation = SimulationMode::Physics;
                else throw std::runtime_error("Simulation must be static, copy, or physics");
            } else if (argument == "--fixed-dt") {
                options.fixedDt = boundedNumber(value(), 1.0 / 10000.0, 8.0 / 120.0);
            } else if (argument == "--wind-strength") {
                options.wind = static_cast<float>(boundedNumber(value(), 0.0, 100.0));
            } else if (argument == "--gravity-strength") {
                options.gravity = static_cast<float>(boundedNumber(value(), 0.0, 100.0));
            } else if (argument == "--no-gravity") {
                options.forces &= ~Gravity;
            } else if (argument == "--no-recovery") {
                options.forces &= ~Recovery;
            } else if (argument == "--no-wind") {
                options.forces &= ~Wind;
            } else if (argument == "--paused") {
                options.paused = true;
            } else if (argument == "--diagnostics") {
                options.diagnostics = value();
            } else if (argument == "--dump-state") {
                options.dumpState = value();
            } else if (argument == "--sample-every") {
                options.sampleEvery = positiveInteger(value());
            } else if (argument == "--events") {
                std::stringstream input(value());
                std::string event;
                while (std::getline(input, event, ',')) {
                    const size_t separator = event.find(':');
                    if (separator == std::string::npos || !isControl(event.substr(separator + 1)))
                        throw std::runtime_error("Unknown frame:action event; see --help");
                    options.events.emplace_back(positiveInteger(event.substr(0, separator)), event.substr(separator + 1));
                }
                std::stable_sort(options.events.begin(), options.events.end(),
                    [](const std::pair<uint32_t, std::string>& a, const std::pair<uint32_t, std::string>& b) { return a.first < b.first; });
            } else if (argument == "--help") {
                options.help = true;
            } else {
                throw std::runtime_error("Unknown option: " + argument);
            }
        }
        if (options.lodNear >= options.lodFar || options.lodMin > options.lodMax)
            throw std::runtime_error("LOD requires near < far and min <= max");
        if (options.spherePosition.y < options.sphereRadius)
            throw std::runtime_error("Sphere centre height must be at least its radius");
        if (options.interaction && options.simulation != SimulationMode::Physics)
            throw std::runtime_error("Interaction requires --simulation physics");
        if (options.simulation == SimulationMode::Static) {
            if (options.explicitCulling && options.culling != 0)
                throw std::runtime_error("Static bypasses compute. Use --simulation copy for culling without physics.");
            options.culling = 0;
        }
        return options;
    }

    bool framebufferResized = false;

    void resizeCallback(GLFWwindow* window, int width, int height) {
        framebufferResized = true;
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

            previousX = xPosition;
            previousY = yPosition;
        } else if (rightMouseDown) {
            float deltaZ = static_cast<float>((previousY - yPosition) * 0.05);

            camera->UpdateOrbit(0.0f, 0.0f, deltaZ);

            previousY = yPosition;
        }
    }
}

int main(int argc, char** argv) try {
    const Options options = parseOptions(argc, argv);
    if (options.help) {
        std::cout << "Grass: --scene single|bent|patch|field [--wireframe] [--yaw degrees] [--frames count]\n"
                  << "--simulation static|copy|physics (default physics), --fixed-dt seconds (max 1/15)\n"
                  << "--no-gravity --no-recovery --no-wind --wind-strength 0..100 --gravity-strength 0..100 --paused\n"
                  << "--diagnostics path.csv --sample-every N --dump-state path-prefix\n"
                  << "--culling none|frustum|orientation|distance|all|compact (default all; static uses none)\n"
                  << "--max-distance 60 --distance-buckets 4 --orientation-threshold 0.9 --frustum-tolerance 0.01\n"
                  << "--pitch degrees --radius 1..50 --view-turn degrees --blades count (field only)\n"
                  << "--orbit-step degrees/frame --zoom-step units/frame (reproducible camera motion)\n"
                  << "--timings path.csv (GPU timestamps; use Release without validation for measurements)\n"
                  << "--lod on|off (default on), --lod-near 15 --lod-far 45 --lod-max 8 --lod-min 2\n"
                  << "LOD levels: 2..64; near < far; --lod-colors (red=high, blue=low)\n"
                  << "--geometry-stats path.csv (optional pipeline statistics; separate from timing runs)\n"
                  << "--events frame:action,... (1-based): pause,resume,step,reset,gravity,recovery,wind,culling,\n"
                  << "cull-none,cull-frustum,cull-orientation,cull-distance,cull-all,look-away,look-home,lod,lod-on,lod-off,lod-colors\n"
                  << "--interactive enables a visible sphere and grass interaction (default off for baseline experiments)\n"
                  << "--interaction on|off --sphere on|off --sphere-x -4 --sphere-y 1.2 --sphere-z 0 --sphere-radius 1.1\n"
                  << "--contact-strength 0..120 --sphere-speed 0..20 --sphere-motion manual|orbit|pass --sphere-travel 14\n"
                  << "--interaction-stats path.csv; contact-on/off, sphere-orbit/pass/manual/home/away and move-... events\n"
                  << "Interactive mode: arrows move X/Z; Q/E lower/raise; I contact; P automatic orbit; T sphere home.\n"
                  << "Left drag: orbit. Right drag/wheel: zoom. Space: pause. N: step. R: reset grass and sphere.\n"
                  << "G: gravity. B: recovery. W: wind. C: cycle culling. L: LOD. H: LOD colors. Escape: exit.\n";
        return 0;
    }
    const std::string applicationName = "Vulkan Grass Rendering - " + options.scene + (options.wireframe ? " (wireframe)" : "");
    InitializeWindow(1280, 720, applicationName.c_str());

    unsigned int glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    Instance* instance = new Instance(applicationName.c_str(), glfwExtensionCount, glfwExtensions);

    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(instance->GetVkInstance(), GetGLFWWindow(), nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create window surface");
    }

    instance->PickPhysicalDevice({ VK_KHR_SWAPCHAIN_EXTENSION_NAME }, QueueFlagBit::GraphicsBit | QueueFlagBit::TransferBit | QueueFlagBit::ComputeBit | QueueFlagBit::PresentBit, surface);

    VkPhysicalDeviceFeatures deviceFeatures = {};
    deviceFeatures.tessellationShader = VK_TRUE;
    deviceFeatures.fillModeNonSolid = VK_TRUE;
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    if (!options.geometryStats.empty()) {
        VkPhysicalDeviceFeatures supported = {};
        vkGetPhysicalDeviceFeatures(instance->GetPhysicalDevice(), &supported);
        if (!supported.pipelineStatisticsQuery) throw std::runtime_error("Geometry statistics are not supported; omit --geometry-stats to use LOD normally");
        deviceFeatures.pipelineStatisticsQuery = VK_TRUE;
    }

    device = instance->CreateDevice(QueueFlagBit::GraphicsBit | QueueFlagBit::TransferBit | QueueFlagBit::ComputeBit | QueueFlagBit::PresentBit, deviceFeatures);

    swapChain = device->CreateSwapChain(surface, 5);

    camera = new Camera(device, 1280.f / 720.f);
    float yaw = options.preset == GrassPreset::Field || options.preset == GrassPreset::Patch ? 25.0f : 0.0f;
    float pitch = options.preset == GrassPreset::Field ? -25.0f : (options.preset == GrassPreset::Patch ? -18.0f : -5.0f);
    float radius = options.preset == GrassPreset::Field ? 23.0f : (options.preset == GrassPreset::Patch ? 10.0f : 4.0f);
    if (options.overrideYaw) yaw = options.yaw;
    if (options.overridePitch) pitch = options.pitch;
    if (options.overrideRadius) radius = options.radius;
    camera->SetOrbit(yaw, pitch, radius);
    camera->SetViewTurn(options.viewTurn);

    VkCommandPoolCreateInfo transferPoolInfo = {};
    transferPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    // The upload helpers submit their command buffers to the graphics queue.
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
    
    Blades* blades = new Blades(device, transferCommandPool, planeDim, options.preset, options.fieldCount);
    Model* sphere = options.sphereVisible || options.interaction
        ? CreateInteractionSphere(device, transferCommandPool, grassImage) : nullptr;

    vkDestroyCommandPool(device->GetVkDevice(), transferCommandPool, nullptr);

    scene = new Scene(device, options.simulation);
    scene->Configure(options.fixedDt, options.forces, options.wind, options.gravity, options.paused);
    scene->ConfigureCulling(options.culling, options.buckets, options.orientationThreshold, options.maxDistance, options.frustumTolerance);
    scene->ConfigureLod(options.lod, options.lodColors, options.lodNear, options.lodFar, options.lodMax, options.lodMin);
    scene->AddModel(plane);
    scene->AddBlades(blades);
    if (sphere) scene->AddModel(sphere);
    scene->ConfigureInteraction(sphere, options.interaction, options.sphereVisible, options.spherePosition,
        options.sphereRadius, options.contactStrength, options.sphereSpeed, options.sphereTravel, options.sphereMotion);

    renderer = new Renderer(device, swapChain, scene, camera, options.wireframe, !options.timings.empty(), !options.geometryStats.empty());
    VkPhysicalDeviceProperties gpuProperties;
    vkGetPhysicalDeviceProperties(instance->GetPhysicalDevice(), &gpuProperties);
    std::cout << "Stage 6 interactive grass | scene=" << options.scene << " | blades=" << blades->GetBladeCount()
              << " | simulation=" << options.simulationName << " | fixedDt=" << options.fixedDt
              << " | gravity=" << options.gravity << " | wind=" << options.wind << " | forces=" << options.forces
              << " | culling=" << cullingName(options.culling) << " | maxDistance=" << options.maxDistance << " | buckets=" << options.buckets
              << " | lod=" << (options.lod ? "on" : "off") << " | lodNear=" << options.lodNear << " | lodFar=" << options.lodFar
              << " | lodMax=" << options.lodMax << " | lodMin=" << options.lodMin << " | lodColors=" << options.lodColors
              << " | contact=" << options.interaction << " | sphere=" << options.sphereVisible
              << " | sphereRadius=" << options.sphereRadius << " | contactStrength=" << options.contactStrength
              << " | wireframe=" << (options.wireframe ? "on" : "off") << " | GPU=" << gpuProperties.deviceName << std::endl;

    glfwSetFramebufferSizeCallback(GetGLFWWindow(), resizeCallback);
    glfwSetMouseButtonCallback(GetGLFWWindow(), mouseDownCallback);
    glfwSetCursorPosCallback(GetGLFWWindow(), mouseMoveCallback);
    glfwSetScrollCallback(GetGLFWWindow(), [](GLFWwindow*, double, double y) {
        camera->UpdateOrbit(0.0f, 0.0f, static_cast<float>(y) * 0.75f);
    });
    glfwSetKeyCallback(GetGLFWWindow(), [](GLFWwindow* window, int key, int, int action, int) {
        if (action != GLFW_PRESS) return;
        if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(window, GLFW_TRUE);
        else if (key == GLFW_KEY_SPACE) applyControl(scene->IsPaused() ? "resume" : "pause");
        else if (key == GLFW_KEY_N) applyControl("step");
        else if (key == GLFW_KEY_R) applyControl("reset");
        else if (key == GLFW_KEY_G) applyControl("gravity");
        else if (key == GLFW_KEY_B) applyControl("recovery");
        else if (key == GLFW_KEY_W) applyControl("wind");
        else if (key == GLFW_KEY_C) applyControl("culling");
        else if (key == GLFW_KEY_L) applyControl("lod");
        else if (key == GLFW_KEY_H) applyControl("lod-colors");
        else if (key == GLFW_KEY_I && scene->HasSphere()) applyControl("interaction");
        else if (key == GLFW_KEY_P && scene->HasSphere()) applyControl("sphere-auto");
        else if (key == GLFW_KEY_T && scene->HasSphere()) applyControl("sphere-home");
    });

    uint32_t renderedFrames = 0;
    size_t nextEvent = 0;
    std::string lastTitle;
    GrassDiagnostics diagnostics(options.diagnostics);
    std::ofstream timingCsv;
    std::ofstream geometryCsv;
    std::ofstream interactionCsv;
    if (!options.interactionStats.empty()) {
        interactionCsv.open(options.interactionStats);
        if (!interactionCsv) throw std::runtime_error("Cannot open interaction statistics CSV");
        interactionCsv << "frame,simulation_steps,substeps,enabled,visible,motion,radius,strength";
        for (unsigned i = 0; i < 9; ++i) interactionCsv << ",x" << i << ",y" << i << ",z" << i;
        interactionCsv << '\n' << std::setprecision(10);
    }
    if (!options.geometryStats.empty()) {
        geometryCsv.open(options.geometryStats);
        if (!geometryCsv) throw std::runtime_error("Cannot open geometry statistics CSV");
        geometryCsv << "frame,lod_enabled,lod_colors,lod_near,lod_far,lod_max,lod_min,camera_x,camera_y,camera_z,patches,tes_invocations\n" << std::setprecision(10);
    }
    if (!options.timings.empty()) {
        timingCsv.open(options.timings);
        if (!timingCsv) throw std::runtime_error("Cannot open GPU timing CSV");
        timingCsv << "frame,culling_mask,compute_ms,graphics_ms,gpu_ms\n" << std::setprecision(10);
    }
    scene->DiscardElapsedTime();
    while (!ShouldQuit()) {
        renderer->WaitForFrame();
        glfwPollEvents();
        if (ShouldQuit()) break;

        int width, height;
        glfwGetFramebufferSize(GetGLFWWindow(), &width, &height);
        if (width == 0 || height == 0) {
            glfwWaitEvents();
            scene->DiscardElapsedTime();
            continue;
        }

        if (framebufferResized) {
            swapChain->Recreate();
            renderer->RecreateFrameResources();
            framebufferResized = false;
            scene->DiscardElapsedTime();
        }

        while (nextEvent < options.events.size() && options.events[nextEvent].first == renderedFrames + 1)
            applyControl(options.events[nextEvent++].second);
        glm::vec3 direction = scriptedMovement;
        if (scene->HasSphere()) {
            const auto held = [&](int key) { return glfwGetKey(GetGLFWWindow(), key) == GLFW_PRESS ? 1.0f : 0.0f; };
            direction += glm::vec3(held(GLFW_KEY_RIGHT) - held(GLFW_KEY_LEFT), held(GLFW_KEY_E) - held(GLFW_KEY_Q),
                held(GLFW_KEY_DOWN) - held(GLFW_KEY_UP));
        }
        scene->SetMovement(direction);
        if (options.orbitStep != 0 || options.zoomStep != 0)
            camera->SetOrbit(yaw + renderedFrames * options.orbitStep, pitch,
                glm::clamp(radius + renderedFrames * options.zoomStep, 1.0f, 50.0f));
        const uint32_t forces = scene->GetSimulation().forceMask;
        const std::string title = applicationName + " | " + options.simulationName
            + (scene->IsPaused() ? " | PAUSED" : " | RUNNING")
            + " | G:" + ((forces & Gravity) ? "on" : "off")
            + " B:" + ((forces & Recovery) ? "on" : "off") + " W:" + ((forces & Wind) ? "on" : "off");
        const std::string cullingTitle = title + (scene->HasSphere() ? (scene->GetSimulation().contactControls.x ? " | I:contact ON" : " | I:contact OFF") : "")
            + " | Cull:" + cullingName(scene->GetSimulation().cullingMask)
            + " | LOD:" + (scene->GetSimulation().lodControls.x ? "on" : "off")
            + (scene->GetSimulation().lodControls.y ? " (colors)" : "") + " | L:LOD H:colors";
        if (cullingTitle != lastTitle) { glfwSetWindowTitle(GetGLFWWindow(), cullingTitle.c_str()); lastTitle = cullingTitle; }
        if (!renderer->Frame()) continue;
        ++renderedFrames;
        if (interactionCsv.is_open()) {
            const auto& settings = scene->GetSimulation();
            interactionCsv << renderedFrames << ',' << scene->GetSimulationSteps() << ',' << settings.stepCount << ','
                << settings.contactControls.x << ',' << settings.contactControls.y << ',' << settings.contactControls.z << ','
                << settings.spherePath[0].w << ',' << settings.contactSettings.x;
            for (const auto& point : settings.spherePath) interactionCsv << ',' << point.x << ',' << point.y << ',' << point.z;
            interactionCsv << '\n';
            if (!interactionCsv) throw std::runtime_error("Failed to write interaction statistics");
        }
        if (geometryCsv.is_open()) {
            const auto geometry = renderer->ReadGpuGeometry();
            const auto& settings = scene->GetSimulation();
            const auto& position = camera->GetUniforms().position;
            geometryCsv << renderedFrames << ',' << settings.lodControls.x << ',' << settings.lodControls.y << ','
                << settings.lodDistances.x << ',' << settings.lodDistances.y << ',' << settings.lodDistances.z << ',' << settings.lodDistances.w << ','
                << position.x << ',' << position.y << ',' << position.z << ',' << geometry[0] << ',' << geometry[1] << '\n';
            if (!geometryCsv) throw std::runtime_error("Failed to write geometry statistics");
        }
        if (timingCsv.is_open()) {
            const auto times = renderer->ReadGpuTimings();
            timingCsv << renderedFrames << ',' << scene->GetSimulation().cullingMask << ','
                << times[0] << ',' << times[1] << ',' << times[0] + times[1] << '\n';
            if (!timingCsv) throw std::runtime_error("Failed to write GPU timings");
        }
        if (diagnostics.Enabled() && (renderedFrames == 1 || renderedFrames % options.sampleEvery == 0 || renderedFrames == options.frames))
            diagnostics.Record(renderedFrames, *scene, *blades, renderer->Readback(*blades));
        if (options.frames != 0 && renderedFrames >= options.frames) break;
    }

    vkDeviceWaitIdle(device->GetVkDevice());
    if (!options.dumpState.empty() && renderedFrames > 0)
        GrassDiagnostics::Dump(options.dumpState, *blades, renderer->Readback(*blades));
    std::cout << "Completed frames=" << renderedFrames << " | simulationSteps=" << scene->GetSimulationSteps()
              << " | simulationTime=" << scene->GetSimulationTime() << std::endl;

    delete renderer;
    delete scene;
    delete plane;
    delete sphere;
    delete blades;
    delete camera;
    vkDestroyImage(device->GetVkDevice(), grassImage, nullptr);
    vkFreeMemory(device->GetVkDevice(), grassImageMemory, nullptr);
    delete swapChain;
    delete device;
    vkDestroySurfaceKHR(instance->GetVkInstance(), surface, nullptr);
    delete instance;
    DestroyWindow();
    return 0;
} catch (const std::exception& error) {
    std::cerr << "Error: " << error.what() << std::endl;
    return 1;
}
