#include "Scene.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace {

constexpr uint32_t kMaximumBladeCount = 16'777'216u;
constexpr uint32_t kMaximumDimension = 16'384u;
constexpr uint32_t kBaselineCullMask = CullOrientation | CullFrustum | CullDistance;
constexpr uint32_t kKnownCullMask = kBaselineCullMask | CullOcclusion;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kPlacementTau = 0.01f;
constexpr float kPlacementEpsilon = 0.01f;
constexpr float kPlacementSafety = 0.95f;
constexpr float kMinimumBladeHeight = 0.08f;
constexpr uint32_t kShadowResolution = 2048u;

struct PresetDefinition {
    uint32_t culling;
    bool lod;
    bool occlusion;
    uint32_t features;
};

PresetDefinition DefinitionForPreset(const std::string& name) {
    if (name == "basic") {
        return {0u, false, false, 0u};
    }
    if (name == "forces") {
        return {0u, false, false, SimulateForces};
    }
    if (name == "culling") {
        return {kBaselineCullMask, false, false, SimulateForces};
    }
    if (name == "lod") {
        return {kBaselineCullMask, true, false, SimulateForces};
    }
    if (name == "occlusion") {
        return {kBaselineCullMask, true, true, SimulateForces};
    }
    if (name == "interaction") {
        return {kBaselineCullMask, true, true, SimulateForces | InteractiveSphere};
    }
    if (name == "final") {
        return {kBaselineCullMask, true, true,
                SimulateForces | InteractiveSphere | BetterShading};
    }
    if (name == "wireframe") {
        return {kBaselineCullMask, true, true,
                SimulateForces | InteractiveSphere | BetterShading | Wireframe};
    }
    throw std::invalid_argument("invalid preset '" + name + "'");
}

bool IsCameraPreset(const std::string& name) {
    return name == "auto" || name == "overview" || name == "grazing" ||
           name == "occluder" || name == "interaction" || name == "detail" ||
           name == "beauty";
}

std::string ResolveCameraPreset(const std::string& camera, const std::string& preset) {
    if (camera != "auto") {
        return camera;
    }
    if (preset == "final") {
        return "beauty";
    }
    if (preset == "interaction") {
        return "interaction";
    }
    return "overview";
}

float NormalizeDegrees(float degrees) {
    float normalized = std::fmod(degrees, 360.0f);
    if (normalized < 0.0f) {
        normalized += 360.0f;
    }
    return normalized;
}

uint32_t MixBits(uint32_t value) {
    const uint32_t rotated = (value << 7u) | (value >> 25u);
    value = (value + 0x9d6c4a13u) ^ (rotated * 0x5f31e927u);
    value ^= value >> 13u;
    value *= 0x6b2d0495u;
    return value ^ (value << 11u) ^ (value >> 17u);
}

uint32_t CoordinateHash(int32_t x, int32_t z, uint32_t seed) {
    uint32_t value = seed ^ 0xa511e9b3u;
    value ^= MixBits(static_cast<uint32_t>(x) + 0x68bc21ebu);
    value = MixBits(value ^ (static_cast<uint32_t>(z) + 0x02e5be93u));
    return value;
}

float UnitFloat(uint32_t bits) {
    return static_cast<float>(bits >> 8u) * (1.0f / 16'777'216.0f);
}

class DeterministicRng {
public:
    explicit DeterministicRng(uint32_t seed) : engine_(seed) {}

    uint32_t NextU32() {
        return engine_();
    }

    float NextUnit() {
        return UnitFloat(NextU32());
    }

private:
    std::mt19937 engine_;
};

float Cross2(const glm::vec2& origin, const glm::vec2& a, const glm::vec2& b) {
    const glm::vec2 first = a - origin;
    const glm::vec2 second = b - origin;
    return first.x * second.y - first.y * second.x;
}

