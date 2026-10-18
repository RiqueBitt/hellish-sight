#!/usr/bin/env python3
"""Builds the Hellish Sight port of Mekanism (Ruby Launcher mod) from the Mekanism sources.

Output: out/Mekanism/ (manifest.json, module/client.lua, module/server.lua, res/*.png, LICENSE)
"""
import os, sys, shutil, json, colorsys
from PIL import Image

SRC = sys.argv[1]   # Mekanism-26.3 root
OUT = sys.argv[2]   # output folder (…/Mekanism)
M = os.path.join(SRC, 'src/main/resources/assets/mekanism/textures')
T = os.path.join(SRC, 'src/tools/resources/assets/mekanismtools/textures')

if os.path.exists(OUT):
    shutil.rmtree(OUT)
os.makedirs(os.path.join(OUT, 'res', 'armor'), exist_ok=True)
os.makedirs(os.path.join(OUT, 'module'))

def frame16(path):
    im = Image.open(path).convert('RGBA')
    w, h = im.size
    if h > w:                       # animated strip: first frame
        im = im.crop((0, 0, w, w))
    if im.size != (16, 16):
        im = im.resize((16, 16), Image.NEAREST)
    return im

def hue_shift(im, target_hue, sat_mul=1.0, val_mul=1.0, only_colored=True):
    px = im.load()
    out = im.copy()
    po = out.load()
    for y in range(im.size[1]):
        for x in range(im.size[0]):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            if only_colored and s < 0.12:
                continue
            h = target_hue
            s = min(1.0, s * sat_mul + 0.15)
            v = min(1.0, v * val_mul)
            rr, gg, bb = colorsys.hsv_to_rgb(h, s, v)
            po[x, y] = (int(rr * 255), int(gg * 255), int(bb * 255), a)
    return out

used = []
def put(name, im):
    p = os.path.join(OUT, 'res', name + '.png')
    im.save(p)
    used.append(name)
    return 'res/' + name + '.png'

def tex(rel, name=None, base=M):
    p = os.path.join(base, rel)
    if not os.path.exists(p):
        print('missing texture', rel)
        return None
    return put(name or os.path.splitext(os.path.basename(rel))[0], frame16(p))

COPPER_HUE = 0.06   # orange

# ---------------------------------------------------------------------------------
# Content tables (Lua is generated from these)
# ---------------------------------------------------------------------------------
blocks = []     # (id, name, texture, def lua)
items = []      # (id, name, texture, def lua)
machines = []   # (id, name, textures dict, def lua)
recipes = []    # lua lines
smelting = []   # (id, in, out, count, xp)
ores = []       # server: (id, block, size, count, ymin, ymax)

ORE = "{ hardness = 3.0, resistance = 15.0, tool = EBlockTool.Pickaxe, sound = 'stone' }"
METAL = "{ hardness = 5.0, resistance = 30.0, tool = EBlockTool.Pickaxe, sound = 'metal' }"
def mat(base, extra=''):
    return "ItemDefinition.new({ base = EBaseItem.%s%s })" % (base, extra)

# ---- ores ---------------------------------------------------------------------------
for metal, hue_src in (('osmium', None), ('tin', None), ('lead', None), ('uranium', None)):
    blocks.append((metal + '_ore', metal.capitalize() + ' Ore', tex('block/%s_ore.png' % metal), ORE))
blocks.append(('fluorite_ore', 'Fluorite Ore', tex('block/fluorite_ore.png'), ORE.replace("}", ", drop = 'mekanism:fluorite_gem', dropMin = 2, dropMax = 5 }")))
# copper does not exist in 1.8: recolour the tin textures
put('copper_ore', hue_shift(frame16(os.path.join(M, 'block/tin_ore.png')), COPPER_HUE, 1.4, 1.0))
blocks.append(('copper_ore', 'Copper Ore', 'res/copper_ore.png', ORE))

ores += [
    ('osmium_ore_gen', 'mekanism:osmium_ore', 8, 12, 0, 60),
    ('tin_ore_gen', 'mekanism:tin_ore', 9, 14, 0, 60),
    ('copper_ore_gen', 'mekanism:copper_ore', 9, 16, 0, 64),
    ('lead_ore_gen', 'mekanism:lead_ore', 9, 8, 0, 64),
    ('uranium_ore_gen', 'mekanism:uranium_ore', 7, 6, 0, 48),
    ('fluorite_ore_gen', 'mekanism:fluorite_ore', 7, 4, 0, 32),
]

