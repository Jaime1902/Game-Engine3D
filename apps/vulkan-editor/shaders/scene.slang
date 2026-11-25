// Shader de la escena: piezas (cajas y rampas), cuadrícula del suelo, terreno de vóxeles y terreno de malla.
//
// Descriptores (set 0):
//   binding 0  FrameData: cámara, sol, estilo y tabla de materiales (capas de textura por orientación y tinte).
//   binding 1  Texture2DArray con todas las texturas del catálogo, filtro nearest (pixel art).
//
// Push constants (128 bytes):
//   model  matriz del objeto (vóxeles: transformación del terreno * tamaño del vóxel)
//   fade   cuadrícula: xy centro del desvanecimiento en coordenadas locales, z radio
//   grid   cuadrícula: xy desplazamiento en el mundo, z separación;  w = modo
//          (1 cuadrícula, 2 pieza, 3 vóxeles, 4 terreno de malla)
//   data   pieza: 6 materiales de 16 bits empaquetados en x, y, z;  vóxeles: origen del chunk (x, y, z)
//          terreno de malla: xy origen del chunk en celdas, zw materiales de las 4 capas (16 bits cada uno)
//   extra  x > 0: resaltar (mezcla con blanco);  y: metros por celda del terreno de malla
//
// Salidas: color y normal del mundo (para los contornos de la pasada de post). normal.a = 0 en lo que
// no debe tener contorno (la cuadrícula).

struct GpuMaterial {
    uint top;
    uint side;
    uint bottom;
    uint tint;     // 0xBBGGRR
};