std::vector<glm::vec2> ConvexHull(std::vector<glm::vec2> points) {
    std::sort(points.begin(), points.end(), [](const glm::vec2& a, const glm::vec2& b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    points.erase(std::unique(points.begin(), points.end(),
                             [](const glm::vec2& a, const glm::vec2& b) {
                                 return a.x == b.x && a.y == b.y;
                             }),
                 points.end());
    if (points.size() < 3u) {
        return points;
    }

    std::vector<glm::vec2> hull;
    hull.reserve(points.size() * 2u);
    for (const glm::vec2& point : points) {
        while (hull.size() >= 2u &&
               Cross2(hull[hull.size() - 2u], hull.back(), point) <= 0.0f) {
            hull.pop_back();
        }
        hull.push_back(point);
    }
    const std::size_t lowerSize = hull.size();
    for (std::size_t i = points.size() - 1u; i-- > 0u;) {
        const glm::vec2& point = points[i];
        while (hull.size() > lowerSize &&
               Cross2(hull[hull.size() - 2u], hull.back(), point) <= 0.0f) {
            hull.pop_back();
        }
        hull.push_back(point);
    }
    hull.pop_back();
    return hull;
}

float PointSegmentDistance(const glm::vec2& point,
                           const glm::vec2& a,
                           const glm::vec2& b) {
    const glm::vec2 edge = b - a;
    const float edgeLengthSquared = glm::dot(edge, edge);
    if (edgeLengthSquared <= 0.0f) {
        return glm::length(point - a);
    }
    const float fraction = glm::clamp(glm::dot(point - a, edge) / edgeLengthSquared,
                                      0.0f, 1.0f);
    return glm::length(point - (a + fraction * edge));
}

float PointHullDistance(const glm::vec2& point, const std::vector<glm::vec2>& hull) {
    if (hull.empty()) {
        return std::numeric_limits<float>::infinity();
    }
    if (hull.size() == 1u) {
        return glm::length(point - hull.front());
    }

    bool inside = hull.size() >= 3u;
    float minimum = std::numeric_limits<float>::infinity();
    for (std::size_t i = 0; i < hull.size(); ++i) {
        const glm::vec2& a = hull[i];
        const glm::vec2& b = hull[(i + 1u) % hull.size()];
        const float edgeScale = 1.0f + glm::length(b - a);
        if (Cross2(a, b, point) < -1.0e-6f * edgeScale) {
            inside = false;
        }
        minimum = std::min(minimum, PointSegmentDistance(point, a, b));
    }
    if (inside || minimum <= 1.0e-6f) {
        return 0.0f;
    }
    return minimum;
}

float AabbDistanceSquared(const glm::vec2& point,
                          const glm::vec2& minimum,
                          const glm::vec2& maximum) {
    const float dx = point.x < minimum.x ? minimum.x - point.x
                    : point.x > maximum.x ? point.x - maximum.x
                                              : 0.0f;
    const float dz = point.y < minimum.y ? minimum.y - point.y
                    : point.y > maximum.y ? point.y - maximum.y
                                              : 0.0f;
    return dx * dx + dz * dz;
}

glm::vec3 ClampColor(const glm::vec3& color) {
    return glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
}

glm::vec3 StoneFaceColor(const glm::vec3& base, uint32_t bits) {
    const float shade = 0.82f + 0.28f * UnitFloat(bits);
    const float warmth = UnitFloat(MixBits(bits ^ 0x91e10da5u)) - 0.5f;
    return ClampColor(base * shade + glm::vec3(0.025f * warmth, 0.014f * warmth,
                                               -0.018f * warmth));
}

void AppendFlatTriangle(std::vector<SceneVertex>& vertices,
                        std::vector<uint32_t>& indices,
                        glm::vec3 a,
                        glm::vec3 b,
                        glm::vec3 c,
                        const glm::vec3& color,
                        const glm::vec3& interior) {
    glm::vec3 normal = glm::cross(b - a, c - a);
    const glm::vec3 centroid = (a + b + c) / 3.0f;
    if (glm::dot(normal, centroid - interior) < 0.0f) {
        std::swap(b, c);
        normal = -normal;
    }

    const float lengthSquared = glm::dot(normal, normal);
    if (lengthSquared <= 1.0e-12f) {
        return;
    }
    normal *= 1.0f / std::sqrt(lengthSquared);

    const uint32_t first = static_cast<uint32_t>(vertices.size());
    vertices.push_back({a, normal, color});
    vertices.push_back({b, normal, color});
    vertices.push_back({c, normal, color});
    indices.push_back(first);
    indices.push_back(first + 1u);
    indices.push_back(first + 2u);
}

void AppendFacetedRock(std::vector<SceneVertex>& vertices,
                       std::vector<uint32_t>& indices,
                       const glm::vec3& center,
                       const glm::vec3& scale,
                       float rotation,
                       uint32_t seed,
                       const glm::vec3& baseColor,
                       uint32_t segments) {
    std::vector<glm::vec3> lower(segments);
    std::vector<glm::vec3> middle(segments);
    std::vector<glm::vec3> upper(segments);

    for (uint32_t i = 0; i < segments; ++i) {
        const uint32_t bits = MixBits(seed + i * 0x9e3779b9u);
        const float angle = rotation + kTwoPi * static_cast<float>(i) /
                                           static_cast<float>(segments);
        const float radial = 0.86f + 0.25f * UnitFloat(bits);
        const float middleRadial = radial * (0.96f + 0.10f * UnitFloat(MixBits(bits)));
        const float upperRadial = radial * (0.69f + 0.10f * UnitFloat(MixBits(bits ^ 0x51edu)));
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);

        lower[i] = center + glm::vec3(cosine * scale.x * radial * 0.76f,
                                      0.018f,
                                      sine * scale.z * radial * 0.76f);
        middle[i] = center + glm::vec3(cosine * scale.x * middleRadial,
                                       scale.y * (0.32f + 0.05f * UnitFloat(bits ^ 0xabc98388u)),
                                       sine * scale.z * middleRadial);
        upper[i] = center + glm::vec3(cosine * scale.x * upperRadial,
                                      scale.y * (0.73f + 0.07f * UnitFloat(bits ^ 0x7f4a7c15u)),
                                      sine * scale.z * upperRadial);
    }

    const uint32_t topBits = MixBits(seed ^ 0xd1b54a35u);
    const glm::vec3 top = center + glm::vec3(
        (UnitFloat(topBits) - 0.5f) * scale.x * 0.24f,
        scale.y,
        (UnitFloat(MixBits(topBits)) - 0.5f) * scale.z * 0.24f);
    const glm::vec3 bottom = center + glm::vec3(0.0f, 0.018f, 0.0f);
    const glm::vec3 interior = center + glm::vec3(0.0f, scale.y * 0.44f, 0.0f);

    uint32_t face = 0u;
    const auto color = [&](uint32_t faceIndex) {
        return StoneFaceColor(baseColor, MixBits(seed + faceIndex * 0x45d9f3bu));
    };

    for (uint32_t i = 0; i < segments; ++i) {
        const uint32_t next = (i + 1u) % segments;
        AppendFlatTriangle(vertices, indices, lower[i], middle[i], middle[next],
                           color(face++), interior);
        AppendFlatTriangle(vertices, indices, lower[i], middle[next], lower[next],
                           color(face++), interior);
        AppendFlatTriangle(vertices, indices, middle[i], upper[i], upper[next],
                           color(face++), interior);
        AppendFlatTriangle(vertices, indices, middle[i], upper[next], middle[next],
                           color(face++), interior);
        AppendFlatTriangle(vertices, indices, upper[i], top, upper[next],
                           color(face++), interior);
        AppendFlatTriangle(vertices, indices, lower[i], lower[next], bottom,
                           color(face++), interior);
    }
}

void AppendFacetedBlock(std::vector<SceneVertex>& vertices,
                        std::vector<uint32_t>& indices,
                        const glm::vec3& center,
                        const glm::vec3& halfExtent,
                        float rotation,
                        const glm::vec2& topShift,
                        uint32_t seed,
                        const glm::vec3& baseColor) {
    const float cosine = std::cos(rotation);
    const float sine = std::sin(rotation);
    const auto transform = [&](const glm::vec3& local) {
        return center + glm::vec3(cosine * local.x + sine * local.z,
                                  local.y,
                                  -sine * local.x + cosine * local.z);
    };

    const float x = halfExtent.x;
    const float z = halfExtent.z;
    const float height = halfExtent.y * 2.0f;
    std::array<glm::vec3, 4> bottom = {
        transform(glm::vec3(-x, 0.02f, -z)), transform(glm::vec3(-x, 0.02f, z)),
        transform(glm::vec3(x, 0.02f, z)), transform(glm::vec3(x, 0.02f, -z))};
    std::array<glm::vec3, 4> top = {
        transform(glm::vec3(-x + topShift.x, height, -z + topShift.y)),
        transform(glm::vec3(-x + topShift.x, height, z + topShift.y)),
        transform(glm::vec3(x + topShift.x, height, z + topShift.y)),
        transform(glm::vec3(x + topShift.x, height, -z + topShift.y))};
    const glm::vec3 interior = center + glm::vec3(0.0f, halfExtent.y, 0.0f);

    uint32_t face = 0u;
    const auto append = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
        AppendFlatTriangle(vertices, indices, a, b, c,
                           StoneFaceColor(baseColor, MixBits(seed + face++ * 0x27d4eb2du)),
                           interior);
    };

    for (uint32_t i = 0; i < 4u; ++i) {
        const uint32_t next = (i + 1u) % 4u;
        append(bottom[i], top[i], top[next]);
        append(bottom[i], top[next], bottom[next]);
    }
    append(top[0], top[1], top[2]);
    append(top[0], top[2], top[3]);
    append(bottom[0], bottom[3], bottom[2]);
    append(bottom[0], bottom[2], bottom[1]);
}

uint32_t ParseUnsigned(const std::string& text, const std::string& option) {
    if (text.empty() || text.front() == '-' || text.front() == '+') {
        throw std::invalid_argument(option + " expects an unsigned integer, got '" + text + "'");
    }

    uint64_t value = 0u;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto result = std::from_chars(begin, end, value, 10);
    if (result.ec != std::errc() || result.ptr != end ||
        value > std::numeric_limits<uint32_t>::max()) {
        throw std::invalid_argument(option + " expects a uint32 value, got '" + text + "'");
    }
    return static_cast<uint32_t>(value);
}

float ParseFiniteFloat(const std::string& text, const std::string& option) {
    if (text.empty() || std::any_of(text.begin(), text.end(), [](unsigned char character) {
            return std::isspace(character) != 0;
        })) {
        throw std::invalid_argument(option + " expects a finite number, got '" + text + "'");
    }

    errno = 0;
    char* parsedEnd = nullptr;
    const float value = std::strtof(text.c_str(), &parsedEnd);
    if (errno == ERANGE || parsedEnd == text.c_str() ||
        parsedEnd != text.c_str() + text.size() || !std::isfinite(value)) {
        throw std::invalid_argument(option + " expects a finite number, got '" + text + "'");
    }
    return value;
}

bool ParseOnOff(const std::string& text, const std::string& option) {
    if (text == "on") {
        return true;
    }
    if (text == "off") {
        return false;
    }
    throw std::invalid_argument(option + " expects 'on' or 'off', got '" + text + "'");
}

uint32_t RenderFeatureForName(const std::string& name) {
    if (name == "sky") return RenderFeatureSky;
    if (name == "root-color") return RenderFeatureRootColor;
    if (name == "rounded-normals") return RenderFeatureRoundedNormals;
    if (name == "transmission") return RenderFeatureTransmission;
    if (name == "specular") return RenderFeatureSpecular;
    if (name == "fog") return RenderFeatureFog;
    if (name == "shadows") return RenderFeatureShadows;
    throw std::invalid_argument("--render-disable contains unknown feature '" + name + "'");
}

void DisableRenderFeatures(const std::string& text, uint32_t& mask) {
    if (text.empty()) {
        throw std::invalid_argument("--render-disable requires a non-empty comma list");
    }
    std::size_t begin = 0u;
    while (begin <= text.size()) {
        const std::size_t comma = text.find(',', begin);
        const std::size_t end = comma == std::string::npos ? text.size() : comma;
        if (end == begin) {
            throw std::invalid_argument("--render-disable contains an empty component");
        }
        const std::string name = text.substr(begin, end - begin);
        mask &= ~RenderFeatureForName(name);
        if (comma == std::string::npos) {
            break;
        }
        begin = comma + 1u;
    }
}