# ---- storage blocks -------------------------------------------------------------------
for metal in ('osmium', 'tin', 'lead', 'uranium', 'bronze', 'steel', 'refined_obsidian', 'refined_glowstone', 'fluorite', 'charcoal', 'salt'):
    name = 'Block of ' + metal.replace('_', ' ').title()
    if metal == 'salt': name = 'Salt Block'
    defs = METAL if metal not in ('charcoal', 'salt', 'fluorite') else ORE
    blocks.append(('block_' + metal, name, tex('block/block_%s.png' % metal), defs))
put('block_copper', hue_shift(frame16(os.path.join(M, 'block/block_tin.png')), COPPER_HUE, 1.4, 1.0))
blocks.append(('block_copper', 'Block of Copper', 'res/block_copper.png', METAL))
blocks.append(('steel_casing', 'Steel Casing', tex('block/steel_casing.png'), METAL))

# ---- metals ---------------------------------------------------------------------------
METALS = ['osmium', 'tin', 'lead', 'uranium', 'bronze', 'steel', 'refined_obsidian', 'refined_glowstone']
for m in METALS:
    items.append(('ingot_' + m, m.replace('_', ' ').title() + ' Ingot', tex('item/ingot_%s.png' % m), mat('Ingot')))
    items.append(('nugget_' + m, m.replace('_', ' ').title() + ' Nugget', tex('item/nugget_%s.png' % m), mat('Default')))
put('ingot_copper', hue_shift(frame16(os.path.join(M, 'item/ingot_tin.png')), COPPER_HUE, 1.4, 1.0))
put('nugget_copper', hue_shift(frame16(os.path.join(M, 'item/nugget_tin.png')), COPPER_HUE, 1.4, 1.0))
items.append(('ingot_copper', 'Copper Ingot', 'res/ingot_copper.png', mat('Ingot')))
items.append(('nugget_copper', 'Copper Nugget', 'res/nugget_copper.png', mat('Default')))

DUSTS = ['osmium', 'tin', 'lead', 'uranium', 'copper', 'iron', 'gold', 'bronze', 'steel', 'diamond', 'emerald',
         'obsidian', 'refined_obsidian', 'coal', 'charcoal', 'quartz', 'lapis_lazuli', 'sulfur', 'lithium', 'fluorite']
for d in DUSTS:
    items.append(('dust_' + d, d.replace('_', ' ').title() + ' Dust', tex('item/dust_%s.png' % d), mat('Default')))

ORE_METALS = ['iron', 'gold', 'osmium', 'tin', 'copper', 'lead', 'uranium']
for kind, label in (('dirty_dust', 'Dirty %s Dust'), ('clump', '%s Clump'), ('shard', '%s Shard')):
    for m in ORE_METALS:
        items.append(('%s_%s' % (kind, m), label % m.title(), tex('item/%s_%s.png' % (kind, m)), mat('Default')))

for e in ('carbon', 'redstone', 'diamond', 'refined_obsidian', 'gold', 'tin', 'iron'):
    items.append(('enriched_' + e, 'Enriched ' + e.replace('_', ' ').title(), tex('item/enriched_%s.png' % e), mat('Default')))
for a, label in (('infused', 'Infused Alloy'), ('reinforced', 'Reinforced Alloy'), ('atomic', 'Atomic Alloy')):
    items.append(('alloy_' + a, label, tex('item/alloy_%s.png' % a), mat('Default')))
for c in ('basic', 'advanced', 'elite', 'ultimate'):
    items.append(('%s_control_circuit' % c, '%s Control Circuit' % c.title(), tex('item/%s_control_circuit.png' % c), mat('Default')))
for misc, label in (('salt', 'Salt'), ('bio_fuel', 'Bio Fuel'), ('sawdust', 'Sawdust'), ('fluorite_gem', 'Fluorite'),
                    ('substrate', 'Substrate'), ('energy_tablet', 'Energy Tablet')):
    items.append((misc, label, tex('item/%s.png' % misc), mat('Default')))

