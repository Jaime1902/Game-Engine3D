// transform: matriz completa (cámara * modelo).
// fade:      xy = centro del desvanecimiento en coordenadas locales, z = radio local (0 = sin desvanecer).
// grid:      xy = desplazamiento en el mundo (x, z), z = separación entre líneas, w = 1 si es la cuadrícula.
struct ScenePush {
    float4x4 transform;
    float4 fade;
    float4 grid;
};

[[vk::push_constant]] ConstantBuffer<ScenePush> scene;

struct Vertex {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 color : COLOR;
};

struct Pixel {
    float4 position : SV_Position;
    float3 color : COLOR;
    float2 local : TEXCOORD0;
};

Pixel vertexMain(Vertex input) {
    Pixel output;
    output.position = mul(scene.transform, float4(input.position, 1.0));
    output.color = input.color;
    output.local = input.position.xz;
    return output;
}

// Umbrales de tramado ordenado 4x4 (Bayer). Desvanecer con tramado conserva píxeles nítidos
// y colores sólidos, en lugar de mezclas semitransparentes que ensucian la paleta.
static const float bayer[16] = {
     0.0,  8.0,  2.0, 10.0,
    12.0,  4.0, 14.0,  6.0,
     3.0, 11.0,  1.0,  9.0,
    15.0,  7.0, 13.0,  5.0
};

float4 fragmentMain(Pixel input) : SV_Target {
    float3 color = input.color;
    if (scene.grid.w > 0.5) {
        // Ejes del mundo: rojo para X (z = 0) y azul para Z (x = 0).
        float2 world = input.local * scene.grid.z + scene.grid.xy;
        float epsilon = scene.grid.z * 0.02;
        if (abs(world.y) < epsilon) color = float3(0.72, 0.30, 0.31);
        else if (abs(world.x) < epsilon) color = float3(0.27, 0.52, 0.76);
    }
    if (scene.fade.z > 0.0) {
        float distance = length(input.local - scene.fade.xy);
        float alpha = saturate(1.0 - distance / scene.fade.z);
        alpha = alpha * alpha;
        uint2 pixel = uint2(input.position.xy) % 4;
        float threshold = (bayer[pixel.y * 4 + pixel.x] + 0.5) / 16.0;
        if (alpha < threshold) discard;
    }
    return float4(color, 1.0);
}