std::string RequireValue(int argc, char** argv, int& index, const std::string& option) {
    if (index + 1 >= argc || argv[index + 1] == nullptr) {
        throw std::invalid_argument("missing value for " + option);
    }
    const std::string value(argv[index + 1]);
    if (value.rfind("--", 0u) == 0u) {
        throw std::invalid_argument("missing value for " + option);
    }
    ++index;
    return value;
}

} // namespace

Scene::Scene(const RunOptions& options)
    : options_(options) {
    if (options_.bladeCount > kMaximumBladeCount) {
        throw std::invalid_argument("blade count exceeds 16777216");
    }
    if (!std::isfinite(options_.fieldSize) || options_.fieldSize < 1.0f ||
        options_.fieldSize > 512.0f) {
        throw std::invalid_argument("field size must be between 1 and 512");
    }
    if (!std::isfinite(options_.maxDistance) || options_.maxDistance <= 0.0f) {
        throw std::invalid_argument("maximum distance must be finite and positive");
    }
    if (!std::isfinite(options_.fixedDelta) || options_.fixedDelta <= 0.0f ||
        options_.fixedDelta > 1.0f / 30.0f) {
        throw std::invalid_argument("fixed timestep must be greater than 0 and at most 1/30");
    }
    if (!std::isfinite(options_.windStrength) || options_.windStrength < 0.0f ||
        options_.windStrength > 18.0f) {
        throw std::invalid_argument("wind strength must be between 0 and 18");
    }
    if (!std::isfinite(options_.sunAzimuth)) {
        throw std::invalid_argument("sun azimuth must be finite");
    }
    if (!std::isfinite(options_.sunElevation) || options_.sunElevation < 1.0f ||
        options_.sunElevation > 89.0f) {
        throw std::invalid_argument("sun elevation must be between 1 and 89 degrees");
    }
    if (options_.sphereMotion != "manual" && options_.sphereMotion != "sweep") {
        throw std::invalid_argument("sphere motion must be 'manual' or 'sweep'");
    }
    if ((options_.renderFeatureMask & ~static_cast<uint32_t>(RenderFeatureAll)) != 0u) {
        throw std::invalid_argument("render feature mask contains unknown bits");
    }
    if (!options_.physicsProbePath.empty() && !options_.verify) {
        throw std::invalid_argument("physics probe requires verification to be enabled");
    }
    if (!IsCameraPreset(options_.camera)) {
        throw std::invalid_argument("invalid camera preset '" + options_.camera + "'");
    }

    const PresetDefinition preset = DefinitionForPreset(options_.preset);
    featureMask_ = preset.features;
    if (options_.interactionOverride.has_value()) {
        if (*options_.interactionOverride) {
            featureMask_ |= InteractiveSphere;
        } else {
            featureMask_ &= ~static_cast<uint32_t>(InteractiveSphere);
        }
    }
    if (options_.shadingOverride.has_value()) {
        if (*options_.shadingOverride) {
            featureMask_ |= BetterShading;
        } else {
            featureMask_ &= ~static_cast<uint32_t>(BetterShading);
        }
    }
    if (options_.benchmarkPath.empty() && options_.captureDir.empty()) {
        featureMask_ |= ShowOverlay;
    }
    cullMask_ = options_.culling & kBaselineCullMask;
    if (options_.occlusion) {
        cullMask_ |= CullOcclusion;
    }
    lodEnabled_ = options_.lod;
    windAngle_ = std::atan2(wind_.z, wind_.x);
    windStrength_ = options_.windStrength;
    wind_ = glm::vec3(std::cos(windAngle_) * windStrength_, 0.0f,
                      std::sin(windAngle_) * windStrength_);

    options_.sunAzimuth = NormalizeDegrees(options_.sunAzimuth);
    const float azimuth = options_.sunAzimuth * kPi / 180.0f;
    const float elevation = options_.sunElevation * kPi / 180.0f;
    const float horizontal = std::cos(elevation);
    sunDirection_ = glm::normalize(glm::vec3(horizontal * std::cos(azimuth),
                                             std::sin(elevation),
                                             horizontal * std::sin(azimuth)));

    options_.camera = ResolveCameraPreset(options_.camera, options_.preset);
    ConfigureCamera(options_.camera);
    GenerateGeometry();
    BuildLightMatrix();
    GenerateBlades();
}

void Scene::GenerateBlades() {
    const uint32_t count = options_.bladeCount;
    blades_.resize(count);
    if (count == 0u) {
        return;
    }

    uint32_t columns = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(count))));
    columns = std::max(columns, 1u);
    const uint32_t rows = (count + columns - 1u) / columns;
    const uint32_t cellCount = columns * rows;

    uint32_t step = 1u;
    if (cellCount > 1u) {
        step = MixBits(options_.seed ^ 0x51ed270bu) % cellCount;
        step |= 1u;
        while (step >= cellCount || std::gcd(step, cellCount) != 1u) {
            step += 2u;
            if (step >= cellCount) {
                step = 1u;
            }
        }
    }
    const uint32_t offset = cellCount == 1u ? 0u : MixBits(options_.seed) % cellCount;

    DeterministicRng random(options_.seed);
    const float halfField = options_.fieldSize * 0.5f;
    constexpr float clumpSize = 3.2f;

    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t cell = static_cast<uint32_t>(
            (static_cast<uint64_t>(offset) + static_cast<uint64_t>(i) * step) % cellCount);
        const uint32_t column = cell % columns;
        const uint32_t row = cell / columns;
        const float baseXJitter = random.NextUnit();
        const float baseZJitter = random.NextUnit();
        const float baseHeight = 0.6f + 0.6f * random.NextUnit();
        const float baseWidth = 0.025f + 0.035f * random.NextUnit();
        const float stiffness = 7.0f + 6.0f * random.NextUnit();

        const auto cellRoot = [&](float xJitter, float zJitter) {
            const float x = ((static_cast<float>(column) + xJitter) /
                                 static_cast<float>(columns) -
                             0.5f) * options_.fieldSize;
            const float z = ((static_cast<float>(row) + zJitter) /
                                 static_cast<float>(rows) -
                             0.5f) * options_.fieldSize;
            return glm::vec3(glm::clamp(x, -halfField, halfField), 0.0f,
                             glm::clamp(z, -halfField, halfField));
        };
        const auto globalRoot = [&](float xUnit, float zUnit) {
            return glm::vec3((xUnit - 0.5f) * options_.fieldSize, 0.0f,
                             (zUnit - 0.5f) * options_.fieldSize);
        };

        glm::vec3 root(0.0f);
        float height = 0.0f;
        float width = 0.0f;
        const auto accept = [&](const glm::vec3& candidate) {
            const float distance = StaticObstacleDistance(candidate);
            float scale = 1.0f;
            if (std::isfinite(distance)) {
                if (distance <= kPlacementEpsilon) {
                    return false;
                }
                // A quadratic curve is a convex combination of root/guide/tip.  The guide
                // stays above the root and corrected tip reach is <= 1.01h, so adding w/2
                // encloses the whole ribbon; the safety-scaled epsilon gap keeps it off stone.
                const float unscaledEnvelope =
                    (1.0f + kPlacementTau) * baseHeight + 0.5f * baseWidth;
                scale = std::min(1.0f, kPlacementSafety *
                                           (distance - kPlacementEpsilon) /
                                           unscaledEnvelope);
            }
            const float candidateHeight = scale * baseHeight;
            if (!std::isfinite(scale) || scale <= 0.0f ||
                candidateHeight < kMinimumBladeHeight) {
                return false;
            }
            root = candidate;
            height = candidateHeight;
            width = scale * baseWidth;
            return true;
        };

        bool placed = accept(cellRoot(baseXJitter, baseZJitter));
        if (!placed) {
            DeterministicRng replacement(MixBits(options_.seed ^ 0xd8b3a51fu ^ MixBits(i)));
            for (uint32_t attempt = 0u; !placed && attempt < 8u; ++attempt) {
                const float replacementX = replacement.NextUnit();
                const float replacementZ = replacement.NextUnit();
                placed = accept(cellRoot(replacementX, replacementZ));
            }
            for (uint32_t attempt = 0u; !placed && attempt < 256u; ++attempt) {
                const float replacementX = replacement.NextUnit();
                const float replacementZ = replacement.NextUnit();
                placed = accept(globalRoot(replacementX, replacementZ));
            }
        }
        if (!placed) {
            std::ostringstream message;
            message << "bounded grass placement failed for field size " << options_.fieldSize
                    << ", blade count " << count << ", blade index " << i
                    << " after 265 candidate positions";
            throw std::runtime_error(message.str());
        }

        const int32_t clumpX = static_cast<int32_t>(std::floor((root.x + halfField) / clumpSize));
        const int32_t clumpZ = static_cast<int32_t>(std::floor((root.z + halfField) / clumpSize));
        const uint32_t clumpHash = CoordinateHash(clumpX, clumpZ, options_.seed);
        const int32_t rootHashX = static_cast<int32_t>(std::floor(root.x * 128.0f));
        const int32_t rootHashZ = static_cast<int32_t>(std::floor(root.z * 128.0f));
        const uint32_t rootHash = CoordinateHash(rootHashX, rootHashZ, options_.seed ^ i);
        float lateralAngle = UnitFloat(clumpHash) * kTwoPi;
        lateralAngle += (UnitFloat(rootHash) - 0.5f) * 0.72f;
        if (lateralAngle < 0.0f) {
            lateralAngle += kTwoPi;
        } else if (lateralAngle >= kTwoPi) {
            lateralAngle -= kTwoPi;
        }

        const glm::vec3 tip = root + glm::vec3(0.0f, height, 0.0f);

        Blade& blade = blades_[i];
        blade.v0 = glm::vec4(root, lateralAngle);
        blade.v1 = glm::vec4(tip, height);
        blade.v2 = glm::vec4(tip, width);
        blade.up = glm::vec4(0.0f, 1.0f, 0.0f, stiffness);
    }
}