# ---- tools and armour (Mekanism Tools) ------------------------------------------------
TIERS = {
    # level, uses, speed, damage, ench | armour: mult, head, torso, legs, feet, ench
    'osmium':            ((2, 1024, 4, 4, 14), (30, 3, 5, 6, 3, 12)),
    'bronze':            ((2, 800, 14, 2, 10), (35, 2, 6, 5, 3, 10)),
    'steel':             ((3, 1280, 18, 3, 10), (40, 3, 6, 5, 3, 9)),
    'refined_obsidian':  ((3, 4096, 12, 8, 40), (50, 5, 10, 8, 5, 40)),
    'refined_glowstone': ((2, 384, 20, 2, 18), (18, 3, 7, 6, 3, 18)),
    'lapis_lazuli':      ((1, 128, 8, 1, 32), (13, 2, 6, 5, 2, 23)),
}
INGOT_FOR = {'lapis_lazuli': 'minecraft:lapis_lazuli'}
tools_lua = []
for t, ((lv, uses, spd, dmg, en), (mult, hd, ts, lg, ft, aen)) in TIERS.items():
    T_name = t.replace('_', ' ').title()
    tools_lua.append('    local %sTier = registerItemTier("%s", { level = %d, uses = %d, speed = %d, damage = %d, ench = %d })' % (t, t, lv, uses, spd, dmg, en))
    tools_lua.append('    local %sArmor = registerArmorMaterial("%s", { durabilityMultiplier = %d, head = %d, torso = %d, legs = %d, feet = %d, ench = %d })' % (t, t, mult, hd, ts, lg, ft, aen))
    for tool, base, label in (('sword', 'Weapon', 'Sword'), ('pickaxe', 'Pickaxe', 'Pickaxe'), ('axe', 'Hatchet', 'Axe'),
                              ('shovel', 'Shovel', 'Shovel'), ('hoe', 'Hoe', 'Hoe')):
        p = tex('item/%s/%s.png' % (t, tool), '%s_%s' % (t, tool), T)
        tools_lua.append('    registerItem("%s_%s", "%s %s", "%s", ItemDefinition.new({ base = EBaseItem.%s, customTier = %sTier, maxStacksize = 1 }))' % (t, tool, T_name, label, p, base, t))
    for piece, base, label in (('helmet', 'Helmet', 'Helmet'), ('chestplate', 'Chestplate', 'Chestplate'),
                               ('leggings', 'Leggings', 'Leggings'), ('boots', 'Boots', 'Boots')):
        p = tex('item/%s/%s.png' % (t, piece), '%s_%s' % (t, piece), T)
        tools_lua.append('    registerItem("%s_%s", "%s %s", "%s", ItemDefinition.new({ base = EBaseItem.%s, armorSet = "mek_%s", customArmorMaterial = %sArmor, maxStacksize = 1 }))' % (t, piece, T_name, label, p, base, t, t))
    # worn armour (64x32 layers)
    l1 = os.path.join(T, 'entity/equipment/humanoid/%s.png' % t)
    l2 = os.path.join(T, 'entity/equipment/humanoid_leggings/%s.png' % t)
    for src, suffix in ((l1, '_1'), (l2, '_2')):
        if os.path.exists(src):
            im = Image.open(src).convert('RGBA')
            if im.size != (64, 32):
                im = im.resize((64, 32), Image.NEAREST)
            im.save(os.path.join(OUT, 'res', 'armor', 'mek_%s%s.png' % (t, suffix)))
            im.save(os.path.join(OUT, 'armor', 'mek_%s%s.png' % (t, suffix))) if os.path.isdir(os.path.join(OUT, 'armor')) else None
        else:
            print('missing armour layer', src)
    # recipes
    mat_item = INGOT_FOR.get(t, 'mekanism:ingot_' + t)
    for tool, shape in (('sword', ['M', 'M', 'S']), ('pickaxe', ['MMM', ' S ', ' S ']), ('axe', ['MM', 'MS', ' S']),
                        ('shovel', ['M', 'S', 'S']), ('hoe', ['MM', ' S', ' S'])):
        recipes.append('    registerShapedRecipe("%s_%s", {%s}, { M = "%s", S = "minecraft:stick" }, "mekanism:%s_%s", { count = 1, group = ERecipeGroup.Tool })'
                       % (t, tool, ', '.join('"%s"' % r for r in shape), mat_item, t, tool))
    for piece, shape in (('helmet', ['MMM', 'M M']), ('chestplate', ['M M', 'MMM', 'MMM']), ('leggings', ['MMM', 'M M', 'M M']), ('boots', ['M M', 'M M'])):
        recipes.append('    registerShapedRecipe("%s_%s", {%s}, { M = "%s" }, "mekanism:%s_%s", { count = 1, group = ERecipeGroup.Armour })'
                       % (t, piece, ', '.join('"%s"' % r for r in shape), mat_item, t, piece))
os.makedirs(os.path.join(OUT, 'armor'), exist_ok=True)
for f in os.listdir(os.path.join(OUT, 'res', 'armor')):
    shutil.copy(os.path.join(OUT, 'res', 'armor', f), os.path.join(OUT, 'armor', f))

# ---- machines ---------------------------------------------------------------------------
def machine_tex(folder, mid, tint=None):
    d = os.path.join(M, 'block', folder)
    out = {}
    for key, cands in (('front', ['front.png']), ('frontActive', ['front_active.png']), ('side', ['left.png', 'side.png', 'right.png']),
                       ('top', ['top.png']), ('bottom', ['bottom.png'])):
        for c in cands:
            p = os.path.join(d, c)
            if os.path.exists(p):
                im = frame16(p)
                if tint is not None:
                    im = hue_shift(im, tint, 1.2, 1.0)
                out[key] = put('%s_%s' % (mid, key), im)
                break
    return out

