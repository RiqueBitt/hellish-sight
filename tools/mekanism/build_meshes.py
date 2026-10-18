#!/usr/bin/env python3
"""Hellish Sight: converts Mekanism's free-form models (OBJ files and item models with elements) into the
engine's mesh format (res/mesh/*.json + textures) - see Minecraft.World/HSMesh.h.

  - MekaSuit (mekasuit.obj): one part per OBJ object, worn as armour (registerArmorModel)
  - Atomic Disassembler (item model elements): item model (registerItemModel)
  - Wind Generator (composite block model + wind_generator.obj + spinning wind_generator_blades.obj)

usage: build_meshes.py <Mekanism-26.3 root> <out/Mekanism>
"""
import json, math, os, re, sys
from PIL import Image

SRC, OUT = sys.argv[1], sys.argv[2]
MESH = os.path.join(OUT, 'res', 'mesh')
os.makedirs(MESH, exist_ok=True)
ROOTS = [os.path.join(SRC, 'src', d, 'resources', 'assets') for d in ('main', 'generators', 'additions', 'tools', 'defense')]

def asset(ns, kind, rel, ext):
    for r in ROOTS:
        p = os.path.join(r, ns, kind, rel + ext)
        if os.path.exists(p):
            return p
    raise FileNotFoundError('%s:%s/%s%s' % (ns, kind, rel, ext))

def ref_path(ref, kind='textures', ext='.png'):
    ns, rel = ref.split(':', 1) if ':' in ref else ('minecraft', ref)
    return asset(ns, kind, rel, ext)

def save_tex(ref, name):
    im = Image.open(ref_path(ref)).convert('RGBA')
    w, h = im.size
    if h > w and h % w == 0:
        im = im.crop((0, 0, w, w))      # animated strip: first frame
    im.save(os.path.join(MESH, name + '.png'))
    return 'res/mesh/%s.png' % name

# ---------------------------------------------------------------------------------------------------
def rot_matrix(rx, ry, rz):
    """X, then Y, then Z (degrees)"""
    def mx(a):
        c, s = math.cos(a), math.sin(a); return [[1, 0, 0], [0, c, -s], [0, s, c]]
    def my(a):
        c, s = math.cos(a), math.sin(a); return [[c, 0, s], [0, 1, 0], [-s, 0, c]]
    def mz(a):
        c, s = math.cos(a), math.sin(a); return [[c, -s, 0], [s, c, 0], [0, 0, 1]]
    def mul(a, b):
        return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
    return mul(mz(math.radians(rz)), mul(my(math.radians(ry)), mx(math.radians(rx))))

CORNERS = {   # same corner order as HSModel / TileRenderer (u0 for 0,1; v0 for 0,3)
    'down':  [(0,0,1),(0,0,0),(1,0,0),(1,0,1)],
    'up':    [(0,1,0),(0,1,1),(1,1,1),(1,1,0)],
    'north': [(1,1,0),(1,0,0),(0,0,0),(0,1,0)],
    'south': [(0,1,1),(0,0,1),(1,0,1),(1,1,1)],
    'west':  [(0,1,0),(0,0,0),(0,0,1),(0,1,1)],
    'east':  [(1,1,1),(1,0,1),(1,0,0),(1,1,0)],
}

def default_uv(face, fr, to):
    if face == 'down':  return [fr[0], 16 - to[2], to[0], 16 - fr[2]]
    if face == 'up':    return [fr[0], fr[2], to[0], to[2]]
    if face == 'north': return [16 - to[0], 16 - to[1], 16 - fr[0], 16 - fr[1]]
    if face == 'south': return [fr[0], 16 - to[1], to[0], 16 - fr[1]]
    if face == 'west':  return [fr[2], 16 - to[1], to[2], 16 - fr[1]]
    return [16 - to[2], 16 - to[1], 16 - fr[2], 16 - fr[1]]