void Scene::GenerateGeometry() {
    vertices_.clear();
    indices_.clear();
    draws_.clear();
    staticFootprints_.clear();
    vertices_.reserve(2'500u);
    indices_.reserve(5'200u);
    draws_.reserve(3u);
    staticFootprints_.reserve(12u);

    const uint32_t groundFirst = static_cast<uint32_t>(indices_.size());
    constexpr uint32_t groundResolution = 16u;
    const float halfField = options_.fieldSize * 0.5f;
    for (uint32_t z = 0; z <= groundResolution; ++z) {
        for (uint32_t x = 0; x <= groundResolution; ++x) {
            const float xFraction = static_cast<float>(x) / static_cast<float>(groundResolution);
            const float zFraction = static_cast<float>(z) / static_cast<float>(groundResolution);
            const float variation = UnitFloat(CoordinateHash(static_cast<int32_t>(x),
                                                              static_cast<int32_t>(z),
                                                              options_.seed ^ 0x8f6a2c31u)) -
                                    0.5f;
            const glm::vec3 color = ClampColor(glm::vec3(0.185f, 0.285f, 0.125f) +
                                                glm::vec3(0.028f, 0.035f, 0.018f) * variation);
            vertices_.push_back({glm::vec3(-halfField + xFraction * options_.fieldSize,
                                           0.0f,
                                           -halfField + zFraction * options_.fieldSize),
                                 glm::vec3(0.0f, 1.0f, 0.0f), color});
        }
    }
    const uint32_t groundStride = groundResolution + 1u;
    for (uint32_t z = 0; z < groundResolution; ++z) {
        for (uint32_t x = 0; x < groundResolution; ++x) {
            const uint32_t a = z * groundStride + x;
            const uint32_t b = (z + 1u) * groundStride + x;
            const uint32_t c = (z + 1u) * groundStride + x + 1u;
            const uint32_t d = z * groundStride + x + 1u;
            indices_.insert(indices_.end(), {a, b, c, a, c, d});
        }
    }
    draws_.push_back({groundFirst, static_cast<uint32_t>(indices_.size()) - groundFirst, 0u, false});

    const uint32_t stoneFirst = static_cast<uint32_t>(indices_.size());
    const float layoutScale = std::min(1.0f, options_.fieldSize / 64.0f);
    const float layoutSpan = options_.fieldSize < 64.0f ? 64.0f : options_.fieldSize;
    const auto at = [&](float xFraction, float zFraction, float offsetX, float offsetZ) {
        return glm::vec3(xFraction * layoutSpan + offsetX, 0.0f,
                         zFraction * layoutSpan + offsetZ);
    };
    const auto scaleAndRecord = [&](std::size_t firstVertex) {
        if (layoutScale < 1.0f) {
            for (std::size_t i = firstVertex; i < vertices_.size(); ++i) {
                vertices_[i].position *= layoutScale;
            }
        }
        RecordStaticFootprint(firstVertex);
    };
    const auto appendRock = [&](const glm::vec3& center, const glm::vec3& scale,
                                float rotation, uint32_t seed, const glm::vec3& color,
                                uint32_t segments) {
        const std::size_t firstVertex = vertices_.size();
        AppendFacetedRock(vertices_, indices_, center, scale, rotation, seed, color, segments);
        scaleAndRecord(firstVertex);
    };
    const auto appendBlock = [&](const glm::vec3& center, const glm::vec3& halfExtent,
                                 float rotation, const glm::vec2& topShift, uint32_t seed,
                                 const glm::vec3& color) {
        const std::size_t firstVertex = vertices_.size();
        AppendFacetedBlock(vertices_, indices_, center, halfExtent, rotation, topShift,
                           seed, color);
        scaleAndRecord(firstVertex);
    };

    appendRock(at(0.15f, 0.13f, 0.0f, 0.0f), glm::vec3(2.15f, 3.25f, 1.75f),
               0.15f, options_.seed ^ 0x1001u, glm::vec3(0.40f, 0.38f, 0.34f), 8u);
    appendRock(at(0.15f, 0.13f, -2.3f, 0.7f), glm::vec3(1.20f, 1.45f, 1.35f),
               -0.4f, options_.seed ^ 0x1002u, glm::vec3(0.45f, 0.42f, 0.36f), 7u);
    appendRock(at(0.15f, 0.13f, 2.2f, -0.5f), glm::vec3(1.00f, 1.20f, 0.90f),
               0.7f, options_.seed ^ 0x1003u, glm::vec3(0.36f, 0.37f, 0.35f), 7u);

    appendRock(at(-0.18f, -0.03f, 0.0f, 0.0f), glm::vec3(1.75f, 2.15f, 1.45f),
               0.55f, options_.seed ^ 0x2001u, glm::vec3(0.43f, 0.39f, 0.32f), 8u);
    appendRock(at(-0.18f, -0.03f, 2.0f, 0.4f), glm::vec3(1.05f, 1.05f, 1.20f),
               -0.2f, options_.seed ^ 0x2002u, glm::vec3(0.48f, 0.45f, 0.38f), 7u);
    appendRock(at(-0.18f, -0.03f, -1.7f, -0.5f), glm::vec3(0.82f, 0.78f, 0.95f),
               0.9f, options_.seed ^ 0x2003u, glm::vec3(0.38f, 0.40f, 0.34f), 7u);

    appendRock(at(0.18f, -0.24f, 0.0f, 0.0f), glm::vec3(1.90f, 2.55f, 1.65f),
               -0.25f, options_.seed ^ 0x3001u, glm::vec3(0.34f, 0.36f, 0.38f), 8u);
    appendRock(at(0.18f, -0.24f, -2.0f, 0.8f), glm::vec3(1.15f, 1.10f, 1.00f),
               0.4f, options_.seed ^ 0x3002u, glm::vec3(0.39f, 0.40f, 0.39f), 7u);
    appendRock(at(0.18f, -0.24f, 1.8f, 0.6f), glm::vec3(0.90f, 0.82f, 1.10f),
               0.1f, options_.seed ^ 0x3003u, glm::vec3(0.42f, 0.39f, 0.34f), 7u);

    appendBlock(at(-0.24f, 0.22f, 0.0f, 0.0f), glm::vec3(1.35f, 1.65f, 0.72f),
                0.32f, glm::vec2(0.14f, -0.08f), options_.seed ^ 0x4001u,
                glm::vec3(0.37f, 0.39f, 0.36f));
    appendBlock(at(-0.24f, 0.22f, 2.1f, 0.25f), glm::vec3(0.92f, 1.12f, 0.62f),
                -0.28f, glm::vec2(-0.08f, 0.06f), options_.seed ^ 0x4002u,
                glm::vec3(0.44f, 0.42f, 0.36f));
    appendBlock(at(-0.24f, 0.22f, 0.7f, -1.45f), glm::vec3(1.75f, 0.42f, 0.82f),
                0.08f, glm::vec2(0.04f, 0.02f), options_.seed ^ 0x4003u,
                glm::vec3(0.35f, 0.36f, 0.34f));
    draws_.push_back({stoneFirst, static_cast<uint32_t>(indices_.size()) - stoneFirst, 1u, false});

    const uint32_t sphereFirst = static_cast<uint32_t>(indices_.size());
    constexpr uint32_t stacks = 48u;
    constexpr uint32_t slices = 72u;
    const uint32_t sphereBase = static_cast<uint32_t>(vertices_.size());
    vertices_.push_back({glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
                         glm::vec3(0.94f, 0.52f, 0.13f)});
    for (uint32_t stack = 1u; stack < stacks; ++stack) {
        const float latitude = kPi * static_cast<float>(stack) / static_cast<float>(stacks);
        const float ringRadius = std::sin(latitude);
        const float y = std::cos(latitude);
        for (uint32_t slice = 0u; slice < slices; ++slice) {
            const float longitude = kTwoPi * static_cast<float>(slice) /
                                    static_cast<float>(slices);
            const glm::vec3 normal(ringRadius * std::cos(longitude), y,
                                   ringRadius * std::sin(longitude));
            const glm::vec3 color = ClampColor(glm::vec3(0.82f, 0.30f, 0.07f) +
                                                glm::vec3(0.11f * normal.y,
                                                          0.055f * (normal.x + 1.0f),
                                                          0.025f * (normal.z + 1.0f)));
            vertices_.push_back({normal * colliderRadius_, normal, color});
        }
    }
    const uint32_t bottom = static_cast<uint32_t>(vertices_.size());
    vertices_.push_back({glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f),
                         glm::vec3(0.72f, 0.28f, 0.08f)});

    const uint32_t firstRing = sphereBase + 1u;
    for (uint32_t slice = 0u; slice < slices; ++slice) {
        const uint32_t next = (slice + 1u) % slices;
        indices_.insert(indices_.end(), {sphereBase, firstRing + next, firstRing + slice});
    }
    constexpr uint32_t ringCount = stacks - 1u;
    for (uint32_t ring = 0u; ring + 1u < ringCount; ++ring) {
        const uint32_t upper = firstRing + ring * slices;
        const uint32_t lower = upper + slices;
        for (uint32_t slice = 0u; slice < slices; ++slice) {
            const uint32_t next = (slice + 1u) % slices;
            indices_.insert(indices_.end(), {upper + slice, upper + next, lower + next,
                                              upper + slice, lower + next, lower + slice});
        }
    }
    const uint32_t lastRing = firstRing + (ringCount - 1u) * slices;
    for (uint32_t slice = 0u; slice < slices; ++slice) {
        const uint32_t next = (slice + 1u) % slices;
        indices_.insert(indices_.end(), {bottom, lastRing + slice, lastRing + next});
    }
    draws_.push_back({sphereFirst, static_cast<uint32_t>(indices_.size()) - sphereFirst, 2u, true});
}

void Scene::RecordStaticFootprint(std::size_t firstVertex) {
    if (firstVertex >= vertices_.size()) {
        throw std::logic_error("static object emitted no vertices");
    }

    std::vector<glm::vec2> points;
    points.reserve(vertices_.size() - firstVertex);
    for (std::size_t i = firstVertex; i < vertices_.size(); ++i) {
        points.emplace_back(vertices_[i].position.x, vertices_[i].position.z);
    }

    StaticFootprint footprint;
    footprint.hull = ConvexHull(std::move(points));
    if (footprint.hull.empty()) {
        throw std::logic_error("static object footprint is empty");
    }
    footprint.minimum = footprint.hull.front();
    footprint.maximum = footprint.hull.front();
    for (const glm::vec2& point : footprint.hull) {
        footprint.minimum = glm::min(footprint.minimum, point);
        footprint.maximum = glm::max(footprint.maximum, point);
    }
    staticFootprints_.push_back(std::move(footprint));
}

float Scene::StaticObstacleDistance(const glm::vec3& position) const {
    const glm::vec2 point(position.x, position.z);
    float minimum = std::numeric_limits<float>::infinity();
    for (const StaticFootprint& footprint : staticFootprints_) {
        const float lowerBoundSquared =
            AabbDistanceSquared(point, footprint.minimum, footprint.maximum);
        if (std::isfinite(minimum) && lowerBoundSquared >= minimum * minimum) {
            continue;
        }
        minimum = std::min(minimum, PointHullDistance(point, footprint.hull));
        if (minimum == 0.0f) {
            break;
        }
    }
    return minimum;
}

void Scene::BuildLightMatrix() {
    constexpr float maximumGrassHeight = 1.2f;
    constexpr float maximumGrassWidth = 0.06f;
    const float grassReach = 1.01f * maximumGrassHeight +
                             0.5f * maximumGrassWidth + kPlacementEpsilon;
    const float halfField = options_.fieldSize * 0.5f;
    glm::vec3 worldMinimum(-halfField - grassReach, 0.0f, -halfField - grassReach);
    glm::vec3 worldMaximum(halfField + grassReach, grassReach,
                           halfField + grassReach);

    if (draws_.size() >= 2u) {
        const SceneDraw& stone = draws_[1];
        const uint32_t end = stone.firstIndex + stone.indexCount;
        for (uint32_t i = stone.firstIndex; i < end; ++i) {
            const glm::vec3& position = vertices_.at(indices_.at(i)).position;
            worldMinimum = glm::min(worldMinimum, position);
            worldMaximum = glm::max(worldMaximum, position);
        }
    }

    const float horizontalCenterLimit = std::max(halfField, 4.0f);
    const float verticalCenterLimit = std::max(6.0f, options_.fieldSize * 0.25f);
    worldMinimum = glm::min(worldMinimum,
                            glm::vec3(-horizontalCenterLimit - colliderRadius_, 0.0f,
                                      -halfField - colliderRadius_));
    worldMaximum = glm::max(worldMaximum,
                            glm::vec3(horizontalCenterLimit + colliderRadius_,
                                      verticalCenterLimit + colliderRadius_,
                                      halfField + colliderRadius_));

    const glm::vec3 center = 0.5f * (worldMinimum + worldMaximum);
    const glm::vec3 extent = 0.5f * (worldMaximum - worldMinimum);
    const float viewDistance = std::max(10.0f, glm::length(extent) * 2.5f + 4.0f);
    const glm::vec3 up = std::abs(glm::dot(sunDirection_, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.95f
                             ? glm::vec3(0.0f, 0.0f, 1.0f)
                             : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::mat4 lightView = glm::lookAt(center + sunDirection_ * viewDistance,
                                            center, up);

    float minimumX = std::numeric_limits<float>::infinity();
    float maximumX = -std::numeric_limits<float>::infinity();
    float minimumY = std::numeric_limits<float>::infinity();
    float maximumY = -std::numeric_limits<float>::infinity();
    float minimumDepth = std::numeric_limits<float>::infinity();
    float maximumDepth = -std::numeric_limits<float>::infinity();
    for (uint32_t corner = 0u; corner < 8u; ++corner) {
        const glm::vec3 world((corner & 1u) != 0u ? worldMaximum.x : worldMinimum.x,
                              (corner & 2u) != 0u ? worldMaximum.y : worldMinimum.y,
                              (corner & 4u) != 0u ? worldMaximum.z : worldMinimum.z);
        const glm::vec4 light = lightView * glm::vec4(world, 1.0f);
        minimumX = std::min(minimumX, light.x);
        maximumX = std::max(maximumX, light.x);
        minimumY = std::min(minimumY, light.y);
        maximumY = std::max(maximumY, light.y);
        const float depth = -light.z;
        minimumDepth = std::min(minimumDepth, depth);
        maximumDepth = std::max(maximumDepth, depth);
    }

    const float xySpan = std::max(maximumX - minimumX, maximumY - minimumY);
    const float xyPadding = std::max(0.5f, xySpan * 0.03f);
    float projectionWidth = std::max(0.01f, maximumX - minimumX + 2.0f * xyPadding);
    float projectionHeight = std::max(0.01f, maximumY - minimumY + 2.0f * xyPadding);
    float centerX = 0.5f * (minimumX + maximumX);
    float centerY = 0.5f * (minimumY + maximumY);
    const float texelX = projectionWidth / static_cast<float>(kShadowResolution);
    const float texelY = projectionHeight / static_cast<float>(kShadowResolution);
    centerX = std::round(centerX / texelX) * texelX;
    centerY = std::round(centerY / texelY) * texelY;

    const float depthPadding = std::max(2.0f, (maximumDepth - minimumDepth) * 0.05f);
    const float nearPlane = std::max(0.01f, minimumDepth - depthPadding);
    const float farPlane = std::max(nearPlane + 0.01f, maximumDepth + depthPadding);
    glm::mat4 lightProjection = glm::ortho(centerX - 0.5f * projectionWidth,
                                           centerX + 0.5f * projectionWidth,
                                           centerY - 0.5f * projectionHeight,
                                           centerY + 0.5f * projectionHeight,
                                           nearPlane, farPlane);
    lightProjection[1][1] *= -1.0f;
    lightViewProj_ = lightProjection * lightView;
}

void Scene::ConfigureCamera(const std::string& preset) {
    if (!IsCameraPreset(preset)) {
        throw std::invalid_argument("invalid camera preset '" + preset + "'");
    }

    if (preset == "overview") {
        cameraTarget_ = glm::vec3(0.0f, 0.95f, -options_.fieldSize * 0.055f);
        cameraYaw_ = 0.72f;
        cameraPitch_ = 0.21f;
        cameraDistance_ = glm::clamp(options_.fieldSize * 0.58f, 12.0f, 300.0f);
    } else if (preset == "grazing") {
        cameraTarget_ = glm::vec3(0.0f, 0.82f, -options_.fieldSize * 0.08f);
        cameraYaw_ = 0.20f;
        cameraPitch_ = 0.065f;
        cameraDistance_ = glm::clamp(options_.fieldSize * 0.47f, 10.0f, 260.0f);
    } else if (preset == "occluder") {
        cameraTarget_ = glm::vec3(options_.fieldSize * 0.035f, 0.82f,
                                  -options_.fieldSize * 0.08f);
        cameraYaw_ = -0.82f;
        cameraPitch_ = 0.055f;
        cameraDistance_ = glm::clamp(options_.fieldSize * 0.47f, 10.0f, 260.0f);
    } else if (preset == "interaction") {
        cameraTarget_ = glm::vec3(0.0f, 0.55f, 0.0f);
        cameraYaw_ = 0.2f;
        cameraPitch_ = 0.22f;
        cameraDistance_ = 6.5f;
    } else if (preset == "detail") {
        cameraTarget_ = glm::vec3(-2.5f, 0.55f, 3.0f);
        cameraYaw_ = 0.25f;
        cameraPitch_ = 0.16f;
        cameraDistance_ = 4.0f;
    } else {
        cameraTarget_ = glm::vec3(0.0f, 0.95f, -0.055f * options_.fieldSize);
        cameraYaw_ = 0.6f;
        cameraPitch_ = 0.015f;
        cameraDistance_ = glm::clamp(0.5f * options_.fieldSize, 12.0f, 300.0f);
    }
}

glm::vec3 Scene::CameraPosition() const {
    const float horizontal = std::cos(cameraPitch_) * cameraDistance_;
    return cameraTarget_ + glm::vec3(std::sin(cameraYaw_) * horizontal,
                                     std::sin(cameraPitch_) * cameraDistance_,
                                     std::cos(cameraYaw_) * horizontal);
}

void Scene::Update(GLFWwindow* window, float delta, float simTime) {
    bool sweepAssigning = false;
    if (options_.sphereMotion == "sweep" && !sweepFinished_) {
        const bool automated = !options_.benchmarkPath.empty() ||
                               !options_.captureDir.empty() || options_.verify;
        const float startTime = automated
                                    ? static_cast<float>(options_.warmup) * options_.fixedDelta
                                    : 0.0f;
        const float safeTime = std::isfinite(simTime) ? std::max(simTime, 0.0f) : 0.0f;
        if (safeTime < startTime) {
            colliderCenter_ = glm::vec3(-4.0f, colliderRadius_, 0.0f);
            sweepAssigning = true;
        } else {
            const float scriptedTime = safeTime - startTime;
            if (scriptedTime < 1.0f) {
                colliderCenter_ = glm::vec3(-4.0f, colliderRadius_, 0.0f);
                sweepAssigning = true;
            } else if (scriptedTime < 6.0f) {
                const float fraction = (scriptedTime - 1.0f) / 5.0f;
                colliderCenter_ = glm::vec3(-4.0f + 8.0f * fraction,
                                            colliderRadius_, 0.0f);
                sweepAssigning = true;
            } else if (scriptedTime < 8.0f) {
                colliderCenter_ = glm::vec3(4.0f, colliderRadius_, 0.0f);
                sweepAssigning = true;
            } else {
                colliderCenter_ = glm::vec3(4.0f, colliderRadius_, 0.0f);
                sweepFinished_ = true;
            }
        }
    }

    if (window == nullptr) {
        return;
    }

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(window, &cursorX, &cursorY);
    if (!cursorInitialized_) {
        previousCursorX_ = cursorX;
        previousCursorY_ = cursorY;
        cursorInitialized_ = true;
    }
    const float cursorDeltaX = static_cast<float>(cursorX - previousCursorX_);
    const float cursorDeltaY = static_cast<float>(cursorY - previousCursorY_);
    previousCursorX_ = cursorX;
    previousCursorY_ = cursorY;

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        cameraYaw_ -= cursorDeltaX * 0.005f;
        const float minimumPitch = options_.camera == "beauty" ? 0.005f : 0.025f;
        cameraPitch_ = glm::clamp(cameraPitch_ - cursorDeltaY * 0.004f,
                                  minimumPitch, 1.45f);
    }
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        cameraDistance_ *= std::exp(cursorDeltaY * 0.010f);
        cameraDistance_ = glm::clamp(cameraDistance_, 2.25f,
                                     std::max(12.0f, options_.fieldSize * 3.0f));
    }

    const auto edgePressed = [&](std::size_t slot, int key) {
        const bool down = glfwGetKey(window, key) == GLFW_PRESS;
        const bool edge = down && !toggleKeyDown_[slot];
        toggleKeyDown_[slot] = down;
        return edge;
    };
    if (edgePressed(0u, GLFW_KEY_1)) {
        cullMask_ ^= CullOrientation;
    }
    if (edgePressed(1u, GLFW_KEY_2)) {
        cullMask_ ^= CullFrustum;
    }
    if (edgePressed(2u, GLFW_KEY_3)) {
        cullMask_ ^= CullDistance;
    }
    if (edgePressed(3u, GLFW_KEY_4)) {
        cullMask_ ^= CullOcclusion;
    }
    if (edgePressed(4u, GLFW_KEY_L)) {
        lodEnabled_ = !lodEnabled_;
    }
    if (edgePressed(5u, GLFW_KEY_F)) {
        featureMask_ ^= Wireframe;
    }
    if (edgePressed(9u, GLFW_KEY_B)) {
        featureMask_ ^= BetterShading;
    }
    if (edgePressed(8u, GLFW_KEY_H)) {
        featureMask_ ^= ShowOverlay;
    }
    if (edgePressed(6u, GLFW_KEY_SPACE)) {
        paused_ = !paused_;
    }
    if (edgePressed(7u, GLFW_KEY_R)) {
        resetRequested_ = true;
    }
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    float inputDelta = std::isfinite(delta) ? delta : 0.0f;
    inputDelta = glm::clamp(inputDelta, 0.0f, 0.1f);
    glm::vec3 forward = cameraTarget_ - CameraPosition();
    forward.y = 0.0f;
    const float forwardLength = glm::length(forward);
    if (forwardLength > 1.0e-6f) {
        forward /= forwardLength;
    } else {
        forward = glm::vec3(0.0f, 0.0f, -1.0f);
    }
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

    glm::vec3 movement(0.0f);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        movement += forward;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        movement -= forward;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        movement += right;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        movement -= right;
    }
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
        movement.y += 1.0f;
    }
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
        movement.y -= 1.0f;
    }
    const float movementLength = glm::length(movement);
    if (!sweepAssigning && movementLength > 1.0e-6f) {
        colliderCenter_ += movement / movementLength * (4.5f * inputDelta);
        const float halfField = options_.fieldSize * 0.5f;
        colliderCenter_.x = glm::clamp(colliderCenter_.x, -halfField, halfField);
        colliderCenter_.z = glm::clamp(colliderCenter_.z, -halfField, halfField);
        colliderCenter_.y = glm::clamp(colliderCenter_.y, colliderRadius_,
                                       std::max(6.0f, options_.fieldSize * 0.25f));
    }

    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
        windAngle_ += 0.90f * inputDelta;
    }
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        windAngle_ -= 0.90f * inputDelta;
    }
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
        windStrength_ = std::min(18.0f, windStrength_ + 3.0f * inputDelta);
    }
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
        windStrength_ = std::max(0.0f, windStrength_ - 3.0f * inputDelta);
    }
    wind_ = glm::vec3(std::cos(windAngle_) * windStrength_, 0.0f,
                      std::sin(windAngle_) * windStrength_);
}

