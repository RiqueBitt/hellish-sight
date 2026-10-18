#!/usr/bin/env python3
"""Hellish Sight: converts the Mekanism Java block models (JSON "elements") of the ported machines into
the engine's box-model format (res/models/<id>.json + res/models/*.png) and adds `model = ...`
(and `modelActive = ...`) to their registerMachine texture tables in module/client.lua.

usage: build_models.py <Mekanism-26.3 root> <out/Mekanism>
"""
import json, os, re, sys
from PIL import Image

SRC, OUT = sys.argv[1], sys.argv[2]
ASSET_ROOTS = [os.path.join(SRC, 'src', d, 'resources', 'assets') for d in ('main', 'generators', 'additions', 'defense', 'tools')]
MODEL_DIR = os.path.join(OUT, 'res', 'models')
os.makedirs(MODEL_DIR, exist_ok=True)

def find_asset(ns, kind, rel, ext):
    for root in ASSET_ROOTS:
        p = os.path.join(root, ns, kind, rel + ext)
        if os.path.exists(p):
            return p
    return None

def split_ref(ref, default_ns='minecraft'):
    if ':' in ref:
        ns, rel = ref.split(':', 1)
    else:
        ns, rel = default_ns, ref
    return ns, rel

_model_cache = {}
def load_model(ref):
    """returns (elements, textures) with the parent chain resolved; elements may be None"""
    if ref in _model_cache:
        return _model_cache[ref]
    ns, rel = split_ref(ref)
    path = find_asset(ns, 'models', rel, '.json')
    if path is None:
        _model_cache[ref] = (None, {})
        return _model_cache[ref]
    with open(path, encoding='utf-8') as f:
        d = json.load(f)
    elements, textures = None, {}
    if 'parent' in d and not d['parent'].startswith('builtin/'):
        pe, pt = load_model(d['parent'])
        elements = pe
        textures.update(pt)
    textures.update(d.get('textures', {}))
    if 'elements' in d:
        elements = d['elements']
    _model_cache[ref] = (elements, textures)
    return _model_cache[ref]

def resolve_tex(name, textures, depth=0):
    if depth > 10 or name is None:
        return None
    if name.startswith('#'):
        return resolve_tex(textures.get(name[1:]), textures, depth + 1)
    return name

def default_uv(face, fr, to):
    if face == 'down':  return [fr[0], 16 - to[2], to[0], 16 - fr[2]]
    if face == 'up':    return [fr[0], fr[2], to[0], to[2]]
    if face == 'north': return [16 - to[0], 16 - to[1], 16 - fr[0], 16 - fr[1]]
    if face == 'south': return [fr[0], 16 - to[1], to[0], 16 - fr[1]]
    if face == 'west':  return [fr[2], 16 - to[1], to[2], 16 - fr[1]]
    return [16 - to[2], 16 - to[1], 16 - fr[2], 16 - fr[1]]  # east

_tex_saved = {}
def save_texture(ref):
    """copies a texture into res/models (first frame of strips, max 64 px); returns the mod-relative path"""
    if ref in _tex_saved:
        return _tex_saved[ref]
    ns, rel = split_ref(ref)
    path = find_asset(ns, 'textures', rel, '.png')
    if path is None:
        print('  missing texture', ref)
        _tex_saved[ref] = None
        return None
    im = Image.open(path).convert('RGBA')
    w, h = im.size
    if h > w:
        im = im.crop((0, 0, w, w))
    size = 16
    while size < min(w, 64):
        size *= 2
    if im.size != (size, size):
        im = im.resize((size, size), Image.NEAREST if size >= w else Image.BOX)
    name = 'mdl_' + re.sub(r'[^a-z0-9_]', '_', rel.split('/')[-1].lower())
    base, n = name, 2
    while any(v and os.path.basename(v) == name + '.png' for v in _tex_saved.values()):
        name = '%s_%d' % (base, n); n += 1
    im.save(os.path.join(MODEL_DIR, name + '.png'))
    _tex_saved[ref] = 'res/models/%s.png' % name
    return _tex_saved[ref]