def element_quads(elements, tex_of, offset=(0, 0, 0)):
    """block-model elements -> mesh quads [tex, emissive, x,y,z,u,v * 4] in blocks"""
    quads = []
    for e in elements:
        fr, to = e['from'], e['to']
        rot = e.get('rotation')
        m = None
        origin = [8, 8, 8]
        if rot:
            origin = rot.get('origin', [8, 8, 8])
            if 'axis' in rot:
                a = rot.get('angle', 0)
                m = rot_matrix(a if rot['axis'] == 'x' else 0, a if rot['axis'] == 'y' else 0, a if rot['axis'] == 'z' else 0)
            else:
                m = rot_matrix(rot.get('x', 0), rot.get('y', 0), rot.get('z', 0))
        emissive = 1 if e.get('light_emission') else 0
        for fname, f in e.get('faces', {}).items():
            t = tex_of(f.get('texture', ''))
            if t is None:
                continue
            uv = f.get('uv', default_uv(fname, fr, to))
            r = (f.get('rotation', 0) // 90) % 4
            q = [t, emissive]
            for c in range(4):
                p = [to[k] if CORNERS[fname][c][k] else fr[k] for k in range(3)]
                if m:
                    v = [p[k] - origin[k] for k in range(3)]
                    p = [sum(m[i][k] * v[k] for k in range(3)) + origin[i] for i in range(3)]
                s = (c + r) % 4
                u = uv[0] if s in (0, 1) else uv[2]
                vv = uv[1] if s in (0, 3) else uv[3]
                q += [round(p[0] / 16 + offset[0], 5), round(p[1] / 16 + offset[1], 5), round(p[2] / 16 + offset[2], 5), round(u / 16, 5), round(vv / 16, 5)]
            quads.append(q)
    return quads

def obj_parts(path, mat_tex, offset=(0, 0, 0), emissive_suffix='_led'):
    """OBJ -> {object name: quads}; mat_tex maps material -> texture index; v flipped (flip_v)"""
    verts, uvs, parts, cur, mat = [], [], {}, 'main', None
    for line in open(path, encoding='utf-8'):
        p = line.split()
        if not p:
            continue
        if p[0] == 'v':
            verts.append([float(x) for x in p[1:4]])
        elif p[0] == 'vt':
            uvs.append([float(p[1]), float(p[2])])
        elif p[0] == 'o' or p[0] == 'g':
            cur = p[1]
        elif p[0] == 'usemtl':
            mat = p[1]
        elif p[0] == 'f':
            idx = [x.split('/') for x in p[1:]]
            if len(idx) < 3:
                continue
            if len(idx) == 3:
                idx.append(idx[2])
            for start in range(0, len(idx) - 2, 2):      # fan of quads for n-gons
                face = [idx[0], idx[start + 1], idx[start + 2], idx[start + 3] if start + 3 < len(idx) else idx[start + 2]]
                t = mat_tex.get(mat, 0)
                q = [t, 1 if mat and mat.endswith(emissive_suffix) else 0]
                for vi in face:
                    v = verts[int(vi[0]) - 1]
                    tc = uvs[int(vi[1]) - 1] if len(vi) > 1 and vi[1] else [0, 0]
                    q += [round(v[0] + offset[0], 5), round(v[1] + offset[1], 5), round(v[2] + offset[2], 5), round(tc[0], 5), round(1 - tc[1], 5)]
                parts.setdefault(cur, []).append(q)
    return parts

def display_of(d):
    out = {}
    for src, dst in (('gui', 'gui'), ('firstperson_righthand', 'firstperson'), ('thirdperson_righthand', 'thirdperson'), ('ground', 'ground')):
        if src in d.get('display', {}):
            out[dst] = d['display'][src]
    return out

def write(name, mesh):
    with open(os.path.join(MESH, name + '.json'), 'w') as f:
        json.dump(mesh, f, separators=(',', ':'))
    n = sum(len(q) for q in mesh['parts'].values())
    print('%s: %d parts, %d quads' % (name, len(mesh['parts']), n))

# ---- MekaSuit ---------------------------------------------------------------------------------------
M = os.path.join(SRC, 'src/main/resources/assets/mekanism/models/entity')
tex_list = [save_tex('mekanism:entity/armor/mekasuit_player', 'mekasuit_player'),
            save_tex('mekanism:entity/armor/mekasuit_armor_body', 'mekasuit_armor_body'),
            save_tex('mekanism:entity/armor/mekasuit_armor_helmet', 'mekasuit_armor_helmet'),
            save_tex('mekanism:entity/armor/mekasuit_armor_exoskeleton', 'mekasuit_armor_exoskeleton')]
mat = {'player': 0, 'body': 1, 'body_led': 1, 'helmet': 2, 'helmet_led': 2, 'exoskeleton': 3}
write('mekasuit', {'textures': tex_list, 'parts': obj_parts(os.path.join(M, 'mekasuit.obj'), mat)})

# ---- Atomic Disassembler ----------------------------------------------------------------------------
d = json.load(open(ref_path('mekanism:item/atomic_disassembler', 'models', '.json')))
t_dis = save_tex(d['textures']['disassembler'], 'atomic_disassembler')
write('atomic_disassembler', {'textures': [t_dis], 'parts': {'main': element_quads(d['elements'], lambda t: 0)}, 'display': display_of(d)})

# ---- Wind Generator ---------------------------------------------------------------------------------
G = 'mekanismgenerators'
comp = json.load(open(ref_path(G + ':block/wind_generator', 'models', '.json')))
base_child = comp['children']['base']
tex_names = {}
textures = []
def tex_index(ref):
    if ref not in tex_names:
        tex_names[ref] = len(textures)
        textures.append(save_tex(ref, 'wg_' + re.sub(r'[^a-z0-9]+', '_', ref.split('/')[-1])))
    return tex_names[ref]
def child_tex(key):
    key = key.lstrip('#')
    ref = base_child['textures'].get(key)
    return tex_index(ref) if ref else None
base_quads = element_quads(base_child['elements'], child_tex)
wg = tex_index(G + ':block/model/wind_generator')
main_obj = obj_parts(ref_path(G + ':block/wind_generator', 'models', '.obj'),
                     {None: wg}, offset=(0.5, 0, 0.5))
blades_obj = obj_parts(ref_path(G + ':block/wind_generator_blades', 'models', '.obj'),
                       {None: wg})
for q in [q for qs in main_obj.values() for q in qs]:
    q[0] = wg
base_quads += [q for qs in main_obj.values() for q in qs]
blade_quads = [q for qs in blades_obj.values() for q in qs]
for q in blade_quads:
    q[0] = wg
item = json.load(open(ref_path(G + ':item/wind_generator', 'models', '.json')))
write('wind_generator', {'textures': textures, 'parts': {'base': base_quads, 'blades': blade_quads},
                         'pivots': {'blades': [0.5, 4.5, 0.5]}, 'display': display_of(item)})

# ---- Lua registrations -------------------------------------------------------------------------------
lua_path = os.path.join(OUT, 'module', 'client.lua')
lua = open(lua_path, encoding='utf-8').read()
BEGIN, END = '    -- [hs-meshes begin]\n', '    -- [hs-meshes end]\n'
if BEGIN in lua:
    lua = lua[:lua.index(BEGIN)] + lua[lua.index(END) + len(END):]
block = BEGIN + '''    -- 3D models: MekaSuit worn on the player, Atomic Disassembler, Wind Generator (spinning blades)
    registerArmorModel("mekasuit_helmet", "res/mesh/mekasuit.json", "helmet")
    registerArmorModel("mekasuit_bodyarmor", "res/mesh/mekasuit.json", "chest")
    registerArmorModel("mekasuit_pants", "res/mesh/mekasuit.json", "leggings")
    registerArmorModel("mekasuit_boots", "res/mesh/mekasuit.json", "boots")
    registerItemModel("atomic_disassembler", "res/mesh/atomic_disassembler.json")
    registerMachine("wind_generator", "Wind Generator", { mesh = "res/mesh/wind_generator.json", side = "res/solar_side.png", top = "res/solar_side.png", bottom = "res/solar_side.png", front = "res/solar_side.png" }, { kind = "generator", generatorType = "wind", generation = 480, transfer = 960, energyCapacity = 200000, spin = { part = "blades", axis = "z", speed = 6 } })
    registerShapedRecipe("wind_generator", {" O ", "OTO", "ECE"}, { O = "mekanism:ingot_osmium", T = "mekanism:energy_tablet", E = "mekanism:alloy_infused", C = "mekanism:basic_control_circuit" }, "mekanism:wind_generator", { count = 1, group = ERecipeGroup.Mechanism })
''' + END
i = lua.rindex('    log("Mekanism:')
lua = lua[:i] + block + lua[i:]
open(lua_path, 'w', encoding='utf-8').write(lua)
print('lua: mesh registrations added')