FrameUniform Scene::MakeUniform(uint32_t width, uint32_t height, float delta, float simTime) const {
    FrameUniform uniform{};
    const glm::vec3 cameraPosition = CameraPosition();
    uniform.view = glm::lookAt(cameraPosition, cameraTarget_, glm::vec3(0.0f, 1.0f, 0.0f));

    const float safeWidth = static_cast<float>(std::max(width, 1u));
    const float safeHeight = static_cast<float>(std::max(height, 1u));
    const float maximumCameraDistance = std::max(12.0f, options_.fieldSize * 3.0f);
    const float farPlane = std::max(100.0f,
                                    maximumCameraDistance + options_.fieldSize * 1.5f);
    const float verticalFov = options_.camera == "beauty" ? 70.0f : 55.0f;
    uniform.proj = glm::perspective(verticalFov * kPi / 180.0f, safeWidth / safeHeight,
                                    0.1f, farPlane);
    uniform.proj[1][1] *= -1.0f;

    uniform.cameraTime = glm::vec4(cameraPosition, simTime);
    uniform.wind = glm::vec4(wind_, gustFrequency_);
    uniform.collider = glm::vec4(colliderCenter_, colliderRadius_);
    uniform.parameters = glm::vec4(paused_ ? 0.0f : delta, options_.maxDistance, 8.0f, 12.0f);
    uniform.viewport = glm::vec4(static_cast<float>(width), static_cast<float>(height), 5.0f, 40.0f);
    uniform.flags = glm::uvec4(static_cast<uint32_t>(blades_.size()), cullMask_ & kKnownCullMask,
                               lodEnabled_ ? 1u : 0u, featureMask_);
    uniform.lighting = glm::vec4(sunDirection_, 1.0f);
    uniform.terrain = glm::vec4(options_.fieldSize, 0.0f, 0.0f, 0.0f);
    uniform.lightViewProj = lightViewProj_;
    uniform.sunColor = glm::vec4(1.0f, 0.84f, 0.64f, 1.6f);
    uniform.atmosphere = glm::vec4(22.0f, 90.0f, 0.0f, 0.0f);
    uniform.renderControl = glm::uvec4(options_.renderFeatureMask, 0u, 0u, 0u);
    return uniform;
}