def convert(model_ref, out_name):
    elements, textures = load_model(model_ref)
    if not elements:
        return None
    if len(elements) == 1 and list(elements[0].get('from', [])) == [0, 0, 0] and list(elements[0].get('to', [])) == [16, 16, 16]:
        return None     # a plain cube: the normal block renderer (with smooth lighting) already draws it
    tex_list, tex_index = [], {}
    out_elements = []
    for e in elements:
        fr, to = e.get('from', [0, 0, 0]), e.get('to', [16, 16, 16])
        oe = {'from': fr, 'to': to}
        if 'rotation' in e:
            r = e['rotation']
            oe['rotation'] = {'axis': r.get('axis', 'y'), 'angle': r.get('angle', 0), 'origin': r.get('origin', [8, 8, 8]), 'rescale': r.get('rescale', False)}
        if e.get('shade') is False:
            oe['shade'] = False
        if e.get('light_emission'):
            oe['emission'] = int(e['light_emission'])
        faces = {}
        for fname, f in e.get('faces', {}).items():
            ref = resolve_tex(f.get('texture'), textures)
            if ref is None:
                continue
            rel = save_texture(ref)
            if rel is None:
                continue
            if rel not in tex_index:
                tex_index[rel] = len(tex_list)
                tex_list.append(rel)
            of = {'t': tex_index[rel], 'uv': f.get('uv', default_uv(fname, fr, to))}
            if f.get('rotation'):
                of['rotation'] = f['rotation']
            if 'cullface' in f:
                of['cull'] = f['cullface']
            if 'tintindex' in f:
                of['tint'] = f['tintindex']
            faces[fname] = of
        if faces:
            oe['faces'] = faces
            out_elements.append(oe)
    if not out_elements:
        return None
    path = os.path.join(MODEL_DIR, out_name + '.json')
    with open(path, 'w') as f:
        json.dump({'textures': tex_list, 'rotate': True, 'elements': out_elements}, f, separators=(',', ':'))
    return 'res/models/%s.json' % out_name

def blockstate_models(block_id):
    """(normal, active) model refs from the block's blockstate"""
    for ns in ('mekanism', 'mekanismgenerators', 'mekanismadditions'):
        path = find_asset(ns, 'blockstates', block_id, '.json')
        if path is None:
            continue
        with open(path, encoding='utf-8') as f:
            d = json.load(f)
        normal = active = None
        variants = d.get('variants', {})
        for key, v in variants.items():
            v = v[0] if isinstance(v, list) else v
            props = dict(p.split('=') for p in key.split(',') if '=' in p)
            if props.get('facing', 'north') != 'north':
                continue
            if props.get('active') == 'true':
                active = active or v['model']
            else:
                normal = normal or v['model']
        if normal is None:
            # multipart / other forms: fall back to the block model of the same name
            normal = '%s:block/%s' % (ns, block_id)
        return normal, active
    return None, None

lua_path = os.path.join(OUT, 'module', 'client.lua')
with open(lua_path, encoding='utf-8') as f:
    lua = f.read()

converted = 0
def patch(m):
    global converted
    block_id, rest = m.group(1), m.group(2)
    normal, active = blockstate_models(block_id)
    if normal is None:
        return m.group(0)
    mp = convert(normal, block_id)
    if mp is None:
        return m.group(0)
    extra = ' model = "%s",' % mp
    if active and active != normal:
        ap = convert(active, block_id + '_active')
        if ap:
            extra += ' modelActive = "%s",' % ap
    converted += 1
    return 'registerMachine("%s"%s{%s' % (block_id, rest, extra)

lua = re.sub(r'registerMachine\("([a-z0-9_]+)"(, "[^"]*", )\{', patch, lua)
with open(lua_path, 'w', encoding='utf-8') as f:
    f.write(lua)
cells = 0
for v in _tex_saved.values():
    if v:
        s = Image.open(os.path.join(OUT, v)).size[0] // 16
        cells += s * s
print('models:', converted, 'textures:', sum(1 for v in _tex_saved.values() if v), 'atlas cells:', cells)