struct FrameData {
    float4x4 viewProj;
    float4 sun;        // xyz dirección hacia el sol (normalizada)
    float4 style;      // x bandas de luz (0 = continua), y texeles por repetición de textura
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

static const float modeGrid = 1.0;
static const float modePiece = 2.0;
static const float modeVoxel = 3.0;
static const float modeHeightmap = 4.0;

struct Vertex {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 color : COLOR;      // cuadrícula: color; pieza: x = número de cara
    [[vk::location(2)]] float3 normal : NORMAL;
};

struct VoxelVertex {
    [[vk::location(0)]] uint2 packed : POSITION;   // x: posición y normal; y: material
};

struct HeightVertex {
    [[vk::location(0)]] float height : POSITION;
    [[vk::location(1)]] uint normal : NORMAL;      // 10 bits por eje
    [[vk::location(2)]] float4 weights : COLOR;    // peso de cada capa (0..1)
    [[vk::location(3)]] uint cell : TEXCOORD;      // x | z << 16 dentro del chunk
};

struct Pixel {
    float4 position : SV_Position;
    float4 color : COLOR;          // rgb tinte (o color plano); w: 0 plano, 1 con textura, 2 terreno de malla
    float3 texcoord : TEXCOORD0;   // uv en repeticiones de textura y capa del array
    float2 local : TEXCOORD1;      // cuadrícula: coordenadas locales xz
    float3 normal : TEXCOORD2;     // normal del mundo
    float4 weights : TEXCOORD3;    // terreno de malla: pesos de las capas
    float3 meters : TEXCOORD4;     // terreno de malla: posición local en metros (para las UV)
};

struct Target {
    float4 color : SV_Target0;
    float4 normal : SV_Target1;
};

float3 unpackColor(uint packed) {
    return float3(float(packed & 0xFFu), float((packed >> 8) & 0xFFu), float((packed >> 16) & 0xFFu)) / 255.0;
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

Pixel emptyPixel() {
    Pixel output;
    output.position = float4(0.0, 0.0, 0.0, 1.0);
    output.color = float4(0.0, 0.0, 0.0, 0.0);
    output.texcoord = float3(0.0, 0.0, 0.0);
    output.local = float2(0.0, 0.0);
    output.normal = float3(0.0, 1.0, 0.0);
    output.weights = float4(1.0, 0.0, 0.0, 0.0);
    output.meters = float3(0.0, 0.0, 0.0);
    return output;
}

float3 worldNormalOf(float3 normal) {
    // La inversa transpuesta respeta las escalas no uniformes (una caja estirada sigue bien iluminada).
    float3x3 m = (float3x3)scene.model;
    float3x3 cofactor = float3x3(cross(m[1], m[2]), cross(m[2], m[0]), cross(m[0], m[1]));
    return normalize(mul(cofactor, normal));
}

Pixel vertexMain(Vertex input) {
    Pixel output = emptyPixel();
    float4 world = mul(scene.model, float4(input.position, 1.0));
    output.position = mul(frame.viewProj, world);
    output.color = float4(input.color, 0.0);
    output.local = input.position.xz;
    if (scene.grid.w > 1.5) {
        // Pieza: material de la cara (6 valores de 16 bits en data.xyz, leídos sin índice variable).
        uint face = uint(input.color.x + 0.5);
        uint pair = face < 2u ? scene.data.x : (face < 4u ? scene.data.y : scene.data.z);
        uint index = (face & 1u) != 0u ? (pair >> 16) : (pair & 0xFFFFu);
        GpuMaterial material = loadMaterial(index);
        float3 worldNormal = worldNormalOf(input.normal);
        // Escala del objeto: la textura mantiene su tamaño en metros aunque se estire la pieza.
        // model[i] es una fila (convención HLSL): la escala de cada eje es la longitud de su columna.
        float4x4 m = scene.model;
        float3 scale = float3(length(float3(m[0][0], m[1][0], m[2][0])), length(float3(m[0][1], m[1][1], m[2][1])),
                              length(float3(m[0][2], m[1][2], m[2][2])));
        float3 meters = input.position * scale;
        output.texcoord = float3(planarUv(meters, input.normal), float(materialLayer(material, worldNormal)));
        output.color = float4(unpackColor(material.tint), 1.0);
        output.normal = worldNormal;
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
    Pixel output = emptyPixel();
    uint p = input.packed.x;
    float3 local = float3(float(p & 63u), float((p >> 6) & 63u), float((p >> 12) & 63u));
    float3 voxel = local + float3(int3(scene.data.xyz));
    float3 normal = voxelNormal((p >> 18) & 7u);
    float4 world = mul(scene.model, float4(voxel, 1.0));
    output.position = mul(frame.viewProj, world);
    GpuMaterial material = loadMaterial(input.packed.y & 0xFFFFu);
    float3 worldNormal = worldNormalOf(normal);
    // Un vóxel = una repetición de la textura, así que los quads fusionados la repiten sin estirarla.
    output.texcoord = float3(planarUv(voxel, normal), float(materialLayer(material, worldNormal)));
    output.color = float4(unpackColor(material.tint), 1.0);
    output.normal = worldNormal;
    return output;
}

Pixel heightVertexMain(HeightVertex input) {
    Pixel output = emptyPixel();
    float cellSize = scene.extra.y;
    float2 cell = float2(float(input.cell & 0xFFFFu), float(input.cell >> 16)) + float2(scene.data.xy);
    float3 meters = float3(cell.x * cellSize, input.height, cell.y * cellSize);
    float4 world = mul(scene.model, float4(meters, 1.0));
    output.position = mul(frame.viewProj, world);
    float3 normal = float3(float(input.normal & 1023u), float((input.normal >> 10) & 1023u),
                           float((input.normal >> 20) & 1023u)) / 1023.0 * 2.0 - 1.0;
    output.normal = worldNormalOf(normal);
    output.weights = input.weights;
    output.meters = meters;
    output.color = float4(1.0, 1.0, 1.0, 2.0);
    return output;
}

// Umbrales de tramado ordenado 4x4 (Bayer). Tramar en lugar de mezclar conserva píxeles nítidos
// y colores sólidos, en lugar de mezclas semitransparentes que ensucian la paleta.
static const float bayer[16] = {
     0.0,  8.0,  2.0, 10.0,
    12.0,  4.0, 14.0,  6.0,
     3.0, 11.0,  1.0,  9.0,
    15.0,  7.0, 13.0,  5.0
};

float bayerThreshold(int2 cell) {
    uint2 p = uint2(cell & 3);
    return (bayer[p.y * 4 + p.x] + 0.5) / 16.0;
}

// Luz por bandas: el sol se cuantiza en pocos escalones (sombra, media luz, luz) como en el pixel art
// dibujado a mano. La sombra se tiñe de azul y la luz de cálido en lugar de oscurecer a gris.
float3 lighting(float3 worldNormal) {
    float diffuse = saturate(dot(worldNormal, frame.sun.xyz));
    float bands = frame.style.x;
    if (bands > 0.5) diffuse = min(floor(diffuse * bands + 0.5) / bands, 1.0);
    float3 shadow = float3(0.50, 0.53, 0.66);
    float3 light = float3(1.04, 1.0, 0.92);
    return lerp(shadow, light, diffuse);
}

Target fragmentMain(Pixel input) {
    Target output;
    float3 color = input.color.rgb;
    float3 normal = normalize(input.normal);
    float edge = 1.0;
    if (input.color.w > 1.5) {
        // Terreno de malla: cada texel de la textura toma una sola capa según su peso, con un umbral
        // de tramado anclado a la textura (no a la pantalla): la transición no "nada" al mover la cámara.
        float2 uv = planarUv(input.meters, normal);
        float texels = max(frame.style.y, 1.0);
        float threshold = bayerThreshold(int2(floor(uv * texels)));
        float4 w = input.weights / max(dot(input.weights, float4(1.0, 1.0, 1.0, 1.0)), 0.001);
        uint layer = 3u;
        if (threshold < w.x) layer = 0u;
        else if (threshold < w.x + w.y) layer = 1u;
        else if (threshold < w.x + w.y + w.z) layer = 2u;
        uint pair = layer < 2u ? scene.data.z : scene.data.w;
        uint index = (layer & 1u) != 0u ? (pair >> 16) : (pair & 0xFFFFu);
        GpuMaterial material = loadMaterial(index);
        // Las laderas usan la textura de abajo (en el césped, la tierra).
        uint textureLayer = normal.y > 0.62 ? material.top : material.bottom;
        color = unpackColor(material.tint) * textures.Sample(float3(uv, float(textureLayer))).rgb;
        color *= lighting(normal);
    } else if (input.color.w > 0.5) {
        color *= textures.Sample(input.texcoord).rgb;
        color *= lighting(normal);
    } else {
        edge = 0.0;
    }
    if (abs(scene.grid.w - modeGrid) < 0.5) {
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
        if (alpha < bayerThreshold(int2(input.position.xy))) discard;
    }
    if (scene.extra.x > 0.0) color = lerp(color, float3(1.0, 1.0, 1.0), scene.extra.x);
    output.color = float4(color, 1.0);
    output.normal = float4(normal * 0.5 + 0.5, edge);
    return output;
}