POWER = '{ {"minecraft:redstone", 800}, {"minecraft:redstone_block", 7200}, {"mekanism:energy_tablet", 16000}, {"minecraft:coal", 1600}, {"minecraft:charcoal", 1600}, {"mekanism:bio_fuel", 400}, {"mekanism:block_charcoal", 14400} }'

def recipe_list(rs):
    parts = []
    for r in rs:
        s = '{ "%s", "%s", %d' % (r[0], r[1], r[2])
        if len(r) > 3 and r[3]:
            s += ', extra = "%s"' % r[3]
        if len(r) > 4 and r[4]:
            s += ', inCount = %d' % r[4]
        parts.append(s + ' }')
    return '{\n            ' + ',\n            '.join(parts) + '\n        }'

ench = []   # enrichment chamber
for m in ('osmium', 'tin', 'copper', 'lead', 'uranium'):
    ench.append(('mekanism:%s_ore' % m, 'mekanism:dust_%s' % m, 2))
ench += [('minecraft:iron_ore', 'mekanism:dust_iron', 2), ('minecraft:gold_ore', 'mekanism:dust_gold', 2),
         ('minecraft:diamond_ore', 'minecraft:diamond', 2), ('minecraft:emerald_ore', 'minecraft:emerald', 2),
         ('minecraft:redstone_ore', 'minecraft:redstone', 12), ('minecraft:lapis_ore', 'minecraft:lapis_lazuli', 12),
         ('minecraft:coal_ore', 'minecraft:coal', 2), ('minecraft:quartz_ore', 'minecraft:quartz', 6),
         ('mekanism:fluorite_ore', 'mekanism:fluorite_gem', 6)]
for m in ORE_METALS:
    ench.append(('mekanism:dirty_dust_%s' % m, 'mekanism:dust_%s' % m, 1))
ench += [('minecraft:redstone', 'mekanism:enriched_redstone', 1), ('minecraft:coal', 'mekanism:enriched_carbon', 2),
         ('minecraft:charcoal', 'mekanism:enriched_carbon', 1), ('minecraft:diamond', 'mekanism:enriched_diamond', 1),
         ('mekanism:dust_refined_obsidian', 'mekanism:enriched_refined_obsidian', 1), ('minecraft:gold_ingot', 'mekanism:enriched_gold', 1),
         ('mekanism:dust_tin', 'mekanism:enriched_tin', 1), ('minecraft:gravel', 'minecraft:flint', 1),
         ('minecraft:cobblestone', 'minecraft:mossy_cobblestone', 1), ('minecraft:stone_bricks', 'minecraft:cracked_stone_bricks', 1),
         ('mekanism:dust_obsidian', 'mekanism:dust_refined_obsidian', 1), ('minecraft:obsidian', 'mekanism:dust_obsidian', 4),
         ('mekanism:dust_diamond', 'minecraft:diamond', 1), ('mekanism:dust_emerald', 'minecraft:emerald', 1),
         ('mekanism:dust_lapis_lazuli', 'minecraft:lapis_lazuli', 1), ('mekanism:dust_quartz', 'minecraft:quartz', 1),
         ('mekanism:dust_coal', 'minecraft:coal', 1), ('mekanism:dust_charcoal', 'minecraft:charcoal', 1),
         ('mekanism:dust_fluorite', 'mekanism:fluorite_gem', 1)]

crush = []
for m in ('osmium', 'tin', 'copper', 'lead', 'uranium', 'bronze', 'steel', 'refined_obsidian'):
    crush.append(('mekanism:ingot_%s' % m, 'mekanism:dust_%s' % m, 1))
