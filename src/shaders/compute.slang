#define EPSILON 0.001f

#define SAFE_DIST 8.f
#define MAX_DIST 32.f
#define CULLING_BUCKETS 4.f

struct CameraBufferObject {
    float4x4 view;
    float4x4 proj;
    float4 pos;
};

struct Time {
    float deltaTime;
    float totalTime;
};

struct Blade {
    float4 v0;
    float4 v1;
    float4 v2;
    float4 up;
};

struct NumBlades {
    uint vertexCount;
    uint instanceCount = 1;
    uint firstVertex = 0;
    uint firstInstance = 0;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraBufferObject> camera;

[[vk::binding(0, 1)]]
ConstantBuffer<Time> time;

[[vk::binding(0, 2)]]
RWStructuredBuffer<Blade> blades;

[[vk::binding(1, 2)]]
RWStructuredBuffer<Blade> culledBlades;

[[vk::binding(2, 2)]]
RWStructuredBuffer<NumBlades> numBlades;

// TODO: Add bindings to:
// 1. Store the input blades
// 2. Write out the culled blades
// 3. Write the total number of blades remaining

// The project is using vkCmdDrawIndirect to use a buffer as the arguments for a draw call
// This is sort of an advanced feature so we've showed you what this buffer should look like
//
// layout(set = ???, binding = ???) buffer NumBlades {
// 	  uint vertexCount;   // Write the number of blades remaining here
// 	  uint instanceCount; // = 1
// 	  uint firstVertex;   // = 0
// 	  uint firstInstance; // = 0
// } numBlades;

bool inBounds(float value, float bounds) {
    return (value >= -bounds) && (value <= bounds);
}

void computeGeometryInfo(float3 up, float orientation, inout float3 tan, inout float3 bit, inout float3 facing) {
    float3 ref = (length(up - float3(1, 0, 0)) > EPSILON) ? float3(1, 0, 0) : float3(0, 0, 1);

    tan = cross(up, ref);
    bit = cross(tan, up);

    facing = normalize(cos(orientation) * tan + sin(orientation) * bit);
}

float4 windAt(float3 p, float t) {
    float3 dir = normalize(float3(1.f, 0.f, .5f));

    float along = dot(p, dir);
    float primary = sin(along * .25f - t * 2.f);
    float cross = .4f * sin(p.x * .6f + p.z * .35f - t * 3.1f);

    float strength = 4.f * (.6f + .4f * primary + cross * .5f);
    return float4(dir, strength);
}

//float rand() {
//    seed += uint2(1);
//    uint2 q = 1103515245U * ((seed >> 1U) ^ (seed.yx));
//    uint n = 1103515245U * ((q.x) ^ (q.y >> 3U));
//    return float(n) * (1.0 / float(0xffffffffU));
//}

#define LENIENCE .95f
bool oob(float4 v) {
    return v.x < -LENIENCE || v.y < -LENIENCE || v.x > LENIENCE || v.y > LENIENCE || v.z < 0.f;
}

[[shader("compute")]]
[numthreads(32, 1, 1)]
void main(uint3 gThreadIdx: SV_DispatchThreadID) {
	// Reset the number of blades to 0
	if (gThreadIdx.x == 0) {
		numBlades[0].vertexCount = 0;
	}
	AllMemoryBarrierWithGroupSync(); // Wait till all threads reach this point

    if (!inBounds(gThreadIdx.x, 1 << 13)) return;

    // TODO: Apply forces on every blade and update the vertices in the buffer
    float4 d = float4(0, -1, 0, 9.8f);

    Blade b = blades[gThreadIdx.x];
    float3 iv2 = (b.v0 + b.up * b.v1.w).xyz;
    float3 r = (iv2 - b.v2.xyz) * b.up.w;

    float3 ge = d.xyz * d.w;
    float3 tan, bit, facing;
    computeGeometryInfo(b.up.xyz, b.v0.w, tan, bit, facing);
    float3 gf = .25f * length(ge) * facing;
    float3 g = ge + gf;

    float4 wi = windAt(b.v0.xyz, time.totalTime);
    float fd = 1.f - abs(dot(normalize(wi.xyz * wi.w), normalize(b.v2.xyz - b.v0.xyz)));
    float fr = dot(b.v2.xyz - b.v0.xyz, b.up.xyz) / b.v1.w;

    float3 w = wi.xyz * wi.w * fd * fr;

    float3 newV2 = b.v2.xyz + (r + g + w) * time.deltaTime;

    // Validation time!
    newV2 -= b.up.xyz * min(dot(b.up.xyz, newV2 - b.v0.xyz), 0.f);
    float projLen = length(newV2 - b.v0.xyz - b.up.xyz * dot(newV2 - b.v0.xyz, b.up.xyz));
    float3 newV1 = b.v0.xyz + b.v1.w * b.up.xyz * max(1.f - projLen / b.v1.w, .05f * max(projLen / b.v1.w, 1.f));

    float l0 = distance(b.v0.xyz, newV2);
    float l1 = distance(b.v0.xyz, newV1) + distance(newV1, newV2);
    float l = (2.f * l0 + l1) / 3.f;

    // TODO: Cull blades that are too far away or not in the camera frustum and write them
    // to the culled blades buffer
    // Note: to do this, you will need to use an atomic operation to read and update numBlades.vertexCount
    // You want to write the visible blades to the buffer without write conflicts between threads
    Blade nBlade = {};
    nBlade.v0 = b.v0;
    nBlade.v1 = float4(b.v0.xyz + b.v1.w / l * (newV1 - b.v0.xyz), b.v1.w);
    nBlade.v2 = float4(nBlade.v1.xyz + b.v1.w / l * (newV2 - newV1), b.v2.w);
    nBlade.up = b.up;

    blades[gThreadIdx.x] = nBlade;

    //If it's too orthogonal to the camera, then cull
    if (abs(dot(facing, normalize(b.v0.xyz - float3(camera.pos.x, 0, camera.pos.z)))) < .2f) return;

    // View frustum culling
    float4 ssV0 = mul(camera.proj, mul(camera.view, float4(nBlade.v0.xyz, 1.f)));
    ssV0 /= ssV0.w;
    bool oob0 = oob(ssV0);

    float4 ssV2 = mul(camera.proj, mul(camera.view, float4(nBlade.v2.xyz, 1.f)));
    ssV2 /= ssV2.w;
    bool oob2 = oob(ssV2);

    float4 ssM = float4(.25f * nBlade.v0.xyz + .5f * nBlade.v1.xyz + .25f * nBlade.v2.xyz, 1.f);
    ssM = mul(camera.proj, mul(camera.view, ssM));
    ssM /= ssM.w;
    bool oobM = oob(ssM);

    if (oob0 && oob2 && oobM) return;

    // Distance cull
    int bucketNum = floor(max((distance(camera.pos.xyz, b.v0.xyz) - SAFE_DIST), 0.f) / (MAX_DIST / CULLING_BUCKETS));
    if ((gThreadIdx.x & ((1 << (int(CULLING_BUCKETS) + 1)) - 1)) >= (1 << (int(CULLING_BUCKETS) + 1 - bucketNum))) return;
    if (distance(camera.pos.xyz, b.v0.xyz) >= MAX_DIST) return;

    uint32_t bufIdx;
    InterlockedAdd(numBlades[0].vertexCount, 1, bufIdx);

    culledBlades[bufIdx] = nBlade;
}