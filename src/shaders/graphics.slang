struct CameraBufferObject {
    float4x4 view;
    float4x4 proj;
};

struct VSInput {
    [[vk::location(0)]] float3 pos;
    [[vk::location(1)]] float3 color;
    [[vk::location(2)]] float2 uv;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color;
    [[vk::location(1)]] float2 uv;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraBufferObject> camera;

[[vk::binding(0, 1)]]
ConstantBuffer<float4x4> model;

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output = {};
    output.position = mul(camera.proj, mul(camera.view, mul(model, float4(input.pos, 1.f))));
    output.color = input.color;
    output.uv = input.uv;

    return output;
}

[[vk::binding(1, 1)]]
Sampler2D texSampler;

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_Target {
    return texSampler.Sample(input.uv);
}