crush += [('minecraft:iron_ingot', 'mekanism:dust_iron', 1), ('minecraft:gold_ingot', 'mekanism:dust_gold', 1),
          ('minecraft:diamond', 'mekanism:dust_diamond', 1), ('minecraft:emerald', 'mekanism:dust_emerald', 1),
          ('minecraft:quartz', 'mekanism:dust_quartz', 1), ('minecraft:lapis_lazuli', 'mekanism:dust_lapis_lazuli', 1),
          ('minecraft:coal', 'mekanism:dust_coal', 1), ('minecraft:charcoal', 'mekanism:dust_charcoal', 1),
          ('mekanism:fluorite_gem', 'mekanism:dust_fluorite', 1), ('minecraft:obsidian', 'mekanism:dust_obsidian', 4),
          ('minecraft:cobblestone', 'minecraft:gravel', 1), ('minecraft:gravel', 'minecraft:sand', 1),
          ('minecraft:stone', 'minecraft:cobblestone', 1), ('minecraft:glowstone', 'minecraft:glowstone_dust', 4),
          ('minecraft:wheat', 'mekanism:bio_fuel', 2), ('minecraft:wheat_seeds', 'mekanism:bio_fuel', 1),
          ('minecraft:apple', 'mekanism:bio_fuel', 4), ('minecraft:carrot', 'mekanism:bio_fuel', 2),
          ('minecraft:potato', 'mekanism:bio_fuel', 2), ('minecraft:reeds', 'mekanism:bio_fuel', 2),
          ('minecraft:melon', 'mekanism:bio_fuel', 1), ('minecraft:pumpkin', 'mekanism:bio_fuel', 4),
          ('minecraft:oak_sapling', 'mekanism:bio_fuel', 2), ('minecraft:oak_leaves', 'mekanism:bio_fuel', 1),
          ('mekanism:block_salt', 'mekanism:salt', 4)]
for m in ORE_METALS:
    crush.append(('mekanism:clump_%s' % m, 'mekanism:dirty_dust_%s' % m, 1))

purif = [('mekanism:%s_ore' % m, 'mekanism:clump_%s' % m, 3) for m in ('osmium', 'tin', 'copper', 'lead', 'uranium')]
purif += [('minecraft:iron_ore', 'mekanism:clump_iron', 3), ('minecraft:gold_ore', 'mekanism:clump_gold', 3)]
purif += [('mekanism:shard_%s' % m, 'mekanism:clump_%s' % m, 1) for m in ORE_METALS]
purif = [(a, b, c, 'minecraft:flint') if a.endswith('_ore') else (a, b, c) for (a, b, c) in purif]

inject = [('mekanism:%s_ore' % m, 'mekanism:shard_%s' % m, 4, 'mekanism:salt') for m in ('osmium', 'tin', 'copper', 'lead', 'uranium')]
inject += [('minecraft:iron_ore', 'mekanism:shard_iron', 4, 'mekanism:salt'), ('minecraft:gold_ore', 'mekanism:shard_gold', 4, 'mekanism:salt')]

infuse = [('minecraft:iron_ingot', 'mekanism:alloy_infused', 1, 'minecraft:redstone'),
          ('minecraft:iron_ingot', 'mekanism:alloy_infused', 1, 'mekanism:enriched_redstone'),
          ('mekanism:ingot_osmium', 'mekanism:basic_control_circuit', 1, 'minecraft:redstone'),
          ('mekanism:ingot_osmium', 'mekanism:basic_control_circuit', 1, 'mekanism:enriched_redstone'),
          ('minecraft:iron_ingot', 'mekanism:enriched_iron', 1, 'mekanism:enriched_carbon'),
          ('mekanism:dust_iron', 'mekanism:enriched_iron', 1, 'mekanism:enriched_carbon'),
          ('mekanism:enriched_iron', 'mekanism:dust_steel', 1, 'mekanism:enriched_carbon'),
          ('mekanism:alloy_infused', 'mekanism:alloy_reinforced', 1, 'mekanism:enriched_diamond'),
          ('mekanism:alloy_reinforced', 'mekanism:alloy_atomic', 1, 'mekanism:enriched_refined_obsidian'),
          ('mekanism:dust_obsidian', 'mekanism:dust_refined_obsidian', 1, 'mekanism:enriched_diamond'),
          ('mekanism:ingot_copper', 'mekanism:ingot_bronze', 1, 'mekanism:enriched_tin'),
          ('mekanism:dust_copper', 'mekanism:dust_bronze', 1, 'mekanism:enriched_tin'),
          ('minecraft:cobblestone', 'minecraft:mossy_cobblestone', 1, 'mekanism:bio_fuel'),
          ('minecraft:dirt', 'minecraft:grass_block', 1, 'mekanism:bio_fuel'),
          ('minecraft:sand', 'minecraft:dirt', 1, 'mekanism:bio_fuel')]

compress = [('mekanism:dust_refined_obsidian', 'mekanism:ingot_refined_obsidian', 1, 'mekanism:ingot_osmium'),
            ('minecraft:glowstone_dust', 'mekanism:ingot_refined_glowstone', 1, 'mekanism:ingot_osmium'),
            ('mekanism:dust_diamond', 'minecraft:diamond', 1, 'mekanism:ingot_osmium'),
            ('mekanism:dust_fluorite', 'mekanism:fluorite_gem', 1, 'mekanism:ingot_osmium')]

