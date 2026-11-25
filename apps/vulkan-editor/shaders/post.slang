// Pasada de estilo pixel art sobre la imagen de la escena (a su resolución interna, píxel a píxel).
//
//  - Contorno de silueta: un píxel con un vecino bastante más lejano se oscurece. Dibuja el borde
//    exterior de cada objeto con una línea de un píxel, como en el pixel art hecho a mano.
//  - Arista interior: donde la normal cambia bruscamente (esquinas de cajas, bordes de vóxeles,
//    crestas del terreno) el lado más cercano se aclara: resalta el volumen sin líneas negras.
//  - Reducción de colores opcional por canal, con tramado ordenado.
//
// Descriptores (set 0): 0 color, 1 normal (a = 1 si admite contorno), 2 profundidad.

[[vk::binding(0, 0)]] Sampler2D sceneColor;
[[vk::binding(1, 0)]] Sampler2D sceneNormal;
[[vk::binding(2, 0)]] Sampler2D sceneDepth;

struct PostPush {
    float4 outline;    // x oscurecer silueta (0-1), y aclarar aristas (0-1), z umbral de silueta, w umbral de arista
    float4 camera;     // x plano cercano, y plano lejano, z niveles por canal (0 = sin reducir), w tramado (0-1)
    float4 extent;     // xy tamaño de la zona dibujada en píxeles (la imagen puede ser mayor)
};
[[vk::push_constant]] ConstantBuffer<PostPush> post;

struct PostPixel {
    float4 position : SV_Position;
};

PostPixel postVertexMain(uint id : SV_VertexID) {
    // Triángulo que cubre la pantalla.
    PostPixel output;
    float2 uv = float2(float((id << 1) & 2u), float(id & 2u));
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

static const float bayer[16] = {
     0.0,  8.0,  2.0, 10.0,
    12.0,  4.0, 14.0,  6.0,
     3.0, 11.0,  1.0,  9.0,
    15.0,  7.0, 13.0,  5.0
};

float linearDepth(int2 p) {
    float d = sceneDepth.Load(int3(p, 0)).r;
    float n = post.camera.x;
    float f = post.camera.y;
    // La matriz de la cámara lleva z de OpenGL [-1, 1] a [0, 1].
    float ndc = d * 2.0 - 1.0;
    return 2.0 * n * f / (f + n - ndc * (f - n));
}

float3 normalAt(int2 p) {
    return sceneNormal.Load(int3(p, 0)).xyz * 2.0 - 1.0;
}

float4 postFragmentMain(PostPixel input) : SV_Target {
    int2 p = int2(input.position.xy);
    float3 color = sceneColor.Load(int3(p, 0)).rgb;
    float4 normalData = sceneNormal.Load(int3(p, 0));
    if (normalData.a > 0.5) {
        float depth = linearDepth(p);
        float3 normal = normalData.xyz * 2.0 - 1.0;
        int2 offsets[4] = { int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1) };
        float silhouette = 0.0;
        float crease = 0.0;
        [unroll]
        for (int i = 0; i < 4; ++i) {
            int2 q = clamp(p + offsets[i], int2(0, 0), int2(post.extent.xy) - 1);
            float neighborDepth = linearDepth(q);
            // Diferencia relativa: la misma regla vale cerca y lejos de la cámara.
            float difference = (neighborDepth - depth) / depth;
            silhouette += saturate(difference - post.outline.z) * 8.0;
            if (sceneNormal.Load(int3(q, 0)).a > 0.5) {
                float3 neighborNormal = normalAt(q);
                // Solo un lado de la arista se aclara (el que no está detrás) para que la línea mida un píxel.
                float side = smoothstep(-0.01, 0.01, dot(normal - neighborNormal, float3(1.0, 1.0, 1.0)));
                float notBehind = difference > -0.01 ? 1.0 : 0.0;
                crease += (1.0 - dot(normal, neighborNormal)) * side * notBehind;
            }
        }
        if (silhouette > 0.5) color *= 1.0 - post.outline.x;
        else if (crease > post.outline.w) color *= 1.0 + post.outline.y;
    }
    float levels = post.camera.z;
    if (levels > 1.5) {
        uint2 cell = uint2(p) & 3u;
        float threshold = (bayer[cell.y * 4 + cell.x] + 0.5) / 16.0 - 0.5;
        color = floor(saturate(color) * (levels - 1.0) + 0.5 + threshold * post.camera.w) / (levels - 1.0);
    }
    return float4(saturate(color), 1.0);
}