const std::vector<Blade>& Scene::Blades() const {
    return blades_;
}

const std::vector<SceneVertex>& Scene::Vertices() const {
    return vertices_;
}

const std::vector<uint32_t>& Scene::Indices() const {
    return indices_;
}

const std::vector<SceneDraw>& Scene::Draws() const {
    return draws_;
}

bool Scene::IsPaused() const {
    return paused_;
}

bool Scene::ConsumeResetRequested() {
    const bool requested = resetRequested_;
    resetRequested_ = false;
    return requested;
}

RunOptions ParseOptions(int argc, char** argv) {
    if (argc < 0 || (argc > 0 && argv == nullptr)) {
        throw std::invalid_argument("invalid command-line argument array");
    }

    RunOptions options;
    std::optional<uint32_t> explicitCulling;
    std::optional<bool> explicitLod;
    std::optional<bool> explicitOcclusion;

    for (int i = 1; i < argc; ++i) {
        if (argv[i] == nullptr) {
            throw std::invalid_argument("null command-line argument");
        }
        const std::string argument(argv[i]);
        if (argument == "--help") {
            options.help = true;
        } else if (argument == "--width") {
            const uint32_t value = ParseUnsigned(RequireValue(argc, argv, i, argument), argument);
            if (value == 0u || value > kMaximumDimension) {
                throw std::invalid_argument("--width must be between 1 and 16384");
            }
            options.width = value;
        } else if (argument == "--height") {
            const uint32_t value = ParseUnsigned(RequireValue(argc, argv, i, argument), argument);
            if (value == 0u || value > kMaximumDimension) {
                throw std::invalid_argument("--height must be between 1 and 16384");
            }
            options.height = value;
        } else if (argument == "--blades") {
            const uint32_t value = ParseUnsigned(RequireValue(argc, argv, i, argument), argument);
            if (value > kMaximumBladeCount) {
                throw std::invalid_argument("--blades must be between 0 and 16777216");
            }
            options.bladeCount = value;
        } else if (argument == "--seed") {
            options.seed = ParseUnsigned(RequireValue(argc, argv, i, argument), argument);
        } else if (argument == "--culling") {
            const std::string value = RequireValue(argc, argv, i, argument);
            if (value == "none") {
                explicitCulling = 0u;
            } else if (value == "orientation") {
                explicitCulling = CullOrientation;
            } else if (value == "frustum") {
                explicitCulling = CullFrustum;
            } else if (value == "distance") {
                explicitCulling = CullDistance;
            } else if (value == "all") {
                explicitCulling = kBaselineCullMask;
            } else {
                throw std::invalid_argument("--culling expects none, orientation, frustum, distance, or all");
            }
        } else if (argument == "--lod") {
            explicitLod = ParseOnOff(RequireValue(argc, argv, i, argument), argument);
        } else if (argument == "--occlusion") {
            explicitOcclusion = ParseOnOff(RequireValue(argc, argv, i, argument), argument);
        } else if (argument == "--interaction") {
            options.interactionOverride =
                ParseOnOff(RequireValue(argc, argv, i, argument), argument);
        } else if (argument == "--shading") {
            const std::string value = RequireValue(argc, argv, i, argument);
            if (value == "basic") {
                options.shadingOverride = false;
            } else if (value == "enhanced") {
                options.shadingOverride = true;
            } else {
                throw std::invalid_argument("--shading expects basic or enhanced");
            }
        } else if (argument == "--render-disable") {
            DisableRenderFeatures(RequireValue(argc, argv, i, argument),
                                  options.renderFeatureMask);
        } else if (argument == "--preset") {
            const std::string value = RequireValue(argc, argv, i, argument);
            (void)DefinitionForPreset(value);
            options.preset = value;
        } else if (argument == "--camera") {
            const std::string value = RequireValue(argc, argv, i, argument);
            if (!IsCameraPreset(value)) {
                throw std::invalid_argument(
                    "--camera expects auto, overview, grazing, occluder, interaction, detail, or beauty");
            }
            options.camera = value;
        } else if (argument == "--sphere-motion") {
            const std::string value = RequireValue(argc, argv, i, argument);
            if (value != "manual" && value != "sweep") {
                throw std::invalid_argument("--sphere-motion expects manual or sweep");
            }
            options.sphereMotion = value;
        } else if (argument == "--wind-strength") {
            const float value = ParseFiniteFloat(RequireValue(argc, argv, i, argument), argument);
            if (value < 0.0f || value > 18.0f) {
                throw std::invalid_argument("--wind-strength must be between 0 and 18");
            }
            options.windStrength = value;
        } else if (argument == "--sun-azimuth") {
            options.sunAzimuth = NormalizeDegrees(
                ParseFiniteFloat(RequireValue(argc, argv, i, argument), argument));
        } else if (argument == "--sun-elevation") {
            const float value = ParseFiniteFloat(RequireValue(argc, argv, i, argument), argument);
            if (value < 1.0f || value > 89.0f) {
                throw std::invalid_argument("--sun-elevation must be between 1 and 89 degrees");
            }
            options.sunElevation = value;
        } else if (argument == "--benchmark") {
            options.benchmarkPath = RequireValue(argc, argv, i, argument);
            if (options.benchmarkPath.empty()) {
                throw std::invalid_argument("--benchmark requires a non-empty path");
            }
        } else if (argument == "--capture-dir") {
            options.captureDir = RequireValue(argc, argv, i, argument);
            if (options.captureDir.empty()) {
                throw std::invalid_argument("--capture-dir requires a non-empty path");
            }
        } else if (argument == "--warmup") {
            options.warmup = ParseUnsigned(RequireValue(argc, argv, i, argument), argument);
        } else if (argument == "--frames") {
            options.frames = ParseUnsigned(RequireValue(argc, argv, i, argument), argument);
            if (options.frames == 0u) {
                throw std::invalid_argument("--frames must be positive");
            }
        } else if (argument == "--fixed-dt") {
            options.fixedDelta = ParseFiniteFloat(RequireValue(argc, argv, i, argument), argument);
            if (options.fixedDelta <= 0.0f || options.fixedDelta > 1.0f / 30.0f) {
                throw std::invalid_argument("--fixed-dt must be greater than 0 and at most 1/30");
            }
        } else if (argument == "--hidden") {
            options.hidden = true;
        } else if (argument == "--validation") {
            options.validation = ParseOnOff(RequireValue(argc, argv, i, argument), argument);
        } else if (argument == "--verify") {
            options.verify = true;
        } else if (argument == "--physics-probe") {
            options.physicsProbePath = RequireValue(argc, argv, i, argument);
            if (options.physicsProbePath.empty()) {
                throw std::invalid_argument("--physics-probe requires a non-empty path");
            }
        } else if (argument == "--field-size") {
            options.fieldSize = ParseFiniteFloat(RequireValue(argc, argv, i, argument), argument);
            if (options.fieldSize < 1.0f || options.fieldSize > 512.0f) {
                throw std::invalid_argument("--field-size must be between 1 and 512");
            }
        } else if (argument == "--max-distance") {
            options.maxDistance = ParseFiniteFloat(RequireValue(argc, argv, i, argument), argument);
            if (options.maxDistance <= 0.0f) {
                throw std::invalid_argument("--max-distance must be positive");
            }
        } else {
            throw std::invalid_argument("unknown argument '" + argument + "'");
        }
    }

    const PresetDefinition preset = DefinitionForPreset(options.preset);
    options.culling = preset.culling;
    options.lod = preset.lod;
    options.occlusion = preset.occlusion;
    if (explicitCulling.has_value()) {
        options.culling = *explicitCulling;
    }
    if (explicitLod.has_value()) {
        options.lod = *explicitLod;
    }
    if (explicitOcclusion.has_value()) {
        options.occlusion = *explicitOcclusion;
    }
    if (!options.physicsProbePath.empty() && !options.verify) {
        throw std::invalid_argument("--physics-probe requires --verify");
    }
    options.camera = ResolveCameraPreset(options.camera, options.preset);
    return options;
}

