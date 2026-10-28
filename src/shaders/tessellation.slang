#define EPSILON 0.001f

#define OUTERUFAC 8.f
#define OUTERVFAC 2.f
#define INNERUFAC 8.f
#define INNERVFAC 2.f

//Matches with the attribute description in Blade.cpp
struct PatchInfo {
    [[vk::location(0)]] float4 v0;
    [[vk::location(1)]] float4 v1;
    [[vk::location(2)]] float4 v2;
    [[vk::location(3)]] float4 up;
};

//Consistent with quad
struct TessellationInfo {
    float outer[4] : SV_TessFactor;
    float inner[2] : SV_InsideTessFactor;
};

struct CameraBufferObject {
    float4x4 view;
    float4x4 proj;
    float4 pos;
};

struct DSOutput {
    float4 position : SV_Position;
    float4 pos;
    float4 nor;
};

void computeGeometryInfo(float3 up, float orientation, inout float3 tan, inout float3 bit, inout float3 facing) {
    float3 ref = (length(up - float3(1, 0, 0)) > EPSILON) ? float3(1, 0, 0) : float3(0, 0, 1);

    tan = cross(up, ref);
    bit = cross(tan, up);

    facing = normalize(cos(orientation) * tan + sin(orientation) * bit);
}

[[vk::binding(0, 1)]]
ConstantBuffer<float4x4> model;

[[vk::binding(0, 0)]]
ConstantBuffer<CameraBufferObject> camera;

[shader("vertex")]
PatchInfo vertMain(PatchInfo input) {
    PatchInfo output = {};
    output.v0 = float4(mul(model, float4(input.v0.xyz, 1.f)).xyz, input.v0.w);
    output.v1 = float4(mul(model, float4(input.v1.xyz, 1.f)).xyz, input.v1.w);
    output.v2 = float4(mul(model, float4(input.v2.xyz, 1.f)).xyz, input.v2.w);
    output.up = float4(normalize(mul(model, float4(input.up.xyz, 0.f)).xyz), input.up.w);

    return output;
}

//Runs once per input patch, defines the inner and outer levels
TessellationInfo patchConstant(InputPatch<PatchInfo, 1> patch) {
    TessellationInfo info = {};

    // TODO: Compute the tessellation levels based on view angle and closeness
    float3 tan, bit, facing;
    computeGeometryInfo(patch[0].up.xyz, patch[0].v0.w, tan, bit, facing);

    // If it's too far from the camera, cull
    float dist = length(patch[0].v0.xyz - camera.pos.xyz);
    // TODO: Make this better (lol)
    float distBucket = max(8.f - dist / 1.f, 1.f);

    info.outer[0] = distBucket * OUTERUFAC;
    info.outer[2] = distBucket * OUTERUFAC;

    info.outer[1] = distBucket * OUTERVFAC;
    info.outer[3] = distBucket * OUTERVFAC;

    info.inner[0] = distBucket * INNERUFAC;
    info.inner[1] = distBucket * INNERVFAC;
    return info;
}

//Tessellation control shader
[shader("hull")]
[domain("quad")]
[partitioning("integer")]
[outputtopology("triangle_ccw")]
[outputcontrolpoints(1)]
[patchconstantfunc("patchConstant")]
PatchInfo hullMain(InputPatch<PatchInfo, 1> patch, uint id: SV_OutputControlPointID) {
    return patch[0];
}

//Tessellation evaluation shader
[shader("domain")]
[domain("quad")]
DSOutput domMain(TessellationInfo tessellation, float2 uv: SV_DomainLocation, const OutputPatch<PatchInfo, 1> patch) {
    DSOutput output = {};

    float3 tan, bit, facing;
    computeGeometryInfo(patch[0].up.xyz, patch[0].v0.w, tan, bit, facing);

    float3 span = normalize(cross(patch[0].up.xyz, facing));

    float3 p0 = patch[0].v2.w * (uv.y - .5f) * span + patch[0].v0.xyz;
    float3 p1 = patch[0].v2.w * (uv.y - .5f) * .825f * span + patch[0].v1.xyz;
    float3 p2 = patch[0].v2.w * (uv.y - .5f) * .25f * span + patch[0].v2.xyz;
    // Compute the position of the subdivided vertex

    float3 p11 = lerp(p0, p1, uv.x);
    float3 p12 = lerp(p1, p2, uv.x);

    output.nor = float4(normalize(cross(p12 - p11, facing)), 1.f);
    output.pos = float4(lerp(p11, p12, uv.x), 1.f);

    // Project into clip space
    output.position = mul(camera.proj, mul(camera.view, output.pos));
    return output;
}

[shader("fragment")]
float4 fragMain(DSOutput input) : SV_Target {
    float3 col1 = float3(36, 117, 75) / 255.f;
    float3 col2 = float3(116, 214, 152) / 255.f;

    float3 look = normalize(camera.pos.xyz - input.pos.xyz);
    float3 nor = normalize(input.nor.xyz);
    float lambertian = clamp(abs(dot(look, nor)), .85f, 1.f);

    float3 col = lerp(col1, col2, clamp(input.pos.y / 3.f, 0.f, 1.f));

    return float4(col, 1.f);
}