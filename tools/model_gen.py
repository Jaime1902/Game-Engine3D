"""Genera los modelos glTF de ejemplo del proyecto (low poly, caras planas, colores por vertice).

Uso: python tools/model_gen.py projects/pueblo/Content [--force]

Los modelos miran hacia +Z (convencion glTF, la de Blender); el motor los gira al importarlos para que
miren hacia delante. Pies en y = 0, 1 unidad = 1 metro.
"""
import base64
import json
import math
import os
import struct
import sys
import zlib


class Mesh:
    def __init__(self):
        self.positions, self.normals, self.colors, self.uvs = [], [], [], []

    def triangle(self, a, b, c, color, uv=((0, 0), (1, 0), (1, 1))):
        ux, uy, uz = (b[i] - a[i] for i in range(3))
        vx, vy, vz = (c[i] - a[i] for i in range(3))
        n = (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)
        length = math.sqrt(sum(x * x for x in n)) or 1.0
        n = tuple(x / length for x in n)
        for point, texcoord in zip((a, b, c), uv):
            self.positions.append(point)
            self.normals.append(n)
            self.colors.append(color)
            self.uvs.append(texcoord)

    def quad(self, a, b, c, d, color, uv=((0, 1), (1, 1), (1, 0), (0, 0))):
        self.triangle(a, b, c, color, (uv[0], uv[1], uv[2]))
        self.triangle(a, c, d, color, (uv[0], uv[2], uv[3]))

    def box(self, center, size, color, top=None):
        cx, cy, cz = center
        hx, hy, hz = (s / 2 for s in size)
        p = [(cx + sx * hx, cy + sy * hy, cz + sz * hz) for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
        # indices: x(-,+) y(-,+) z(-,+) -> 0..7 en orden (sx, sy, sz)
        def v(sx, sy, sz):
            return p[(sx > 0) * 4 + (sy > 0) * 2 + (sz > 0)]
        self.quad(v(-1, -1, 1), v(1, -1, 1), v(1, 1, 1), v(-1, 1, 1), color)        # +Z
        self.quad(v(1, -1, -1), v(-1, -1, -1), v(-1, 1, -1), v(1, 1, -1), color)    # -Z
        self.quad(v(1, -1, 1), v(1, -1, -1), v(1, 1, -1), v(1, 1, 1), color)        # +X
        self.quad(v(-1, -1, -1), v(-1, -1, 1), v(-1, 1, 1), v(-1, 1, -1), color)    # -X
        self.quad(v(-1, 1, 1), v(1, 1, 1), v(1, 1, -1), v(-1, 1, -1), top or color)  # +Y
        self.quad(v(-1, -1, -1), v(1, -1, -1), v(1, -1, 1), v(-1, -1, 1), color)    # -Y

    def cylinder(self, center, radius, height, segments, color, cap=None, uv=False):
        cx, cy, cz = center
        for i in range(segments):
            a0, a1 = 2 * math.pi * i / segments, 2 * math.pi * (i + 1) / segments
            p0 = (cx + math.sin(a0) * radius, cy, cz + math.cos(a0) * radius)
            p1 = (cx + math.sin(a1) * radius, cy, cz + math.cos(a1) * radius)
            q0, q1 = (p0[0], cy + height, p0[2]), (p1[0], cy + height, p1[2])
            u0, u1 = i / segments, (i + 1) / segments
            self.quad(p0, p1, q1, q0, color, ((u0, 1), (u1, 1), (u1, 0), (u0, 0)) if uv else ((0, 1), (1, 1), (1, 0), (0, 0)))
            top, bottom = (cx, cy + height, cz), (cx, cy, cz)
            self.triangle(q0, q1, top, cap or color, ((0.5, 0.5),) * 3)
            self.triangle(p1, p0, bottom, cap or color, ((0.5, 0.5),) * 3)

    def cone(self, base_center, radius, height, segments, color):
        cx, cy, cz = base_center
        tip = (cx, cy + height, cz)
        for i in range(segments):
            a0, a1 = 2 * math.pi * i / segments, 2 * math.pi * (i + 1) / segments
            p0 = (cx + math.sin(a0) * radius, cy, cz + math.cos(a0) * radius)
            p1 = (cx + math.sin(a1) * radius, cy, cz + math.cos(a1) * radius)
            self.triangle(p0, p1, tip, color)
            self.triangle(p1, p0, (cx, cy, cz), color)


def rgb(hex_color):
    return tuple(int(hex_color[i:i + 2], 16) / 255 for i in (1, 3, 5)) + (1.0,)


def png(width, height, pixels):
    """PNG RGBA sin compresion externa (zlib de la biblioteca estandar)."""
    raw = b''.join(b'\x00' + bytes(pixels[y * width * 4:(y + 1) * width * 4]) for y in range(height))
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def write_gltf(path, mesh, texture=None):
    count = len(mesh.positions)
    data = bytearray()
    views, accessors = [], []

    def add(values, fmt, kind, components, minmax=False):
        offset = len(data)
        for value in values:
            data.extend(struct.pack('<' + fmt * components, *value))
        while len(data) % 4:
            data.append(0)
        views.append({'buffer': 0, 'byteOffset': offset, 'byteLength': count * 4 * components})
        accessor = {'bufferView': len(views) - 1, 'componentType': 5126, 'count': count, 'type': kind}
        if minmax:
            accessor['min'] = [min(v[i] for v in values) for i in range(components)]
            accessor['max'] = [max(v[i] for v in values) for i in range(components)]
        accessors.append(accessor)
        return len(accessors) - 1

    attributes = {
        'POSITION': add(mesh.positions, 'f', 'VEC3', 3, True),
        'NORMAL': add(mesh.normals, 'f', 'VEC3', 3),
        'COLOR_0': add(mesh.colors, 'f', 'VEC4', 4),
    }
    material = {'name': 'base', 'pbrMetallicRoughness': {'baseColorFactor': [1, 1, 1, 1], 'metallicFactor': 0}}
    gltf = {'asset': {'version': '2.0', 'generator': 'Game-Engine3D model_gen'}, 'scene': 0, 'scenes': [{'nodes': [0]}],
            'nodes': [{'name': os.path.splitext(os.path.basename(path))[0], 'mesh': 0}]}
    if texture:
        attributes['TEXCOORD_0'] = add(mesh.uvs, 'f', 'VEC2', 2)
        uri = 'data:image/png;base64,' + base64.b64encode(texture).decode()
        gltf['images'] = [{'uri': uri}]
        gltf['samplers'] = [{'magFilter': 9728, 'minFilter': 9728}]
        gltf['textures'] = [{'source': 0, 'sampler': 0}]
        material['pbrMetallicRoughness']['baseColorTexture'] = {'index': 0}
    gltf['materials'] = [material]
    gltf['meshes'] = [{'primitives': [{'attributes': attributes, 'material': 0}]}]
    gltf['buffers'] = [{'byteLength': len(data), 'uri': 'data:application/octet-stream;base64,' + base64.b64encode(bytes(data)).decode()}]
    gltf['bufferViews'] = views
    gltf['accessors'] = accessors
    with open(path, 'w', encoding='utf-8') as file:
        json.dump(gltf, file)


def villager():
    m = Mesh()
    skin, shirt, pants, boots, hair = rgb('#e0ac7e'), rgb('#3f7fbf'), rgb('#5a4630'), rgb('#3b2a1e'), rgb('#4a2f1a')
    m.box((-0.12, 0.08, 0.02), (0.16, 0.16, 0.26), boots)
    m.box((0.12, 0.08, 0.02), (0.16, 0.16, 0.26), boots)
    m.box((-0.12, 0.5, 0), (0.16, 0.68, 0.18), pants)
    m.box((0.12, 0.5, 0), (0.16, 0.68, 0.18), pants)
    m.box((0, 1.12, 0), (0.48, 0.58, 0.26), shirt)
    m.box((-0.32, 1.12, 0), (0.13, 0.56, 0.14), shirt)
    m.box((0.32, 1.12, 0), (0.13, 0.56, 0.14), shirt)
    m.box((-0.32, 0.8, 0), (0.12, 0.1, 0.12), skin)
    m.box((0.32, 0.8, 0), (0.12, 0.1, 0.12), skin)
    m.box((0, 1.5, 0), (0.32, 0.34, 0.3), skin, top=hair)
    m.box((0, 1.62, -0.03), (0.34, 0.12, 0.3), hair)
    # Ojos y nariz en la cara delantera (+Z).
    m.box((-0.07, 1.52, 0.155), (0.05, 0.05, 0.02), rgb('#202020'))
    m.box((0.07, 1.52, 0.155), (0.05, 0.05, 0.02), rgb('#202020'))
    m.box((0, 1.46, 0.16), (0.05, 0.06, 0.04), rgb('#d08f62'))
    return m


def tree():
    m = Mesh()
    m.cylinder((0, 0, 0), 0.18, 1.4, 6, rgb('#6b4a2b'))
    m.cone((0, 1.1, 0), 1.0, 1.5, 7, rgb('#3e8b3a'))
    m.cone((0, 1.9, 0), 0.75, 1.3, 7, rgb('#4ea34a'))
    m.cone((0, 2.6, 0), 0.45, 0.9, 7, rgb('#5cb556'))
    return m


def house():
    m = Mesh()
    wall, roof, door, window, base = rgb('#d9cfb4'), rgb('#a8452f'), rgb('#6b4a2b'), rgb('#6fa8d6'), rgb('#8c8c8c')
    m.box((0, 0.1, 0), (4.2, 0.2, 3.2), base)
    m.box((0, 1.4, 0), (4, 2.4, 3), wall)
    # Tejado a dos aguas (cumbrera en X).
    a, b, c, d = (-2.3, 2.6, -1.75), (2.3, 2.6, -1.75), (2.3, 2.6, 1.75), (-2.3, 2.6, 1.75)
    top0, top1 = (-2.3, 3.8, 0), (2.3, 3.8, 0)
    m.quad(d, c, top1, top0, roof)
    m.quad(b, a, top0, top1, roof)
    m.triangle((-2.0, 2.6, -1.5), (-2.0, 2.6, 1.5), (-2.0, 3.7, 0), wall)
    m.triangle((2.0, 2.6, 1.5), (2.0, 2.6, -1.5), (2.0, 3.7, 0), wall)
    m.quad((-2.3, 2.6, -1.75), (2.3, 2.6, -1.75), (2.3, 2.6, 1.75), (-2.3, 2.6, 1.75), roof)
    m.box((0, 1.0, 1.52), (0.9, 1.8, 0.06), door)
    m.box((-1.3, 1.6, 1.52), (0.7, 0.6, 0.05), window)
    m.box((1.3, 1.6, 1.52), (0.7, 0.6, 0.05), window)
    m.box((1.2, 3.4, -0.6), (0.4, 0.9, 0.4), rgb('#7a7a7a'))
    return m


def barrel():
    m = Mesh()
    m.cylinder((0, 0, 0), 0.4, 1.0, 10, (1, 1, 1, 1), cap=rgb('#8a6239'), uv=True)
    # Textura de 16 x 16: duelas de madera con dos aros de hierro.
    size, pixels = 16, []
    for y in range(size):
        for x in range(size):
            iron = y in (2, 3, 12, 13)
            seam = x % 4 == 0
            if iron:
                r, g, b = (70, 70, 78) if y in (2, 12) else (95, 95, 105)
            else:
                shade = 0.8 if seam else 1.0 - 0.04 * ((x * 7 + y * 3) % 3)
                r, g, b = int(150 * shade), int(102 * shade), int(58 * shade)
            pixels += [r, g, b, 255]
    return m, png(size, size, pixels)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    folder = os.path.join(sys.argv[1], 'models')
    force = '--force' in sys.argv
    os.makedirs(folder, exist_ok=True)
    barrel_mesh, barrel_texture = barrel()
    models = {'aldeano.gltf': (villager(), None), 'arbol.gltf': (tree(), None), 'casita.gltf': (house(), None),
              'barril.gltf': (barrel_mesh, barrel_texture)}
    written = 0
    for name, (mesh, texture) in models.items():
        path = os.path.join(folder, name)
        if os.path.exists(path) and not force:
            continue
        write_gltf(path, mesh, texture)
        written += 1
    print(f'{written} modelos escritos en {folder}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
