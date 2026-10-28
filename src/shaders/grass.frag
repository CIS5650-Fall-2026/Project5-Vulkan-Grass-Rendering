#version 450
#extension GL_GOOGLE_include_directive : require

#include "frame_uniform.glsl"
#include "render_common.glsl"
#include "shadow_common.glsl"

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in float inBladeHeight;
layout(location = 3) in float inVariation;
layout(location = 4) in vec3 inTangent;
layout(location = 5) in float inBladeAcross;
layout(location = 6) in vec3 inLateral;

layout(location = 0) out vec4 outColor;

vec3 bladeAlbedo(bool enhanced) {
    float height = clamp(inBladeHeight, 0.0, 1.0);
    bool rootColorEnabled = renderFeatureEnabled(RENDER_ROOT_COLOR_BIT);
    vec3 rootColor = vec3(0.030, 0.090, 0.014);
    vec3 middleColor = vec3(0.135, 0.330, 0.045);
    vec3 tipColor = vec3(0.300, 0.455, 0.090);
    vec3 bladeColor = vec3(0.155, 0.345, 0.052);
    if (rootColorEnabled) {
        bladeColor = height < 0.58
            ? mix(rootColor, middleColor, height / 0.58)
            : mix(middleColor, tipColor, (height - 0.58) / 0.42);
    }

    float warmVariation = inVariation * 2.0 - 1.0;
    bladeColor *= mix(0.90, 1.10, inVariation);
    if (enhanced && rootColorEnabled) {
        vec3 dryTip = vec3(0.43, 0.36, 0.085);
        float dryAmount = smoothstep(0.69, 0.98, height)
                        * smoothstep(0.62, 0.94, inVariation);
        bladeColor = mix(bladeColor, dryTip, 0.58 * dryAmount);
        bladeColor += vec3(0.010, 0.006, -0.003)
                    * warmVariation * height;
    }
    return max(bladeColor, vec3(0.0));
}

vec3 rawBladeNormal(out vec3 rawLateral) {
    vec3 rawTangent = renderSafeNormalize(inTangent, vec3(0.0, 1.0, 0.0));
    vec3 lateralFallback = renderSafeNormalize(cross(rawTangent, inNormal),
                                                vec3(1.0, 0.0, 0.0));
    rawLateral = renderSafeNormalize(inLateral, lateralFallback);
    vec3 interpolatedNormal = renderSafeNormalize(inNormal,
                                                  vec3(0.0, 0.0, 1.0));
    return renderSafeNormalize(cross(rawLateral, rawTangent),
                               interpolatedNormal);
}

vec3 roundedBladeNormal(vec3 rawNormal, vec3 rawLateral) {
    if (!renderFeatureEnabled(RENDER_ROUNDED_NORMALS_BIT)) {
        return rawNormal;
    }
    float across = clamp(inBladeAcross, -1.0, 1.0);
    float centerWeight = sqrt(max(1.0 - 0.70 * across * across, 0.0));
    return renderSafeNormalize(rawNormal * centerWeight
                             + rawLateral * (0.55 * across), rawNormal);
}

void main() {
    vec3 rasterNormal = cross(dFdx(inWorldPosition), dFdy(inWorldPosition));
    vec3 rawLateral;
    vec3 unroundedNormal = rawBladeNormal(rawLateral);
    vec3 shadowNormal = shadowGeometricNormal(rasterNormal, unroundedNormal);
    vec3 rawNormal = roundedBladeNormal(unroundedNormal, rawLateral);
    float faceSign = gl_FrontFacing ? 1.0 : -1.0;
    vec3 normal = rawNormal * faceSign;

    vec3 toCamera = frame.cameraTime.xyz - inWorldPosition;
    float cameraDistance = length(toCamera);
    vec3 viewDirection = renderSafeNormalize(toCamera, normal);
    vec3 eyeRay = -viewDirection;
    vec3 sunDirection = renderSunDirection();
    vec3 sunRadiance = renderSunRadiance();
    bool enhanced = enhancedRenderingEnabled();

    vec3 albedo = bladeAlbedo(enhanced);
    float height = clamp(inBladeHeight, 0.0, 1.0);
    float frontLight = max(dot(normal, sunDirection), 0.0);
    float backLight = max(dot(-normal, sunDirection), 0.0);
    float diffuse = enhanced ? max(frontLight, 0.20 * backLight)
                             : max(frontLight, 0.32 * backLight);
    float visibility = shadowVisibility(inWorldPosition, shadowNormal,
                                        sunDirection);

    float contact = mix(0.68, 1.0, smoothstep(0.0, 0.24, height));
    float ambientStrength = enhanced ? 0.82 : 0.72;
    vec3 linearColor = albedo * renderAmbientRadiance(normal)
                     * ambientStrength * contact;
    float directStrength = enhanced ? 0.88 : 0.72;
    linearColor += visibility * sunRadiance * albedo
                 * (directStrength * diffuse) * contact;

    if (enhanced && renderFeatureEnabled(RENDER_TRANSMISSION_BIT)) {
        float transmission = pow(backLight, 1.35)
                           * mix(0.10, 0.27, height);
        vec3 transmissionColor = mix(albedo, vec3(0.48, 0.62, 0.12), 0.42);
        linearColor += visibility * sunRadiance * transmissionColor
                     * transmission * contact;
    }

    if (enhanced && renderFeatureEnabled(RENDER_SPECULAR_BIT)) {
        vec3 halfVector = renderSafeNormalize(sunDirection + viewDirection,
                                              sunDirection);
        float specularNear = max(frame.viewport.z, 0.0);
        float specularFar = max(frame.viewport.w, specularNear + 1.0e-3);
        float distanceFade = 1.0 - smoothstep(specularNear, specularFar,
                                              cameraDistance);
        float highlight = pow(max(dot(normal, halfVector), 0.0), 30.0)
                        * 0.11 * distanceFade
                        * smoothstep(0.12, 0.58, height);
        linearColor += visibility * sunRadiance
                     * vec3(0.72, 0.74, 0.48) * highlight;
    }

    linearColor = renderApplyFog(linearColor, inWorldPosition, eyeRay,
                                 cameraDistance);
    outColor = vec4(renderToneResponse(linearColor), 1.0);
}
