struct ScenePush {
    float4x4 transform;
};

[[vk::push_constant]] ConstantBuffer<ScenePush> scene;

struct Vertex {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 color : COLOR;
};

struct Pixel {
    float4 position : SV_Position;
    float3 color : COLOR;
};

Pixel vertexMain(Vertex input) {
    Pixel output;
    output.position = mul(scene.transform, float4(input.position, 1.0));
    output.color = input.color;
    return output;
}

float4 fragmentMain(Pixel input) : SV_Target {
    return float4(input.color, 1.0);
}
