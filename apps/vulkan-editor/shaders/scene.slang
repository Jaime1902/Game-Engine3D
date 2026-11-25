// Shader de la escena: piezas (cajas y rampas), cuadrícula del suelo y terreno de vóxeles.
//
// Descriptores (set 0):
//   binding 0  FrameData: cámara, sol y tabla de materiales (capas de textura por orientación y tinte).
//   binding 1  Texture2DArray con todas las texturas del catálogo, filtro nearest (pixel art).
//
// Push constants (128 bytes):
//   model  matriz del objeto (en vóxeles: transformación del terreno * tamaño del vóxel)
//   fade   cuadrícula: xy centro del desvanecimiento en coordenadas locales, z radio
//   grid   cuadrícula: xy desplazamiento en el mundo, z separación;  w = modo
//          (1 cuadrícula, 2 pieza, 3 vóxeles)
//   data   pieza: 6 materiales de 16 bits empaquetados en x, y, z;  vóxeles: origen del chunk (x, y, z)
//   extra  x > 0: resaltar (mezcla con blanco)

struct GpuMaterial {
    uint top;
    uint side;
    uint bottom;
    uint tint;     // 0xBBGGRR
};

struct FrameData {
    float4x4 viewProj;
    float4 sun;    // xyz dirección hacia el sol (normalizada)
    GpuMaterial materials[256];
};

struct ScenePush {
    float4x4 model;
    float4 fade;
    float4 grid;
    uint4 data;
    float4 extra;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] Sampler2DArray textures;
[[vk::push_constant]] ConstantBuffer<ScenePush> scene;

struct Vertex {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 color : COLOR;      // cuadrícula: color; pieza: x = número de cara
    [[vk::location(2)]] float3 normal : NORMAL;
};

struct VoxelVertex {
    [[vk::location(0)]] uint2 packed : POSITION;   // x: posición y normal; y: material
};

struct Pixel {
    float4 position : SV_Position;
    float4 color : COLOR;          // rgb luz * tinte (o color plano); w = 1 si lleva textura
    float3 texcoord : TEXCOORD0;   // uv en texeles de repetición y capa del array
    float2 local : TEXCOORD1;      // cuadrícula: coordenadas locales xz
};

float3 unpackColor(uint packed) {
    return float3(float(packed & 0xFFu), float((packed >> 8) & 0xFFu), float((packed >> 16) & 0xFFu)) / 255.0;
}

// Luz simple por orientación: el sol da de lleno arriba y en diagonal; las caras opuestas quedan en sombra.
float lighting(float3 worldNormal) {
    return 0.55 + 0.45 * saturate(dot(worldNormal, frame.sun.xyz));
}

// Proyección plana según el eje dominante de la normal (en metros del objeto). v crece hacia abajo
// en la textura, así que en las caras verticales se usa -y para que la textura quede derecha.
float2 planarUv(float3 p, float3 n) {
    float3 a = abs(n);
    if (a.y >= a.x && a.y >= a.z) return float2(p.x, p.z);
    if (a.x >= a.z) return float2(p.z * sign(n.x), -p.y);
    return float2(-p.x * sign(n.z), -p.y);
}

// Capa de textura según la orientación de la cara en el mundo.
uint materialLayer(GpuMaterial material, float3 worldNormal) {
    if (worldNormal.y > 0.5) return material.top;
    if (worldNormal.y < -0.5) return material.bottom;
    return material.side;
}

GpuMaterial loadMaterial(uint index) {
    return frame.materials[min(index, 255u)];
}

Pixel vertexMain(Vertex input) {
    Pixel output;
    float4 world = mul(scene.model, float4(input.position, 1.0));
    output.position = mul(frame.viewProj, world);
    output.color = float4(input.color, 0.0);
    output.texcoord = float3(0.0, 0.0, 0.0);
    output.local = input.position.xz;
    if (scene.grid.w > 1.5) {
        // Pieza: material de la cara (6 valores de 16 bits en data.xyz, leídos sin índice variable).
        uint face = uint(input.color.x + 0.5);
        uint pair = face < 2u ? scene.data.x : (face < 4u ? scene.data.y : scene.data.z);
        uint index = (face & 1u) != 0u ? (pair >> 16) : (pair & 0xFFFFu);
        GpuMaterial material = loadMaterial(index);
        float3 worldNormal = normalize(mul((float3x3)scene.model, input.normal));
        // Escala del objeto: la textura mantiene su tamaño en metros aunque se estire la pieza.
        // model[i] es una fila (convención HLSL): la escala de cada eje es la longitud de su columna.
        float4x4 m = scene.model;
        float3 scale = float3(length(float3(m[0][0], m[1][0], m[2][0])), length(float3(m[0][1], m[1][1], m[2][1])),
                              length(float3(m[0][2], m[1][2], m[2][2])));
        float3 meters = input.position * scale;
        output.texcoord = float3(planarUv(meters, input.normal), float(materialLayer(material, worldNormal)));
        output.color = float4(unpackColor(material.tint) * lighting(worldNormal), 1.0);
    }
    return output;
}

float3 voxelNormal(uint index) {
    float3 n = float3(-1.0, 0.0, 0.0);
    if (index == 1u) n = float3(1.0, 0.0, 0.0);
    if (index == 2u) n = float3(0.0, -1.0, 0.0);
    if (index == 3u) n = float3(0.0, 1.0, 0.0);
    if (index == 4u) n = float3(0.0, 0.0, -1.0);
    if (index == 5u) n = float3(0.0, 0.0, 1.0);
    return n;
}

Pixel voxelVertexMain(VoxelVertex input) {
    Pixel output;
    uint p = input.packed.x;
    float3 local = float3(float(p & 63u), float((p >> 6) & 63u), float((p >> 12) & 63u));
    float3 voxel = local + float3(int3(scene.data.xyz));
    float3 normal = voxelNormal((p >> 18) & 7u);
    float4 world = mul(scene.model, float4(voxel, 1.0));
    output.position = mul(frame.viewProj, world);
    GpuMaterial material = loadMaterial(input.packed.y & 0xFFFFu);
    float3 worldNormal = normalize(mul((float3x3)scene.model, normal));
    // Un vóxel = una repetición de la textura, así que los quads fusionados la repiten sin estirarla.
    output.texcoord = float3(planarUv(voxel, normal), float(materialLayer(material, worldNormal)));
    output.color = float4(unpackColor(material.tint) * lighting(worldNormal), 1.0);
    output.local = float2(0.0, 0.0);
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
    float3 color = input.color.rgb;
    if (input.color.w > 0.5) color *= textures.Sample(input.texcoord).rgb;
    if (abs(scene.grid.w - 1.0) < 0.5) {
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
    if (scene.extra.x > 0.0) color = lerp(color, float3(1.0, 1.0, 1.0), scene.extra.x);
    return float4(color, 1.0);
}