combine = [('mekanism:dust_%s' % m, 'mekanism:%s_ore' % m, 1, 'minecraft:cobblestone', 8) for m in ('osmium', 'tin', 'copper', 'lead', 'uranium')]
combine += [('mekanism:dust_iron', 'minecraft:iron_ore', 1, 'minecraft:cobblestone', 8), ('mekanism:dust_gold', 'minecraft:gold_ore', 1, 'minecraft:cobblestone', 8),
            ('minecraft:diamond', 'minecraft:diamond_ore', 1, 'minecraft:cobblestone', 3), ('minecraft:redstone', 'minecraft:redstone_ore', 1, 'minecraft:cobblestone', 16),
            ('minecraft:emerald', 'minecraft:emerald_ore', 1, 'minecraft:cobblestone', 3), ('minecraft:coal', 'minecraft:coal_ore', 1, 'minecraft:cobblestone', 3),
            ('minecraft:lapis_lazuli', 'minecraft:lapis_ore', 1, 'minecraft:cobblestone', 16), ('minecraft:quartz', 'minecraft:quartz_ore', 1, 'minecraft:netherrack', 8)]

saw = [('minecraft:oak_log', 'minecraft:oak_planks', 6), ('minecraft:oak_planks', 'minecraft:stick', 6),
       ('minecraft:bookshelf', 'minecraft:oak_planks', 6), ('minecraft:crafting_table', 'minecraft:oak_planks', 4),
       ('minecraft:chest', 'minecraft:oak_planks', 8), ('minecraft:wooden_door', 'minecraft:oak_planks', 6),
       ('minecraft:stick', 'mekanism:sawdust', 1)]

machines += [
    ('enrichment_chamber', 'Enrichment Chamber', machine_tex('enrichment_chamber', 'enrichment_chamber'), ench, True),
    ('crusher', 'Crusher', machine_tex('crusher', 'crusher'), crush, True),
    ('energized_smelter', 'Energized Smelter', machine_tex('energized_smelter', 'energized_smelter'), [], True),
    ('purification_chamber', 'Purification Chamber', machine_tex('purification_chamber', 'purification_chamber'), purif, True),
    ('chemical_injection_chamber', 'Chemical Injection Chamber', machine_tex('chemical_injection_chamber', 'chemical_injection_chamber'), inject, True),
    ('metallurgic_infuser', 'Metallurgic Infuser', machine_tex('combiner', 'metallurgic_infuser', tint=0.33), infuse, True),
    ('osmium_compressor', 'Osmium Compressor', machine_tex('osmium_compressor', 'osmium_compressor'), compress, True),
    ('combiner', 'Combiner', machine_tex('combiner', 'combiner'), combine, True),
    ('precision_sawmill', 'Precision Sawmill', machine_tex('precision_sawmill', 'precision_sawmill'), saw, True),
]

# ---- crafting -------------------------------------------------------------------------------
def shaped(rid, rows, keys, out, count=1, group='Mechanism'):
    k = ', '.join('%s = "%s"' % (a, b) for a, b in keys.items())
    recipes.append('    registerShapedRecipe("%s", {%s}, { %s }, "%s", { count = %d, group = ERecipeGroup.%s })'
                   % (rid, ', '.join('"%s"' % r for r in rows), k, out, count, group))
def shapeless(rid, ins, out, count=1, group='Decoration'):
    recipes.append('    registerShapelessRecipe("%s", {%s}, "%s", { count = %d, group = ERecipeGroup.%s })'
                   % (rid, ', '.join('"%s"' % i for i in ins), out, count, group))

STORAGE = ['osmium', 'tin', 'lead', 'uranium', 'bronze', 'steel', 'refined_obsidian', 'refined_glowstone', 'copper']
for m in STORAGE:
    shaped('block_%s' % m, ['III', 'III', 'III'], {'I': 'mekanism:ingot_' + m}, 'mekanism:block_' + m, 1, 'Structure')
    shapeless('block_%s_uncraft' % m, ['mekanism:block_' + m], 'mekanism:ingot_' + m, 9)
    shaped('ingot_%s_from_nuggets' % m, ['NNN', 'NNN', 'NNN'], {'N': 'mekanism:nugget_' + m}, 'mekanism:ingot_' + m, 1, 'Structure')
    shapeless('nugget_%s' % m, ['mekanism:ingot_' + m], 'mekanism:nugget_' + m, 9)