void PrintHelp() {
    const RunOptions defaults;
    std::cout
        << "Vulkan Grass Rendering\n"
        << "Usage: vulkan_grass_rendering [options]\n\n"
        << "Scene and rendering options:\n"
        << "  --width N              Window width, 1..16384 (default " << defaults.width << ")\n"
        << "  --height N             Window height, 1..16384 (default " << defaults.height << ")\n"
        << "  --blades N             Grass blades, 0..16777216 (default " << defaults.bladeCount << ")\n"
        << "  --seed N               Deterministic uint32 seed (default " << defaults.seed << ")\n"
        << "  --field-size VALUE     Square field size, 1..512 (default " << defaults.fieldSize << ")\n"
        << "  --max-distance VALUE   Positive distance-culling limit (default " << defaults.maxDistance << ")\n"
        << "  --culling MODE         none|orientation|frustum|distance|all\n"
        << "  --lod on|off           Adaptive grass detail (default on)\n"
        << "  --occlusion on|off     Depth occlusion culling (default on)\n"
        << "  --interaction on|off   Override the preset's sphere interaction feature\n"
        << "  --shading MODE         basic|enhanced; override the preset shading mode\n"
        << "  --render-disable LIST  Comma-list: sky,root-color,rounded-normals,\n"
        << "                         transmission,specular,fog,shadows (repeatable)\n"
        << "  --preset NAME          basic|forces|culling|lod|occlusion|interaction|final|wireframe\n"
        << "                         (default final; explicit feature choices override it)\n"
        << "  --camera NAME          auto|overview|grazing|occluder|interaction|detail|beauty\n"
        << "                         (default auto: beauty for final, interaction for interaction)\n"
        << "  --sphere-motion MODE   manual|sweep (default " << defaults.sphereMotion << ")\n"
        << "  --wind-strength VALUE  Wind magnitude, 0..18 (default " << defaults.windStrength << ")\n"
        << "  --sun-azimuth DEGREES  Surface-to-sun azimuth, normalized to [0,360) (default "
        << defaults.sunAzimuth << ")\n"
        << "  --sun-elevation DEG    Surface-to-sun elevation, 1..89 (default "
        << defaults.sunElevation << ")\n\n"
        << "Automation and diagnostics:\n"
        << "  --benchmark PATH       Write benchmark results to PATH\n"
        << "  --capture-dir PATH     Write captured frames to PATH\n"
        << "  --warmup N             Warm-up frames (default " << defaults.warmup << ")\n"
        << "  --frames N             Measured frames, positive (default " << defaults.frames << ")\n"
        << "  --fixed-dt SECONDS     Fixed step in (0, 1/30] (default " << defaults.fixedDelta << ")\n"
        << "  --hidden               Create a hidden window\n"
        << "  --validation on|off    Vulkan validation (build default "
        << (defaults.validation ? "on" : "off") << ")\n"
        << "  --verify               Enable renderer verification checks\n"
        << "  --physics-probe PATH   Write physics probe data (requires --verify)\n"
        << "  --help                 Print this help without starting Vulkan\n\n"
        << "Controls:\n"
        << "  Left drag orbit; right drag zoom; W/A/S/D move the sphere; Q/E move it down/up.\n"
        << "  Arrow left/right rotate wind; arrow up/down change wind strength.\n"
        << "  1/2/3 toggle orientation/frustum/distance culling; 4 toggles occlusion.\n"
        << "  L toggles LOD; B toggles enhanced shading; F toggles wireframe; H toggles overlay.\n"
        << "  Space pauses.\n"
        << "  R resets grass; Esc exits.\n";
}
