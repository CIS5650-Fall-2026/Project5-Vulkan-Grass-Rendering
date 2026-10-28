#ifndef VULKAN_GRASS_SHADOW_COMMON_GLSL
#define VULKAN_GRASS_SHADOW_COMMON_GLSL

layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap;

bool shadowFinite(float value) {
    return !isnan(value) && !isinf(value);
}

vec3 shadowGeometricNormal(vec3 rawNormal, vec3 fallbackNormal) {
    float lengthSquared = dot(rawNormal, rawNormal);
    if (shadowFinite(lengthSquared) && lengthSquared > 1.0e-20) {
        return rawNormal * inversesqrt(lengthSquared);
    }
    return fallbackNormal;
}

void shadowProjectionRows(out vec3 rowX, out vec3 rowY, out vec3 rowZ) {
    rowX = vec3(frame.lightViewProj[0][0],
                frame.lightViewProj[1][0],
                frame.lightViewProj[2][0]);
    rowY = vec3(frame.lightViewProj[0][1],
                frame.lightViewProj[1][1],
                frame.lightViewProj[2][1]);
    rowZ = vec3(frame.lightViewProj[0][2],
                frame.lightViewProj[1][2],
                frame.lightViewProj[2][2]);
}

bool shadowProject(vec3 worldPosition, out vec3 projected) {
    vec4 clip = frame.lightViewProj * vec4(worldPosition, 1.0);
    if (clip.w <= 1.0e-7) {
        projected = vec3(0.0);
        return false;
    }
    vec3 ndc = clip.xyz / clip.w;
    projected = vec3(ndc.xy * 0.5 + 0.5, ndc.z);
    return all(greaterThanEqual(projected, vec3(0.0)))
        && all(lessThanEqual(projected, vec3(1.0)));
}

float shadowWorldTexel(ivec2 shadowExtent) {
    vec3 rowX;
    vec3 rowY;
    vec3 rowZ;
    shadowProjectionRows(rowX, rowY, rowZ);
    float rowXLength = length(rowX);
    float rowYLength = length(rowY);
    if (rowXLength <= 1.0e-7 || rowYLength <= 1.0e-7) {
        return 0.0;
    }
    float pixelX = 2.0 / (float(shadowExtent.x) * rowXLength);
    float pixelY = 2.0 / (float(shadowExtent.y) * rowYLength);
    return max(pixelX, pixelY);
}

float shadowBaseDepthBias(ivec2 shadowExtent, vec3 rowZ) {
    float worldTexel = shadowWorldTexel(shadowExtent);
    float rowZLength = length(rowZ);
    if (!shadowFinite(worldTexel) || !shadowFinite(rowZLength)
            || !(worldTexel > 0.0) || !(rowZLength > 0.0)) {
        return 2.0e-7;
    }
    return max(2.0e-7, 0.25 * worldTexel * rowZLength);
}

vec2 shadowReceiverDepthGradient(vec3 normal, ivec2 shadowExtent,
                                 vec3 rowX, vec3 rowY, vec3 rowZ) {
    float rowXLengthSquared = dot(rowX, rowX);
    float rowYLengthSquared = dot(rowY, rowY);
    float rowZLengthSquared = dot(rowZ, rowZ);
    if (!shadowFinite(rowXLengthSquared)
            || !shadowFinite(rowYLengthSquared)
            || !shadowFinite(rowZLengthSquared)
            || rowXLengthSquared <= 1.0e-20
            || rowYLengthSquared <= 1.0e-20
            || rowZLengthSquared <= 1.0e-20) {
        return vec2(0.0);
    }

    vec3 worldPerU = 2.0 * rowX / rowXLengthSquared;
    vec3 worldPerV = 2.0 * rowY / rowYLengthSquared;
    vec3 worldPerDepth = rowZ / rowZLengthSquared;
    float denominator = dot(normal, worldPerDepth);
    float denominatorLimit = max(1.0e-5 * length(worldPerDepth), 1.0e-8);
    bool grazing = abs(denominator) < denominatorLimit;
    float safeDenominator = grazing
        ? (denominator < 0.0 ? -denominatorLimit : denominatorLimit)
        : denominator;

    vec2 gradient = -vec2(dot(normal, worldPerU),
                          dot(normal, worldPerV)) / safeDenominator;
    if (any(isnan(gradient)) || any(isinf(gradient))) {
        return vec2(0.0);
    }

    if (grazing) {
        vec2 boundedSlope = 0.5 * vec2(shadowExtent);
        gradient = clamp(gradient, -boundedSlope, boundedSlope);
    }
    return gradient;
}

float shadowTap(vec2 tapUV, vec3 projected, vec2 depthGradient,
                float baseDepthBias, ivec2 shadowExtent) {
    vec2 extent = vec2(shadowExtent);
    ivec2 texelIndex = ivec2(floor(tapUV * extent));
    if (any(lessThan(texelIndex, ivec2(0)))
            || any(greaterThanEqual(texelIndex, shadowExtent))) {
        return 1.0;
    }

    vec2 texelCenterUV = (vec2(texelIndex) + vec2(0.5)) / extent;
    float referenceDepth = projected.z
        + dot(depthGradient, texelCenterUV - projected.xy)
        - baseDepthBias;
    if (!shadowFinite(referenceDepth)
            || referenceDepth < 0.0 || referenceDepth > 1.0) {
        return 1.0;
    }
    return texture(shadowMap, vec3(texelCenterUV, referenceDepth));
}

float shadowVisibility(vec3 worldPosition, vec3 normal, vec3 sunDirection) {
    if (!renderFeatureEnabled(RENDER_SHADOWS_BIT)) {
        return 1.0;
    }

    vec3 projected;
    if (!shadowProject(worldPosition, projected)) {
        return 1.0;
    }

    ivec2 shadowExtent = textureSize(shadowMap, 0);
    if (any(lessThanEqual(shadowExtent, ivec2(0)))) {
        return 1.0;
    }

    vec3 rowX;
    vec3 rowY;
    vec3 rowZ;
    shadowProjectionRows(rowX, rowY, rowZ);
    vec2 depthGradient = shadowReceiverDepthGradient(
        normal, shadowExtent, rowX, rowY, rowZ);
    float baseDepthBias = shadowBaseDepthBias(shadowExtent, rowZ);
    vec2 texel = vec2(1.0) / vec2(shadowExtent);
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 tapUV = projected.xy
                       + vec2(float(x), float(y)) * texel;
            visibility += shadowTap(tapUV, projected, depthGradient,
                                    baseDepthBias, shadowExtent);
        }
    }
    return visibility / 9.0;
}

#endif