shaped('block_fluorite', ['FFF', 'FFF', 'FFF'], {'F': 'mekanism:fluorite_gem'}, 'mekanism:block_fluorite', 1, 'Structure')
shapeless('block_fluorite_uncraft', ['mekanism:block_fluorite'], 'mekanism:fluorite_gem', 9)
shaped('block_charcoal', ['CCC', 'CCC', 'CCC'], {'C': 'minecraft:charcoal'}, 'mekanism:block_charcoal', 1, 'Structure')
shapeless('block_charcoal_uncraft', ['mekanism:block_charcoal'], 'minecraft:charcoal', 9)
shaped('block_salt', ['SS', 'SS'], {'S': 'mekanism:salt'}, 'mekanism:block_salt', 1, 'Structure')
shaped('steel_casing', ['SGS', 'GOG', 'SGS'], {'S': 'mekanism:ingot_steel', 'G': 'minecraft:glass', 'O': 'mekanism:ingot_osmium'}, 'mekanism:steel_casing')
shaped('advanced_control_circuit', ['ACA'], {'A': 'mekanism:alloy_infused', 'C': 'mekanism:basic_control_circuit'}, 'mekanism:advanced_control_circuit')
shaped('elite_control_circuit', ['ACA'], {'A': 'mekanism:alloy_reinforced', 'C': 'mekanism:advanced_control_circuit'}, 'mekanism:elite_control_circuit')
shaped('ultimate_control_circuit', ['ACA'], {'A': 'mekanism:alloy_atomic', 'C': 'mekanism:elite_control_circuit'}, 'mekanism:ultimate_control_circuit')
shaped('energy_tablet', ['RGR', 'ARA', 'RGR'], {'R': 'minecraft:redstone', 'G': 'minecraft:gold_ingot', 'A': 'mekanism:alloy_infused'}, 'mekanism:energy_tablet')
shaped('substrate', ['SBS'], {'S': 'mekanism:salt', 'B': 'mekanism:bio_fuel'}, 'mekanism:substrate', 2)
shapeless('salt_from_water', ['minecraft:water_bucket', 'minecraft:sand'], 'mekanism:salt', 4)
# machines (Mekanism style: casing in the middle)
M_RECIPES = {
    'enrichment_chamber': (['RCR', 'IMI', 'RCR'], {'R': 'minecraft:redstone', 'C': 'mekanism:basic_control_circuit', 'I': 'minecraft:iron_ingot', 'M': 'mekanism:steel_casing'}),
    'crusher': (['RCR', 'LML', 'RCR'], {'R': 'minecraft:redstone', 'C': 'mekanism:basic_control_circuit', 'L': 'minecraft:lava_bucket', 'M': 'mekanism:steel_casing'}),
    'energized_smelter': (['RCR', 'GMG', 'RCR'], {'R': 'minecraft:redstone', 'C': 'mekanism:basic_control_circuit', 'G': 'minecraft:glass', 'M': 'mekanism:steel_casing'}),
    'purification_chamber': (['ACA', 'EME', 'ACA'], {'A': 'mekanism:alloy_reinforced', 'C': 'mekanism:advanced_control_circuit', 'E': 'mekanism:enrichment_chamber', 'M': 'mekanism:steel_casing'}),
    'chemical_injection_chamber': (['ACA', 'PMP', 'ACA'], {'A': 'mekanism:alloy_reinforced', 'C': 'mekanism:elite_control_circuit', 'P': 'mekanism:purification_chamber', 'M': 'mekanism:steel_casing'}),
    'metallurgic_infuser': (['IFI', 'RMR', 'IFI'], {'I': 'minecraft:iron_ingot', 'F': 'minecraft:furnace', 'R': 'minecraft:redstone', 'M': 'mekanism:ingot_osmium'}),
    'osmium_compressor': (['ACA', 'BMB', 'ACA'], {'A': 'mekanism:alloy_infused', 'C': 'mekanism:advanced_control_circuit', 'B': 'minecraft:bucket', 'M': 'mekanism:steel_casing'}),
    'combiner': (['ACA', 'SMS', 'ACA'], {'A': 'mekanism:alloy_reinforced', 'C': 'mekanism:elite_control_circuit', 'S': 'minecraft:cobblestone', 'M': 'mekanism:steel_casing'}),
    'precision_sawmill': (['ICI', 'PMP', 'ICI'], {'I': 'minecraft:iron_ingot', 'C': 'mekanism:basic_control_circuit', 'P': 'minecraft:oak_planks', 'M': 'mekanism:steel_casing'}),
}
for mid, (rows, keys) in M_RECIPES.items():
    shaped(mid, rows, keys, 'mekanism:' + mid)

for m in ('osmium', 'tin', 'copper', 'lead', 'uranium'):
    smelting.append(('smelt_%s_ore' % m, 'mekanism:%s_ore' % m, 'mekanism:ingot_' + m, 1, 0.7))
for m in ('osmium', 'tin', 'copper', 'lead', 'uranium', 'bronze', 'steel'):
    smelting.append(('smelt_dust_%s' % m, 'mekanism:dust_' + m, 'mekanism:ingot_' + m, 1, 0.3))
smelting += [('smelt_dust_iron', 'mekanism:dust_iron', 'minecraft:iron_ingot', 1, 0.3),
             ('smelt_dust_gold', 'mekanism:dust_gold', 'minecraft:gold_ingot', 1, 0.3),
             ('smelt_enriched_iron', 'mekanism:enriched_iron', 'minecraft:iron_ingot', 1, 0.1)]

# ---------------------------------------------------------------------------------
# Lua
# ---------------------------------------------------------------------------------
L = []
L.append('-- Mekanism for Hellish Sight (Minecraft LCE "1.8"). Generated by build_mod.py.')
L.append('-- Port of Mekanism by Aidan C. Brady and the Mekanism team (MIT License, see LICENSE).')
L.append('-- Original: https://github.com/mekanism/Mekanism')
L.append('')
L.append('local POWER = ' + POWER)
L.append('')
L.append('function main()')
L.append('    -- blocks')
for bid, name, tx, d in blocks:
    L.append('    registerBlock("%s", "%s", "%s", BlockDefinition.new(%s))' % (bid, name, tx, d))
L.append('')
L.append('    -- items')
for iid, name, tx, d in items:
    L.append('    registerItem("%s", "%s", "%s", %s)' % (iid, name, tx, d))
L.append('')
L.append('    -- tools and armour (Mekanism Tools)')
L += tools_lua
L.append('')
L.append('    -- machines: input slot (top), power / infusion slot (bottom), output (right)')
for mid, name, tx, rs, power in machines:
    t = ', '.join('%s = "%s"' % (k, v) for k, v in tx.items())
    opts = ['hardness = 3.5', 'processTicks = 200', 'power = POWER']
    if mid == 'energized_smelter':
        opts.append('furnaceRecipes = true')
        opts.append('processTicks = 100')
        opts.remove('processTicks = 200')
    if rs:
        opts.append('recipes = ' + recipe_list(rs))
    L.append('    registerMachine("%s", "%s", { %s }, {\n        %s\n    })' % (mid, name, t, ',\n        '.join(opts)))
L.append('')
L.append('    -- crafting')
L += recipes
for sid, a, b, c, xp in smelting:
    L.append('    registerSmeltingRecipe("%s", "%s", "%s", { count = %d, xp = %.1f })' % (sid, a, b, c, xp))
L.append('')
L.append('    log("Mekanism: %d blocks, %d items, %d machines registered")' % (len(blocks), len(items) + len(TIERS) * 9, len(machines)))
L.append('end')
open(os.path.join(OUT, 'module', 'client.lua'), 'w').write('\n'.join(L) + '\n')

S = ['-- Mekanism for Hellish Sight: world generation (server side).', '', 'function main()']
for oid, blk, size, count, ymin, ymax in ores:
    S.append('    registerOreFeature("%s", "%s", { size = %d, count = %d, yMin = %d, yMax = %d, target = "minecraft:stone", dimension = 0 })' % (oid, blk, size, count, ymin, ymax))
S.append('    log("Mekanism: ore generation registered")')
S.append('end')
open(os.path.join(OUT, 'module', 'server.lua'), 'w').write('\n'.join(S) + '\n')

json.dump({'name': 'Mekanism', 'version': '26.3-hs1', 'modId': 'mekanism',
           'clientMain': 'module/client.lua', 'serverMain': 'module/server.lua'},
          open(os.path.join(OUT, 'manifest.json'), 'w'), indent=2)
shutil.copy(os.path.join(SRC, 'LICENSE'), os.path.join(OUT, 'LICENSE'))
open(os.path.join(OUT, 'README.txt'), 'w').write(
    'Mekanism for Hellish Sight\n'
    '==========================\n'
    'A port of Mekanism (https://github.com/mekanism/Mekanism, MIT License, Copyright (c) 2017-2025 Aidan C. Brady)\n'
    'and Mekanism Tools to the Hellish Sight mod hub (Minecraft Legacy Console Edition).\n'
    'Textures come from Mekanism 26.3; copper (which 1.8 does not have) uses recoloured tin textures.\n\n'
    'Install: copy this "Mekanism" folder into the game\'s "mods" folder and restart the game.\n'
    'Machines open a furnace-style screen: top slot = input, bottom slot = power (redstone, coal,\n'
    'bio fuel, energy tablet...) or the infusion/extra item, right slot = output.\n')
print('blocks', len(blocks), 'items', len(items) + len(TIERS) * 9, 'machines', len(machines), 'textures', len(used))
