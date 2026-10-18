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

POWER = '{ {"minecraft:redstone", 400}, {"minecraft:redstone_block", 3600}, {"minecraft:coal", 200}, {"minecraft:charcoal", 200}, {"mekanism:bio_fuel", 100} }'

def recipe_list(rs):
    parts = []
    for r in rs:
        if isinstance(r, str):
            parts.append(r)
            continue
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

# ---- energy: generators, energy cubes, universal cables -------------------------------
def gen_tex(mid, src_folder, hue):
    return machine_tex(src_folder, mid, tint=hue)
energy_blocks = []   # (id, name, textures, opts)
heat = gen_tex('heat_generator', 'energized_smelter', 0.0)
energy_blocks.append(('heat_generator', 'Heat Generator', heat,
    'kind = "generator", generatorType = "burn", furnaceFuels = true, generation = 80, transfer = 160, energyCapacity = 160000, ' +
    'power = { {"minecraft:lava_bucket", 4000}, {"mekanism:block_charcoal", 14400} }'))
bio = gen_tex('bio_generator', 'energized_smelter', 0.30)
energy_blocks.append(('bio_generator', 'Bio-Generator', bio,
    'kind = "generator", generatorType = "burn", generation = 140, transfer = 280, energyCapacity = 160000, power = { {"mekanism:bio_fuel", 200}, {"minecraft:wheat", 60}, {"minecraft:reeds", 60} }'))
# solar panels: generated tops
from PIL import ImageDraw
def solar_top(name, cell, frame):
    im = Image.new('RGBA', (16, 16), frame)
    d = ImageDraw.Draw(im)
    for gx in range(2):
        for gy in range(2):
            x0, y0 = 1 + gx * 7, 1 + gy * 7
            d.rectangle([x0, y0, x0 + 6, y0 + 6], fill=cell)
            d.line([x0, y0 + 3, x0 + 6, y0 + 3], fill=(cell[0] + 30, cell[1] + 30, cell[2] + 40, 255))
            d.point((x0 + 1, y0 + 1), fill=(200, 220, 255, 255))
    return put(name, im)
casing = frame16(os.path.join(M, 'block/steel_casing.png'))
put('solar_side', casing)
energy_blocks.append(('solar_generator', 'Solar Generator', {'side': 'res/solar_side.png', 'top': solar_top('solar_generator_top', (20, 40, 110, 255), (150, 150, 155, 255)), 'bottom': 'res/solar_side.png', 'front': 'res/solar_side.png'},
    'kind = "generator", generatorType = "solar", generation = 15, transfer = 60, energyCapacity = 96000'))
energy_blocks.append(('advanced_solar_generator', 'Advanced Solar Generator', {'side': 'res/solar_side.png', 'top': solar_top('advanced_solar_generator_top', (25, 70, 140, 255), (80, 80, 85, 255)), 'bottom': 'res/solar_side.png', 'front': 'res/solar_side.png'},
    'kind = "generator", generatorType = "solar", generation = 120, transfer = 480, energyCapacity = 400000'))
cube_base = frame16(os.path.join(M, 'block/models/energy_cube.png'))
CUBE_TIERS = [('basic', 4000000, 4000), ('advanced', 16000000, 16000), ('elite', 64000000, 64000), ('ultimate', 256000000, 256000)]
for tier, cap, tr in CUBE_TIERS:
    corner = frame16(os.path.join(M, 'block/models/energy_cube_%s_corner.png' % tier))
    im = cube_base.copy(); im.alpha_composite(corner)
    core = frame16(os.path.join(M, 'block/energy_core.png'))
    front = im.copy(); front.alpha_composite(core.resize((16, 16)))
    p_side = put('%s_energy_cube' % tier, im)
    p_front = put('%s_energy_cube_front' % tier, front)
    energy_blocks.append(('%s_energy_cube' % tier, '%s Energy Cube' % tier.title(), {'side': p_side, 'front': p_front, 'frontActive': p_front, 'top': p_side, 'bottom': p_side},
        'kind = "storage", energyCapacity = %d, transfer = %d, power = { {"minecraft:redstone", 400}, {"minecraft:redstone_block", 3600} }' % (cap, tr)))
CABLE_TIERS = [('basic', 8000), ('advanced', 128000), ('elite', 1024000), ('ultimate', 8192000)]
for tier, tr in CABLE_TIERS:
    src = Image.open(os.path.join(M, 'block/models/multipart/opaque/%s_universal_cable_vertical.png' % tier)).convert('RGBA')
    if not os.path.exists(os.path.join(M, 'block/models/multipart/opaque/%s_universal_cable_vertical.png' % tier)):
        src = Image.open(os.path.join(M, 'block/models/multipart/%s_universal_cable.png' % tier)).convert('RGBA')
    im = src.resize((16, 16), Image.BOX)
    p = put('%s_universal_cable' % tier, im)
    energy_blocks.append(('%s_universal_cable' % tier, '%s Universal Cable' % tier.title(), {'side': p},
        'kind = "cable", transfer = %d, hardness = 1.0' % tr))

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

shaped('heat_generator', ['III', 'WOW', 'CFC'], {'I': 'minecraft:iron_ingot', 'W': 'minecraft:oak_planks', 'O': 'mekanism:ingot_osmium', 'C': 'mekanism:ingot_copper', 'F': 'minecraft:furnace'}, 'mekanism:heat_generator')
shaped('bio_generator', ['RER', 'BCB', 'IEI'], {'R': 'minecraft:redstone', 'E': 'mekanism:energy_tablet', 'B': 'mekanism:bio_fuel', 'C': 'mekanism:steel_casing', 'I': 'minecraft:iron_ingot'}, 'mekanism:bio_generator')
shaped('solar_generator', ['GGG', 'AIA', 'OEO'], {'G': 'minecraft:glass', 'A': 'mekanism:alloy_infused', 'I': 'minecraft:iron_ingot', 'O': 'mekanism:ingot_osmium', 'E': 'mekanism:energy_tablet'}, 'mekanism:solar_generator')
shaped('advanced_solar_generator', ['SAS', 'SAS', 'III'], {'S': 'mekanism:solar_generator', 'A': 'mekanism:alloy_infused', 'I': 'minecraft:iron_ingot'}, 'mekanism:advanced_solar_generator')
shaped('basic_energy_cube', ['RTR', 'IPI', 'RTR'], {'R': 'minecraft:redstone', 'T': 'mekanism:energy_tablet', 'I': 'minecraft:iron_ingot', 'P': 'mekanism:steel_casing'}, 'mekanism:basic_energy_cube')
shaped('advanced_energy_cube', ['ATA', 'OCO', 'ATA'], {'A': 'mekanism:alloy_infused', 'T': 'mekanism:energy_tablet', 'O': 'mekanism:ingot_osmium', 'C': 'mekanism:basic_energy_cube'}, 'mekanism:advanced_energy_cube')
shaped('elite_energy_cube', ['ATA', 'GCG', 'ATA'], {'A': 'mekanism:alloy_reinforced', 'T': 'mekanism:energy_tablet', 'G': 'minecraft:gold_ingot', 'C': 'mekanism:advanced_energy_cube'}, 'mekanism:elite_energy_cube')
shaped('ultimate_energy_cube', ['ATA', 'DCD', 'ATA'], {'A': 'mekanism:alloy_atomic', 'T': 'mekanism:energy_tablet', 'D': 'minecraft:diamond', 'C': 'mekanism:elite_energy_cube'}, 'mekanism:ultimate_energy_cube')
shaped('basic_universal_cable', ['SRS'], {'S': 'mekanism:ingot_steel', 'R': 'minecraft:redstone'}, 'mekanism:basic_universal_cable', 8)
for lo, hi, alloy in (('basic', 'advanced', 'alloy_infused'), ('advanced', 'elite', 'alloy_reinforced'), ('elite', 'ultimate', 'alloy_atomic')):
    shaped('%s_universal_cable' % hi, ['CCC', 'CAC', 'CCC'], {'C': 'mekanism:%s_universal_cable' % lo, 'A': 'mekanism:' + alloy}, 'mekanism:%s_universal_cable' % hi, 8)

for m in ('osmium', 'tin', 'copper', 'lead', 'uranium'):
    smelting.append(('smelt_%s_ore' % m, 'mekanism:%s_ore' % m, 'mekanism:ingot_' + m, 1, 0.7))
for m in ('osmium', 'tin', 'copper', 'lead', 'uranium', 'bronze', 'steel'):
    smelting.append(('smelt_dust_%s' % m, 'mekanism:dust_' + m, 'mekanism:ingot_' + m, 1, 0.3))
smelting += [('smelt_dust_iron', 'mekanism:dust_iron', 'minecraft:iron_ingot', 1, 0.3),
             ('smelt_dust_gold', 'mekanism:dust_gold', 'minecraft:gold_ingot', 1, 0.3),
             ('smelt_enriched_iron', 'mekanism:enriched_iron', 'minecraft:iron_ingot', 1, 0.1)]


# =====================================================================================
# Full Mekanism: chemicals, chemical machines, tanks, pipes, transporters, pump, miner,
# teleporter, factories, upgrades, configurator, wearables and multiblocks.
# =====================================================================================
from PIL import ImageDraw
G = os.path.join(SRC, 'src/generators/resources/assets/mekanismgenerators/textures')

def R(inp='', out='', count=1, extra=None, inCount=None, chemIn=None, chemIn2=None, chemOut=None, chemOut2=None, secondary=None):
    s = '{ "%s", "%s", %d' % (inp, out, count)
    if extra: s += ', extra = "%s"' % extra
    if inCount: s += ', inCount = %d' % inCount
    for k, v in (('chemIn', chemIn), ('chemIn2', chemIn2), ('chemOut', chemOut), ('chemOut2', chemOut2)):
        if v: s += ', %s = { "%s", %d }' % (k, v[0], v[1])
    if secondary: s += ', secondary = { "%s", chance = %.2f }' % secondary
    return s + ' }'

def tex_hq(path, name, opaque=False, bg=(38, 42, 48, 255)):
    if not os.path.exists(path):
        print('missing texture', path)
        return None
    im = Image.open(path).convert('RGBA')
    w, h = im.size
    if h > w:
        im = im.crop((0, 0, w, w))
    if im.size != (16, 16):
        im = im.resize((16, 16), Image.BOX)
    if opaque:
        back = Image.new('RGBA', (16, 16), bg)
        back.alpha_composite(im)
        im = back
    return put(name, im)

def pixel_art(name, rows, palette):
    im = Image.new('RGBA', (16, 16), (0, 0, 0, 0))
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch in palette:
                im.putpixel((x, y), palette[ch])
    return put(name, im)

STEEL = frame16(os.path.join(M, 'block/steel_casing.png'))
put('mek_steel_side', STEEL)

def shade(c, f):
    return (max(0, min(255, int(c[0] * f))), max(0, min(255, int(c[1] * f))), max(0, min(255, int(c[2] * f))), 255)

def panel_tex(mid, color, ports=None):
    """Machine faces built on the steel casing: a recessed screen in the machine's colour."""
    side = STEEL.copy()
    sd = ImageDraw.Draw(side)
    pc = ports or color
    sd.rectangle([2, 6, 4, 9], fill=shade(pc, 0.8)); sd.rectangle([11, 6, 13, 9], fill=shade(pc, 0.8))
    front = STEEL.copy(); fd = ImageDraw.Draw(front)
    fd.rectangle([2, 2, 13, 11], fill=(24, 24, 28, 255))
    fd.rectangle([3, 3, 12, 10], fill=shade(color, 0.45))
    fd.rectangle([5, 13, 10, 13], fill=(60, 60, 64, 255))
    act = front.copy(); ad = ImageDraw.Draw(act)
    ad.rectangle([3, 3, 12, 10], fill=shade(color, 0.95))
    ad.line([4, 4, 11, 4], fill=shade(color, 1.35))
    ad.point((11, 13), fill=(80, 255, 80, 255)); ad.point((10, 13), fill=(80, 255, 80, 255))
    top = STEEL.copy(); td = ImageDraw.Draw(top)
    td.rectangle([4, 4, 11, 11], fill=shade(color, 0.6)); td.rectangle([5, 5, 10, 10], fill=shade(color, 0.85))
    return {'side': put(mid + '_side', side), 'front': put(mid + '_front', front), 'frontActive': put(mid + '_front_on', act),
            'top': put(mid + '_top', top), 'bottom': 'res/mek_steel_side.png'}

def tinted(mid, path, hue):
    return put(mid, hue_shift(frame16(path), hue, 1.1, 1.0))

extra_machines = []     # (id, name, textures, opts lua)
lua_extra = []          # extra registration lines (chemicals, tools, roles)

# ---- chemicals ----------------------------------------------------------------------------
CHEMS = [('hydrogen', 'Hidrogênio', 0xFFFFFF, True), ('oxygen', 'Oxigênio', 0x6CE2FF, True),
         ('chlorine', 'Cloro', 0xCFE800, True), ('sodium', 'Sódio', 0xE9FEF4, True),
         ('steam', 'Vapor', 0xC1C1C1, True), ('hydrogen_chloride', 'Cloreto de Hidrogênio', 0xA8F1E9, True),
         ('sulfur_dioxide', 'Dióxido de Enxofre', 0xA99D90, True), ('sulfur_trioxide', 'Trióxido de Enxofre', 0xCE6C6C, True),
         ('sulfuric_acid', 'Ácido Sulfúrico', 0x82802B, True), ('hydrofluoric_acid', 'Ácido Fluorídrico', 0xC6C29C, True),
         ('ethylene', 'Etileno', 0xEACCF9, True), ('brine', 'Salmoura', 0xFEEF9C, False), ('lithium', 'Lítio', 0xEBA400, False),
         ('heavy_water', 'Água Pesada', 0x0D0D80, False), ('deuterium', 'Deutério', 0xFF3232, True),
         ('tritium', 'Trítio', 0x64FF70, True), ('dt_fuel', 'Combustível D-T', 0xC8A0C8, True),
         ('uranium_oxide', 'Óxido de Urânio', 0xE1F573, True), ('uranium_hexafluoride', 'Hexafluoreto de Urânio', 0x809960, True),
         ('fissile_fuel', 'Combustível Físsil', 0x2E332F, True), ('nuclear_waste', 'Resíduo Nuclear', 0x4F412A, True),
         ('plutonium', 'Plutônio', 0x1F919C, True), ('polonium', 'Polônio', 0x1B1B19, True),
         ('antimatter', 'Antimatéria', 0xA464B3, True)]
SLURRY_METALS = ['iron', 'gold', 'osmium', 'tin', 'copper', 'lead', 'uranium']
for m in SLURRY_METALS:
    CHEMS.append(('dirty_%s_slurry' % m, 'Polpa Suja de %s' % m.title(), 0x6B5A40, False))
    CHEMS.append(('clean_%s_slurry' % m, 'Polpa Limpa de %s' % m.title(), 0xB0A080, False))
for cid, cname, col, gas in CHEMS:
    lua_extra.append('    registerChemical("%s", { name = "%s", color = 0x%06X, gas = %s })' % (cid, cname, col, 'true' if gas else 'false'))

# ---- new items ------------------------------------------------------------------------------
for m in SLURRY_METALS:
    items.append(('crystal_%s' % m, '%s Crystal' % m.title(), tex('item/crystal_%s.png' % m), mat('Default')))
items.append(('yellow_cake_uranium', 'Yellow Cake Uranium', tex('item/yellow_cake_uranium.png'), mat('Default')))
items.append(('pellet_plutonium', 'Plutonium Pellet', tex('item/pellet_plutonium.png'), mat('Default')))
items.append(('pellet_polonium', 'Polonium Pellet', tex('item/pellet_polonium.png'), mat('Default')))
items.append(('pellet_antimatter', 'Antimatter Pellet', tex('item/pellet_antimatter.png'), mat('Default')))
items.append(('teleportation_core', 'Teleportation Core', tex('item/teleportation_core.png'), mat('Default')))
for u, label in (('speed', 'Speed Upgrade'), ('energy', 'Energy Upgrade'), ('muffling', 'Muffling Upgrade')):
    items.append(('upgrade_%s' % u, label, tex('item/upgrade/%s.png' % u), mat('Default')))
    lua_extra.append('    registerMachineTool("upgrade_%s", "upgrade_%s")' % (u, u))
items.append(('configurator', 'Configurator', tex_hq(os.path.join(M, 'item/configurator_config.png'), 'configurator'), "ItemDefinition.new({ base = EBaseItem.Default, maxStacksize = 1 })"))
lua_extra.append('    registerMachineTool("configurator", "configurator")')

# energy tablet becomes a rechargeable battery (durability = charge)
items[:] = [i for i in items if i[0] != 'energy_tablet']
items.append(('energy_tablet', 'Energy Tablet', tex('item/energy_tablet.png'), "ItemDefinition.new({ base = EBaseItem.Default, maxDamage = 1000 })"))
lua_extra.append('    registerItemRefill("energy_tablet", "energy", 100)')

# wearables (registered after the materials in the tools section of the Lua)
wear_lua = []
wear_lua.append('    local jetpackMat = registerArmorMaterial("jetpack", { durabilityMultiplier = 50, head = 1, torso = 3, legs = 2, feet = 1, ench = 0 })')
wear_lua.append('    local runnerMat = registerArmorMaterial("free_runners", { durabilityMultiplier = 40, head = 1, torso = 3, legs = 2, feet = 2, ench = 0 })')
wear_lua.append('    local scubaMat = registerArmorMaterial("scuba", { durabilityMultiplier = 30, head = 2, torso = 3, legs = 2, feet = 1, ench = 0 })')
wear_lua.append('    local mekaMat = registerArmorMaterial("mekasuit", { durabilityMultiplier = 80, head = 5, torso = 10, legs = 8, feet = 5, ench = 15 })')
def wear(iid, label, base, matvar, armorset, texpath):
    p = tex_hq(texpath, iid)
    wear_lua.append('    registerItem("%s", "%s", "%s", ItemDefinition.new({ base = EBaseItem.%s, armorSet = "%s", customArmorMaterial = %s, maxStacksize = 1 }))' % (iid, label, p, base, armorset, matvar))
PAL = {'k': (20, 20, 24, 255), 'd': (70, 74, 82, 255), 'g': (130, 136, 146, 255), 'l': (190, 196, 204, 255), 'w': (240, 244, 248, 255),
       'o': (255, 140, 30, 255), 'y': (255, 220, 60, 255), 'c': (60, 200, 230, 255), 'b': (40, 110, 200, 255), 'e': (60, 230, 120, 255)}
pixel_art('jetpack', [
    '................',
    '...kkk....kkk...',
    '..kglgk..kglgk..',
    '..kgwgkkkkgwgk..',
    '..kglgkddkglgk..',
    '..kglgkeekglgk..',
    '..kglgkddkglgk..',
    '..kglgkddkglgk..',
    '..kglgkkkkglgk..',
    '..kddgk..kddgk..',
    '...kkk....kkk...',
    '...kdk....kdk...',
    '...oyo....oyo...',
    '...yoy....yoy...',
    '....o......o....',
    '................'], PAL)
pixel_art('free_runners', [
    '................',
    '................',
    '..kkkk....kkkk..',
    '..kglk....kglk..',
    '..kglk....kglk..',
    '..kcck....kcck..',
    '..kglk....kglk..',
    '..kglk....kglk..',
    '..kglkk...kglkk.',
    '..kglllk..kglllk',
    '..kccccck.kcccck',
    '..kdddddk.kddddk',
    '...kkkkk...kkkk.',
    '................',
    '................',
    '................'], PAL)
pixel_art('scuba_mask', [
    '................',
    '....kkkkkkkk....',
    '...kglllllllk...',
    '..kgkkkkkkkklk..',
    '..kgkbcccccbkk..',
    '..kgkcwccccckk..',
    '..kgkcccccbckk..',
    '..kgkbcccccbkk..',
    '..kgkkkkkkkklk..',
    '..kggggddggggk..',
    '...kgdkkkkdgk...',
    '....kk.kk.kk....',
    '................',
    '................',
    '................',
    '................'], PAL)
def wear_px(iid, label, base, matvar, armorset):
    wear_lua.append('    registerItem("%s", "%s", "res/%s.png", ItemDefinition.new({ base = EBaseItem.%s, armorSet = "%s", customArmorMaterial = %s, maxStacksize = 1 }))' % (iid, label, iid, base, armorset, matvar))
wear_px('jetpack', 'Jetpack', 'Chestplate', 'jetpackMat', 'mek_steel')
wear_px('free_runners', 'Free Runners', 'Boots', 'runnerMat', 'mek_steel')
wear_px('scuba_mask', 'Scuba Mask', 'Helmet', 'scubaMat', 'mek_steel')
wear('mekasuit_helmet', 'MekaSuit Helmet', 'Helmet', 'mekaMat', 'mek_refined_obsidian', os.path.join(M, 'item/mekasuit_helmet.png'))
wear('mekasuit_bodyarmor', 'MekaSuit Body Armor', 'Chestplate', 'mekaMat', 'mek_refined_obsidian', os.path.join(M, 'item/mekasuit_bodyarmor.png'))
wear('mekasuit_pants', 'MekaSuit Pants', 'Leggings', 'mekaMat', 'mek_refined_obsidian', os.path.join(M, 'item/mekasuit_pants.png'))
wear('mekasuit_boots', 'MekaSuit Boots', 'Boots', 'mekaMat', 'mek_refined_obsidian', os.path.join(M, 'item/mekasuit_boots.png'))
wear_lua.append('    local atomicTier = registerItemTier("atomic", { level = 3, uses = 4000, speed = 18, damage = 12, ench = 0 })')
p_ad = tex_hq(os.path.join(M, 'item/atomic_disassembler.png'), 'atomic_disassembler')
wear_lua.append('    registerItem("atomic_disassembler", "Atomic Disassembler", "%s", ItemDefinition.new({ base = EBaseItem.Pickaxe, customTier = atomicTier, maxStacksize = 1 }))' % p_ad)
for iid, role in (('jetpack', 'jetpack'), ('free_runners', 'free_runners'), ('scuba_mask', 'gas_mask'), ('mekasuit_helmet', 'gas_mask'),
                  ('mekasuit_bodyarmor', 'jetpack'), ('mekasuit_boots', 'free_runners')):
    wear_lua.append('    registerItemRole("%s", "%s")' % (iid, role))
for iid, res, per in (('jetpack', 'hydrogen', 10), ('mekasuit_helmet', 'energy', 200), ('mekasuit_bodyarmor', 'energy', 200),
                      ('mekasuit_pants', 'energy', 200), ('mekasuit_boots', 'energy', 200), ('free_runners', 'energy', 50),
                      ('atomic_disassembler', 'energy', 50), ('scuba_mask', 'oxygen', 10)):
    wear_lua.append('    registerItemRefill("%s", "%s", %d)' % (iid, res, per))

# ---- blocks for multiblocks ---------------------------------------------------------------------
def block_tex(path, name):
    return tex_hq(path, name, opaque=True)
MB_BLOCKS = [
    ('structural_glass', 'Structural Glass', os.path.join(M, 'block/structural_glass.png')),
    ('dynamic_tank', 'Dynamic Tank', os.path.join(M, 'block/dynamic_tank.png')),
    ('induction_casing', 'Induction Casing', os.path.join(M, 'block/induction_casing.png')),
    ('thermal_evaporation_block', 'Thermal Evaporation Block', os.path.join(M, 'block/thermal_evaporation_block.png')),
    ('thermal_evaporation_valve', 'Thermal Evaporation Valve', os.path.join(M, 'block/thermal_evaporation_valve.png')),
    ('boiler_casing', 'Boiler Casing', os.path.join(M, 'block/boiler_casing.png')),
    ('boiler_valve', 'Boiler Valve', os.path.join(M, 'block/boiler_valve_input.png')),
    ('superheating_element', 'Superheating Element', os.path.join(M, 'block/superheating_element.png')),
    ('pressure_disperser', 'Pressure Disperser', os.path.join(M, 'block/pressure_disperser.png')),
    ('sps_casing', 'SPS Casing', os.path.join(M, 'block/sps_casing.png')),
    ('teleporter_frame', 'Teleporter Frame', os.path.join(M, 'block/teleporter_frame.png')),
    ('turbine_casing', 'Turbine Casing', os.path.join(G, 'block/turbine_casing.png')),
    ('turbine_vent', 'Turbine Vent', os.path.join(G, 'block/turbine_vent.png')),
    ('turbine_rotor', 'Turbine Rotor', os.path.join(G, 'block/turbine_rotor.png')),
    ('electromagnetic_coil', 'Electromagnetic Coil', os.path.join(G, 'block/electromagnetic_coil.png')),
    ('saturating_condenser', 'Saturating Condenser', os.path.join(G, 'block/saturating_condenser.png')),
    ('fission_reactor_casing', 'Fission Reactor Casing', os.path.join(G, 'block/fission_reactor_casing.png')),
    ('fission_reactor_port', 'Fission Reactor Port', os.path.join(G, 'block/fission_reactor_port_input.png')),
    ('reactor_glass', 'Reactor Glass', os.path.join(G, 'block/reactor_glass.png')),
    ('fission_fuel_assembly', 'Fission Fuel Assembly', os.path.join(G, 'block/model/fuel_assembly/fuel_rod_assembly_side.png')),
    ('reactor_frame', 'Fusion Reactor Frame', os.path.join(G, 'block/reactor_frame.png')),
    ('reactor_port', 'Fusion Reactor Port', os.path.join(G, 'block/reactor_port.png')),
]
for tier in ('basic', 'advanced', 'elite', 'ultimate'):
    MB_BLOCKS.append(('%s_induction_cell' % tier, '%s Induction Cell' % tier.title(), os.path.join(M, 'block/%s_induction_cell.png' % tier)))
    MB_BLOCKS.append(('%s_induction_provider' % tier, '%s Induction Provider' % tier.title(), os.path.join(M, 'block/%s_induction_provider.png' % tier)))
for bid, name, path in MB_BLOCKS:
    blocks.append((bid, name, block_tex(path, bid), METAL))

def single(path, mid):
    p = tex_hq(path, mid, opaque=True)
    return {'side': p, 'front': p, 'top': p, 'bottom': p}

# ---- chemical machines ----------------------------------------------------------------------------
E = 'hardness = 3.5, energyCapacity = 80000, power = POWER'
def proc(mid, name, color, recs, per=100, ticks=40, extra=''):
    extra_machines.append((mid, name, panel_tex(mid, color),
        '%s, energyPerTick = %d, processTicks = %d%s, recipes = {\n            %s\n        }' % (E, per, ticks, extra, ',\n            '.join(recs))))

proc('electrolytic_separator', 'Electrolytic Separator', (90, 160, 230), [
    R(chemIn=('minecraft:water', 100), chemOut=('mekanism:hydrogen', 200), chemOut2=('mekanism:oxygen', 100)),
    R(chemIn=('mekanism:brine', 100), chemOut=('mekanism:sodium', 100), chemOut2=('mekanism:chlorine', 100)),
    R(chemIn=('mekanism:heavy_water', 100), chemOut=('mekanism:deuterium', 200), chemOut2=('mekanism:oxygen', 100))], per=200, ticks=10)
proc('chemical_oxidizer', 'Chemical Oxidizer', (200, 190, 60), [
    R('mekanism:dust_sulfur', chemOut=('mekanism:sulfur_dioxide', 100)),
    R('mekanism:yellow_cake_uranium', chemOut=('mekanism:uranium_oxide', 250)),
    R('mekanism:dust_lithium', chemOut=('mekanism:lithium', 100)),
    R('mekanism:salt', chemOut=('mekanism:brine', 15)),
    R('mekanism:pellet_plutonium', chemOut=('mekanism:plutonium', 1000)),
    R('mekanism:pellet_polonium', chemOut=('mekanism:polonium', 1000))])
proc('chemical_infuser', 'Chemical Infuser', (120, 220, 120), [
    R(chemIn=('mekanism:hydrogen', 100), chemIn2=('mekanism:chlorine', 100), chemOut=('mekanism:hydrogen_chloride', 100)),
    R(chemIn=('mekanism:sulfur_dioxide', 100), chemIn2=('mekanism:oxygen', 50), chemOut=('mekanism:sulfur_trioxide', 100)),
    R(chemIn=('mekanism:sulfur_trioxide', 100), chemIn2=('minecraft:water', 100), chemOut=('mekanism:sulfuric_acid', 100)),
    R(chemIn=('mekanism:deuterium', 100), chemIn2=('mekanism:tritium', 100), chemOut=('mekanism:dt_fuel', 200)),
    R(chemIn=('mekanism:uranium_oxide', 100), chemIn2=('mekanism:hydrofluoric_acid', 100), chemOut=('mekanism:uranium_hexafluoride', 100))], ticks=20)
dis = [R('mekanism:%s_ore' % m if m not in ('iron', 'gold') else 'minecraft:%s_ore' % m, chemIn=('mekanism:sulfuric_acid', 100),
         chemOut=('mekanism:dirty_%s_slurry' % m, 1000)) for m in SLURRY_METALS]
dis.append(R('mekanism:fluorite_gem', chemIn=('mekanism:sulfuric_acid', 50), chemOut=('mekanism:hydrofluoric_acid', 1000)))
proc('chemical_dissolution_chamber', 'Chemical Dissolution Chamber', (90, 200, 90), dis, per=150, ticks=100)
proc('chemical_washer', 'Chemical Washer', (60, 120, 220),
     [R(chemIn=('mekanism:dirty_%s_slurry' % m, 200), chemIn2=('minecraft:water', 200), chemOut=('mekanism:clean_%s_slurry' % m, 200)) for m in SLURRY_METALS], ticks=10)
cry = [R(out='mekanism:crystal_%s' % m, chemIn=('mekanism:clean_%s_slurry' % m, 200)) for m in SLURRY_METALS]
cry += [R(out='mekanism:salt', chemIn=('mekanism:brine', 150)), R(out='mekanism:dust_lithium', chemIn=('mekanism:lithium', 100)),
        R(out='mekanism:pellet_plutonium', chemIn=('mekanism:plutonium', 1000)), R(out='mekanism:pellet_polonium', chemIn=('mekanism:polonium', 1000)),
        R(out='mekanism:pellet_antimatter', chemIn=('mekanism:antimatter', 1000))]
proc('chemical_crystallizer', 'Chemical Crystallizer', (90, 230, 210), cry)
proc('pressurized_reaction_chamber', 'Pressurized Reaction Chamber', (200, 120, 220), [
    R('mekanism:bio_fuel', 'mekanism:substrate', 1, inCount=2, chemIn=('minecraft:water', 10), chemIn2=('mekanism:hydrogen', 100), chemOut=('mekanism:ethylene', 100)),
    R('minecraft:coal', 'mekanism:enriched_carbon', 2, chemIn=('minecraft:water', 50), chemIn2=('mekanism:oxygen', 50), chemOut=('mekanism:hydrogen', 50))], ticks=100)
proc('isotopic_centrifuge', 'Isotopic Centrifuge', (120, 200, 90), [
    R(chemIn=('mekanism:uranium_hexafluoride', 200), chemOut=('mekanism:fissile_fuel', 100)),
    R(chemIn=('mekanism:nuclear_waste', 100), chemOut=('mekanism:plutonium', 10)),
    R(chemIn=('minecraft:water', 1000), chemOut=('mekanism:heavy_water', 10))], per=200, ticks=20)
proc('solar_neutron_activator', 'Solar Neutron Activator', (230, 200, 90), [
    R(chemIn=('mekanism:lithium', 100), chemOut=('mekanism:tritium', 100)),
    R(chemIn=('mekanism:nuclear_waste', 100), chemOut=('mekanism:polonium', 10))], per=60, ticks=20)
proc('antiprotonic_nucleosynthesizer', 'Antiprotonic Nucleosynthesizer', (180, 100, 200), [
    R('minecraft:coal', 'minecraft:diamond', 1, chemIn=('mekanism:antimatter', 10)),
    R('minecraft:iron_ingot', 'minecraft:gold_ingot', 1, chemIn=('mekanism:antimatter', 5)),
    R('minecraft:diamond', 'minecraft:nether_star', 1, chemIn=('mekanism:antimatter', 100))], per=2000, ticks=200)

# chemical upgrades of the existing ore processing (true Mekanism ratios)
purif += [R('mekanism:%s_ore' % m if m not in ('iron', 'gold') else 'minecraft:%s_ore' % m, 'mekanism:clump_%s' % m, 3, chemIn=('mekanism:oxygen', 40)) for m in SLURRY_METALS]
purif += [R('mekanism:shard_%s' % m, 'mekanism:clump_%s' % m, 1, chemIn=('mekanism:oxygen', 20)) for m in SLURRY_METALS]
inject += [R('mekanism:%s_ore' % m if m not in ('iron', 'gold') else 'minecraft:%s_ore' % m, 'mekanism:shard_%s' % m, 4, chemIn=('mekanism:hydrogen_chloride', 40)) for m in SLURRY_METALS]
inject += [R('mekanism:crystal_%s' % m, 'mekanism:shard_%s' % m, 1, chemIn=('mekanism:hydrogen_chloride', 20)) for m in SLURRY_METALS]
ench.append(('mekanism:dust_uranium', 'mekanism:yellow_cake_uranium', 2))
crush.append(('minecraft:gunpowder', 'mekanism:dust_sulfur', 1))
saw[:] = [R(a, b, c, secondary=('mekanism:sawdust', 0.25)) if a.endswith('_log') or a.endswith('planks') else R(a, b, c) for (a, b, c) in saw]

# ---- tanks, pipes, transporters ---------------------------------------------------------------------
TANK_TIERS = [('basic', 64000, 1000), ('advanced', 256000, 4000), ('elite', 1024000, 16000), ('ultimate', 4096000, 64000)]
for tier, cap, tr in TANK_TIERS:
    hue = {'basic': None, 'advanced': 0.0, 'elite': 0.55, 'ultimate': 0.78}[tier]
    base = Image.new('RGBA', (16, 16), (36, 52, 70, 255))
    base.alpha_composite(Image.open(os.path.join(M, 'block/models/fluid_tank.png')).convert('RGBA').resize((16, 16), Image.NEAREST))
    p = put('%s_fluid_tank' % tier, base if hue is None else hue_shift(base, hue, 1.2, 1.0))
    extra_machines.append(('%s_fluid_tank' % tier, '%s Fluid Tank' % tier.title(), {'side': p, 'front': p, 'top': p, 'bottom': p},
        'kind = "tank", tankCapacity = %d, transfer = %d, hardness = 2.0' % (cap // 2, tr)))
    ct = panel_tex('%s_chemical_tank' % tier, {'basic': (150, 150, 155), 'advanced': (200, 60, 60), 'elite': (60, 160, 220), 'ultimate': (150, 70, 200)}[tier])
    extra_machines.append(('%s_chemical_tank' % tier, '%s Chemical Tank' % tier.title(), ct,
        'kind = "tank", tankCapacity = %d, transfer = %d, hardness = 2.0' % (cap, tr)))
PIPE_TIERS = [('basic', 2000, 4000, 8), ('advanced', 8000, 16000, 16), ('elite', 32000, 64000, 32), ('ultimate', 128000, 256000, 64)]
for tier, fl, gas, items_per in PIPE_TIERS:
    for kind, label, rate in (('mechanical_pipe', 'Mechanical Pipe', fl), ('pressurized_tube', 'Pressurized Tube', gas), ('logistical_transporter', 'Logistical Transporter', items_per)):
        src = os.path.join(M, 'block/models/multipart/%s_%s.png' % (tier, kind))
        p = tex_hq(src, '%s_%s' % (tier, kind), opaque=True)
        k = 'transporter' if kind == 'logistical_transporter' else 'pipe'
        extra_machines.append(('%s_%s' % (tier, kind), '%s %s' % (tier.title(), label), {'side': p},
            'kind = "%s", transfer = %d, hardness = 1.0, upgrades = false' % (k, rate)))

# ---- pump, miner, teleporter, gas generator ------------------------------------------------------------
extra_machines.append(('electric_pump', 'Electric Pump', panel_tex('electric_pump', (60, 110, 230)),
    'kind = "pump", energyCapacity = 20000, energyPerTick = 50, processTicks = 20, tankCapacity = 16000, radius = 16, power = POWER'))
extra_machines.append(('digital_miner', 'Digital Miner', panel_tex('digital_miner', (40, 200, 90)),
    'kind = "miner", energyCapacity = 100000, energyPerTick = 200, processTicks = 20, radius = 16, power = POWER'))
tp = tex_hq(os.path.join(M, 'block/teleporter.png'), 'teleporter', opaque=True)
extra_machines.append(('teleporter', 'Teleporter', {'side': tp, 'front': tp, 'top': tp, 'bottom': tp},
    'kind = "teleporter", energyCapacity = 5000000, energyPerTick = 1000, power = POWER'))
extra_machines.append(('gas_burning_generator', 'Gas-Burning Generator', panel_tex('gas_burning_generator', (230, 150, 60)),
    'kind = "generator", fuelChem = { "mekanism:ethylene", 1 }, generation = 480, transfer = 960, energyCapacity = 400000'))
extra_machines.append(('chargepad', 'Chargepad', panel_tex('chargepad', (230, 210, 80)),
    'kind = "storage", energyCapacity = 200000, transfer = 2000, power = POWER'))

# ---- factories ---------------------------------------------------------------------------------------
FACTORY_OPS = [('smelting', 'Smelting', 'energized_smelter', []), ('enriching', 'Enriching', 'enrichment_chamber', ench),
               ('crushing', 'Crushing', 'crusher', crush), ('compressing', 'Compressing', 'osmium_compressor', compress),
               ('combining', 'Combining', 'combiner', combine), ('purifying', 'Purifying', 'purification_chamber', purif),
               ('injecting', 'Injecting', 'chemical_injection_chamber', inject), ('infusing', 'Infusing', 'metallurgic_infuser', infuse),
               ('sawing', 'Sawing', 'precision_sawmill', saw)]
factories = []
for op, label, base_mid, rs in FACTORY_OPS:
    d = os.path.join(M, 'block/factory/%s' % op)
    tx = {}
    for key, fn in (('front', 'front'), ('frontActive', 'front_active'), ('side', 'side'), ('top', 'top'), ('bottom', 'bottom')):
        f = os.path.join(d, '%s_factory_%s.png' % (op, fn))
        if os.path.exists(f):
            tx[key] = tex_hq(f, '%s_factory_%s' % (op, fn))
    for tier, par, per in (('basic', 3, 150), ('advanced', 5, 250), ('elite', 7, 350)):
        opts = 'hardness = 3.5, energyCapacity = %d, energyPerTick = %d, processTicks = 200, parallel = %d, power = POWER' % (40000 * par, per, par)
        if op == 'smelting':
            opts += ', furnaceRecipes = true, processTicks = 100'
            opts = opts.replace('processTicks = 200, ', '')
        factories.append(('%s_%s_factory' % (tier, op), '%s %s Factory' % (tier.title(), label), tx, opts, rs))

# ---- multiblock controllers --------------------------------------------------------------------------------
def frame_list(*ids):
    return '{ ' + ', '.join('"%s"' % i for i in ids) + ' }'
ip = tex_hq(os.path.join(M, 'block/induction_port.png'), 'induction_port', opaque=True)
inner_cells = ', '.join('["%s_induction_cell"] = { capacity = %d }, ["%s_induction_provider"] = { transfer = %d }' % (t, c, t, tr)
                        for t, c, tr in (('basic', 50000000, 0), ('advanced', 200000000, 0), ('elite', 800000000, 0), ('ultimate', 2000000000, 0)))
inner_cells = inner_cells.replace('["basic_induction_provider"] = { transfer = 0 }', '["basic_induction_provider"] = { transfer = 64000 }') \
    .replace('["advanced_induction_provider"] = { transfer = 0 }', '["advanced_induction_provider"] = { transfer = 512000 }') \
    .replace('["elite_induction_provider"] = { transfer = 0 }', '["elite_induction_provider"] = { transfer = 4096000 }') \
    .replace('["ultimate_induction_provider"] = { transfer = 0 }', '["ultimate_induction_provider"] = { transfer = 32768000 }')
extra_machines.append(('induction_port', 'Induction Port', {'side': ip, 'front': ip, 'top': ip, 'bottom': ip},
    'kind = "storage", energyCapacity = 0, transfer = 0, multiblock = { frame = %s, inner = { %s }, interiorAir = true, min = 3, max = 18 }'
    % (frame_list('induction_casing', 'structural_glass'), inner_cells)))
dv = tex_hq(os.path.join(M, 'block/dynamic_valve.png'), 'dynamic_valve', opaque=True)
extra_machines.append(('dynamic_valve', 'Dynamic Valve', {'side': dv, 'front': dv, 'top': dv, 'bottom': dv},
    'kind = "tank", tankCapacity = 0, transfer = 16000, multiblock = { frame = %s, interiorAir = true, min = 3, max = 18, tankPerVolume = 16000 }'
    % frame_list('dynamic_tank', 'structural_glass')))
tc = tex_hq(os.path.join(M, 'block/thermal_evaporation_controller.png'), 'thermal_evaporation_controller', opaque=True)
extra_machines.append(('thermal_evaporation_controller', 'Thermal Evaporation Controller', {'side': tc, 'front': tc, 'top': tc, 'bottom': tc},
    'energyCapacity = 0, needsPower = false, processTicks = 20, tankCapacity = 64000, multiblock = { frame = %s, interiorAir = true, min = 3, max = 18, speedPerHeight = 0.5 }, recipes = {\n            %s\n        }'
    % (frame_list('thermal_evaporation_block', 'thermal_evaporation_valve', 'structural_glass'),
       ',\n            '.join([R(chemIn=('minecraft:water', 100), chemOut=('mekanism:brine', 10)), R(chemIn=('mekanism:brine', 100), chemOut=('mekanism:lithium', 10))]))))
bv = tex_hq(os.path.join(M, 'block/boiler_valve_output_steam.png'), 'boiler_controller', opaque=True)
extra_machines.append(('thermoelectric_boiler', 'Thermoelectric Boiler Controller', {'side': bv, 'front': bv, 'top': bv, 'bottom': bv},
    'energyCapacity = 2000000, energyPerTick = 400, processTicks = 4, tankCapacity = 256000, multiblock = { frame = %s, inner = { ["superheating_element"] = { speed = 1.0 }, ["pressure_disperser"] = { speed = 0.2 } }, interiorAir = true, min = 3, max = 18, speedPerVolume = 0.02 }, recipes = {\n            %s\n        }'
    % (frame_list('boiler_casing', 'boiler_valve', 'structural_glass'), R(chemIn=('minecraft:water', 100), chemOut=('mekanism:steam', 100)))))
tv = tex_hq(os.path.join(G, 'block/turbine_valve.png'), 'turbine_valve', opaque=True)
extra_machines.append(('turbine_valve', 'Industrial Turbine Valve', {'side': tv, 'front': tv, 'top': tv, 'bottom': tv},
    'kind = "generator", fuelChem = { "mekanism:steam", 100 }, exhaustChem = { "minecraft:water", 90 }, generation = 400, transfer = 0, energyCapacity = 4000000, tankCapacity = 256000, multiblock = { frame = %s, inner = { ["turbine_rotor"] = { speed = 0.5, transfer = 4000 }, ["electromagnetic_coil"] = { speed = 0.5, transfer = 8000 }, ["pressure_disperser"] = { speed = 0.1 }, ["saturating_condenser"] = { speed = 0.2 } }, interiorAir = true, min = 3, max = 18 }'
    % frame_list('turbine_casing', 'turbine_vent', 'structural_glass')))
fc = tex_hq(os.path.join(G, 'block/fission_reactor_logic_adapter.png'), 'fission_reactor_controller', opaque=True)
extra_machines.append(('fission_reactor_controller', 'Fission Reactor Controller', {'side': fc, 'front': fc, 'top': fc, 'bottom': fc},
    'energyCapacity = 0, needsPower = false, processTicks = 20, tankCapacity = 512000, multiblock = { frame = %s, inner = { ["fission_fuel_assembly"] = { speed = 1.0 } }, interiorAir = true, min = 3, max = 18 }, recipes = {\n            %s\n        }'
    % (frame_list('fission_reactor_casing', 'fission_reactor_port', 'reactor_glass'),
       R(chemIn=('mekanism:fissile_fuel', 10), chemIn2=('minecraft:water', 1000), chemOut=('mekanism:steam', 1000), chemOut2=('mekanism:nuclear_waste', 10)))))
rc = tex_hq(os.path.join(G, 'block/reactor_controller.png'), 'fusion_reactor_controller', opaque=True)
extra_machines.append(('fusion_reactor_controller', 'Fusion Reactor Controller', {'side': rc, 'front': rc, 'top': rc, 'bottom': rc},
    'kind = "generator", fuelChem = { "mekanism:dt_fuel", 2 }, generation = 40000, transfer = 80000, energyCapacity = 100000000, tankCapacity = 64000, multiblock = { frame = %s, interiorAir = true, min = 5, max = 9 }'
    % frame_list('reactor_frame', 'reactor_port', 'reactor_glass')))
sp = tex_hq(os.path.join(M, 'block/sps_port.png'), 'sps_port', opaque=True)
extra_machines.append(('sps_port', 'SPS Port (Supercritical Phase Shifter)', {'side': sp, 'front': sp, 'top': sp, 'bottom': sp},
    'energyCapacity = 10000000, energyPerTick = 20000, processTicks = 100, tankCapacity = 64000, multiblock = { frame = %s, interiorAir = true, min = 5, max = 5 }, recipes = {\n            %s\n        }'
    % (frame_list('sps_casing', 'structural_glass'), R(chemIn=('mekanism:polonium', 2000), chemOut=('mekanism:antimatter', 1)))))

# ---- recipes for everything new ------------------------------------------------------------------------------
C = lambda t: 'mekanism:%s_control_circuit' % t
shaped('upgrade_speed', [' G ', 'ADA', ' G '], {'G': 'minecraft:glass', 'A': 'mekanism:alloy_infused', 'D': 'mekanism:dust_osmium'}, 'mekanism:upgrade_speed')
shaped('upgrade_energy', [' G ', 'ADA', ' G '], {'G': 'minecraft:glass', 'A': 'mekanism:alloy_infused', 'D': 'mekanism:dust_gold'}, 'mekanism:upgrade_energy')
shaped('upgrade_muffling', [' G ', 'ADA', ' G '], {'G': 'minecraft:glass', 'A': 'mekanism:alloy_infused', 'D': 'mekanism:dust_steel'}, 'mekanism:upgrade_muffling')
shaped('configurator', [' L ', 'AEA', ' S '], {'L': 'minecraft:lapis_lazuli', 'A': 'mekanism:alloy_infused', 'E': 'mekanism:energy_tablet', 'S': 'minecraft:stick'}, 'mekanism:configurator', 1, 'Tool')
shaped('teleportation_core', ['LAL', 'GDG', 'LAL'], {'L': 'minecraft:lapis_lazuli', 'A': 'mekanism:alloy_atomic', 'G': 'minecraft:gold_ingot', 'D': 'minecraft:diamond'}, 'mekanism:teleportation_core')
shaped('structural_glass', ['GSG', 'S S', 'GSG'], {'G': 'minecraft:glass', 'S': 'mekanism:ingot_steel'}, 'mekanism:structural_glass', 4, 'Structure')
casing = lambda rid, extra, n=4: shaped(rid, [' S ', 'SXS', ' S '], {'S': 'mekanism:ingot_steel', 'X': extra}, 'mekanism:' + rid, n, 'Structure')
casing('dynamic_tank', 'minecraft:bucket')
casing('induction_casing', 'mekanism:energy_tablet')
casing('thermal_evaporation_block', 'mekanism:ingot_copper', 4)
casing('boiler_casing', 'mekanism:ingot_osmium')
casing('turbine_casing', 'mekanism:ingot_osmium')
casing('fission_reactor_casing', 'mekanism:ingot_lead')
casing('sps_casing', 'mekanism:pellet_polonium')
casing('reactor_frame', 'mekanism:alloy_atomic')
casing('teleporter_frame', 'mekanism:alloy_infused', 8)
shaped('thermal_evaporation_valve', [' B ', 'BCB', ' B '], {'B': 'mekanism:thermal_evaporation_block', 'C': C('advanced')}, 'mekanism:thermal_evaporation_valve')
shaped('thermal_evaporation_controller', ['CGC', 'BTB', 'BBB'], {'C': C('basic'), 'G': 'minecraft:glass', 'B': 'mekanism:thermal_evaporation_block', 'T': 'mekanism:basic_fluid_tank'}, 'mekanism:thermal_evaporation_controller')
shaped('boiler_valve', [' B ', 'BCB', ' B '], {'B': 'mekanism:boiler_casing', 'C': C('advanced')}, 'mekanism:boiler_valve', 2)
shaped('thermoelectric_boiler', ['BCB', 'CTC', 'BCB'], {'B': 'mekanism:boiler_casing', 'C': C('elite'), 'T': 'mekanism:basic_chemical_tank'}, 'mekanism:thermoelectric_boiler')
shaped('superheating_element', ['SCS', 'CRC', 'SCS'], {'S': 'mekanism:ingot_steel', 'C': 'mekanism:ingot_copper', 'R': 'minecraft:redstone_block'}, 'mekanism:superheating_element', 2)
shaped('pressure_disperser', ['SCS', 'GIG', 'SCS'], {'S': 'mekanism:ingot_steel', 'C': C('basic'), 'G': 'minecraft:glass', 'I': 'minecraft:iron_bars'}, 'mekanism:pressure_disperser')
shaped('turbine_rotor', ['SAS', 'SAS', 'SAS'], {'S': 'mekanism:ingot_steel', 'A': 'mekanism:alloy_infused'}, 'mekanism:turbine_rotor')
shaped('electromagnetic_coil', ['SGS', 'GEG', 'SGS'], {'S': 'mekanism:ingot_steel', 'G': 'minecraft:gold_ingot', 'E': 'mekanism:energy_tablet'}, 'mekanism:electromagnetic_coil')
shaped('turbine_vent', [' B ', 'BIB', ' B '], {'B': 'mekanism:turbine_casing', 'I': 'minecraft:iron_bars'}, 'mekanism:turbine_vent', 4)
shaped('turbine_valve', [' B ', 'BCB', ' B '], {'B': 'mekanism:turbine_casing', 'C': C('elite')}, 'mekanism:turbine_valve')
shaped('saturating_condenser', ['SBS', 'SCS', 'SBS'], {'S': 'mekanism:ingot_steel', 'B': 'minecraft:bucket', 'C': C('advanced')}, 'mekanism:saturating_condenser')
shaped('fission_reactor_port', [' B ', 'BCB', ' B '], {'B': 'mekanism:fission_reactor_casing', 'C': C('elite')}, 'mekanism:fission_reactor_port', 4)
shaped('fission_fuel_assembly', ['LAL', 'LBL', 'LAL'], {'L': 'mekanism:ingot_lead', 'A': 'mekanism:alloy_reinforced', 'B': 'mekanism:basic_chemical_tank'}, 'mekanism:fission_fuel_assembly')
shaped('fission_reactor_controller', ['CRC', 'BTB', 'CRC'], {'C': C('ultimate'), 'R': 'minecraft:redstone_block', 'B': 'mekanism:fission_reactor_casing', 'T': 'mekanism:elite_chemical_tank'}, 'mekanism:fission_reactor_controller')
shaped('reactor_glass', [' F ', 'FGF', ' F '], {'F': 'mekanism:reactor_frame', 'G': 'mekanism:structural_glass'}, 'mekanism:reactor_glass', 4, 'Structure')
shaped('reactor_port', [' F ', 'FCF', ' F '], {'F': 'mekanism:reactor_frame', 'C': C('ultimate')}, 'mekanism:reactor_port', 2)
shaped('fusion_reactor_controller', ['CGC', 'ATA', 'AAA'], {'C': C('ultimate'), 'G': 'mekanism:reactor_glass', 'A': 'mekanism:reactor_frame', 'T': 'mekanism:ultimate_chemical_tank'}, 'mekanism:fusion_reactor_controller')
shaped('sps_port', [' B ', 'BCB', ' B '], {'B': 'mekanism:sps_casing', 'C': C('ultimate')}, 'mekanism:sps_port')
shaped('induction_port', [' B ', 'BCB', ' B '], {'B': 'mekanism:induction_casing', 'C': C('elite')}, 'mekanism:induction_port', 2)
shaped('dynamic_valve', [' B ', 'BCB', ' B '], {'B': 'mekanism:dynamic_tank', 'C': C('basic')}, 'mekanism:dynamic_valve', 2)
cell_in = {'basic': 'mekanism:energy_tablet', 'advanced': 'mekanism:basic_induction_cell', 'elite': 'mekanism:advanced_induction_cell', 'ultimate': 'mekanism:elite_induction_cell'}
prov_in = {'basic': 'mekanism:basic_control_circuit', 'advanced': 'mekanism:basic_induction_provider', 'elite': 'mekanism:advanced_induction_provider', 'ultimate': 'mekanism:elite_induction_provider'}
cube_for = {'basic': 'mekanism:basic_energy_cube', 'advanced': 'mekanism:advanced_energy_cube', 'elite': 'mekanism:elite_energy_cube', 'ultimate': 'mekanism:ultimate_energy_cube'}
for t in ('basic', 'advanced', 'elite', 'ultimate'):
    shaped('%s_induction_cell' % t, ['LXL', 'XCX', 'LXL'], {'L': 'mekanism:dust_lithium', 'X': cell_in[t], 'C': cube_for[t]}, 'mekanism:%s_induction_cell' % t)
    shaped('%s_induction_provider' % t, ['LXL', 'XCX', 'LXL'], {'L': 'mekanism:ingot_copper', 'X': prov_in[t], 'C': cube_for[t]}, 'mekanism:%s_induction_provider' % t)
prev = None
for tier, cap, tr in TANK_TIERS:
    if prev is None:
        shaped('basic_fluid_tank', ['RIR', 'I I', 'RIR'], {'R': 'minecraft:redstone', 'I': 'minecraft:iron_ingot'}, 'mekanism:basic_fluid_tank')
        shaped('basic_chemical_tank', ['ROR', 'O O', 'ROR'], {'R': 'minecraft:redstone', 'O': 'mekanism:ingot_osmium'}, 'mekanism:basic_chemical_tank')
    else:
        a = {'advanced': 'mekanism:alloy_infused', 'elite': 'mekanism:alloy_reinforced', 'ultimate': 'mekanism:alloy_atomic'}[tier]
        shaped('%s_fluid_tank' % tier, ['ATA', 'T T', 'ATA'], {'A': a, 'T': 'mekanism:%s_fluid_tank' % prev}, 'mekanism:%s_fluid_tank' % tier)
        shaped('%s_chemical_tank' % tier, ['ATA', 'T T', 'ATA'], {'A': a, 'T': 'mekanism:%s_chemical_tank' % prev}, 'mekanism:%s_chemical_tank' % tier)
    prev = tier
shaped('basic_mechanical_pipe', ['SBS'], {'S': 'mekanism:ingot_steel', 'B': 'minecraft:bucket'}, 'mekanism:basic_mechanical_pipe', 8)
shaped('basic_pressurized_tube', ['SGS'], {'S': 'mekanism:ingot_steel', 'G': 'minecraft:glass'}, 'mekanism:basic_pressurized_tube', 8)
shaped('basic_logistical_transporter', ['SCS'], {'S': 'mekanism:ingot_steel', 'C': C('basic')}, 'mekanism:basic_logistical_transporter', 8)
for lo, hi, alloy in (('basic', 'advanced', 'alloy_infused'), ('advanced', 'elite', 'alloy_reinforced'), ('elite', 'ultimate', 'alloy_atomic')):
    for kind in ('mechanical_pipe', 'pressurized_tube', 'logistical_transporter'):
        shaped('%s_%s' % (hi, kind), ['CCC', 'CAC', 'CCC'], {'C': 'mekanism:%s_%s' % (lo, kind), 'A': 'mekanism:' + alloy}, 'mekanism:%s_%s' % (hi, kind), 8)
MACH = {
    'electrolytic_separator': (['IRI', 'ACA', 'IRI'], {'I': 'minecraft:iron_ingot', 'R': 'minecraft:redstone', 'A': 'mekanism:alloy_infused', 'C': 'mekanism:steel_casing'}),
    'chemical_oxidizer': (['ACA', 'TMT', 'ACA'], {'A': 'mekanism:alloy_infused', 'C': C('advanced'), 'T': 'mekanism:basic_chemical_tank', 'M': 'mekanism:steel_casing'}),
    'chemical_infuser': (['ACA', 'TMT', 'ACA'], {'A': 'mekanism:alloy_reinforced', 'C': C('advanced'), 'T': 'mekanism:basic_chemical_tank', 'M': 'mekanism:steel_casing'}),
    'chemical_dissolution_chamber': (['CTC', 'AMA', 'CTC'], {'C': C('ultimate'), 'T': 'mekanism:basic_chemical_tank', 'A': 'mekanism:alloy_atomic', 'M': 'mekanism:steel_casing'}),
    'chemical_washer': (['CBC', 'TMT', 'CBC'], {'C': C('ultimate'), 'B': 'minecraft:bucket', 'T': 'mekanism:basic_chemical_tank', 'M': 'mekanism:steel_casing'}),
    'chemical_crystallizer': (['TCT', 'AMA', 'TCT'], {'T': 'mekanism:basic_chemical_tank', 'C': C('ultimate'), 'A': 'mekanism:alloy_atomic', 'M': 'mekanism:steel_casing'}),
    'pressurized_reaction_chamber': (['ACA', 'EMT', 'ACA'], {'A': 'mekanism:alloy_reinforced', 'C': C('elite'), 'E': 'mekanism:enrichment_chamber', 'T': 'mekanism:basic_chemical_tank', 'M': 'mekanism:steel_casing'}),
    'isotopic_centrifuge': (['LCL', 'TMT', 'LCL'], {'L': 'mekanism:ingot_lead', 'C': C('elite'), 'T': 'mekanism:basic_chemical_tank', 'M': 'mekanism:steel_casing'}),
    'solar_neutron_activator': (['AGA', 'CMC', 'AGA'], {'A': 'mekanism:alloy_reinforced', 'G': 'mekanism:solar_generator', 'C': C('elite'), 'M': 'mekanism:steel_casing'}),
    'antiprotonic_nucleosynthesizer': (['ACA', 'TMT', 'ACA'], {'A': 'mekanism:alloy_atomic', 'C': C('ultimate'), 'T': 'mekanism:ultimate_chemical_tank', 'M': 'mekanism:steel_casing'}),
    'electric_pump': ([' B ', 'IMI', 'ICI'], {'B': 'minecraft:bucket', 'I': 'mekanism:ingot_osmium', 'M': 'mekanism:steel_casing', 'C': C('basic')}),
    'digital_miner': (['ACA', 'LRL', 'TMT'], {'A': 'mekanism:alloy_atomic', 'C': C('ultimate'), 'L': 'mekanism:basic_logistical_transporter', 'R': 'mekanism:teleportation_core', 'T': 'mekanism:enrichment_chamber', 'M': 'mekanism:steel_casing'}),
    'teleporter': (['COC', 'OTO', 'COC'], {'C': C('basic'), 'O': 'mekanism:teleporter_frame', 'T': 'mekanism:teleportation_core'}),
    'gas_burning_generator': (['PEP', 'CMC', 'PEP'], {'P': 'mekanism:basic_pressurized_tube', 'E': 'mekanism:electrolytic_separator', 'C': C('basic'), 'M': 'mekanism:steel_casing'}),
    'chargepad': (['PPP', 'SES'], {'P': 'minecraft:stone_pressure_plate', 'S': 'mekanism:ingot_steel', 'E': 'mekanism:energy_tablet'}),
}
for mid, (rows, keys) in MACH.items():
    shaped(mid, rows, keys, 'mekanism:' + mid)
for op, label, base_mid, rs in FACTORY_OPS:
    shaped('basic_%s_factory' % op, ['RCR', 'IMI', 'RCR'], {'R': 'minecraft:redstone', 'C': C('basic'), 'I': 'minecraft:iron_ingot', 'M': 'mekanism:' + base_mid}, 'mekanism:basic_%s_factory' % op)
    shaped('advanced_%s_factory' % op, ['ACA', 'IMI', 'ACA'], {'A': 'mekanism:alloy_infused', 'C': C('advanced'), 'I': 'mekanism:ingot_osmium', 'M': 'mekanism:basic_%s_factory' % op}, 'mekanism:advanced_%s_factory' % op)
    shaped('elite_%s_factory' % op, ['ACA', 'IMI', 'ACA'], {'A': 'mekanism:alloy_reinforced', 'C': C('elite'), 'I': 'minecraft:gold_ingot', 'M': 'mekanism:advanced_%s_factory' % op}, 'mekanism:elite_%s_factory' % op)
shaped('jetpack', ['SCS', 'TAT', 'S S'], {'S': 'mekanism:ingot_steel', 'C': C('basic'), 'T': 'mekanism:basic_chemical_tank', 'A': 'mekanism:alloy_infused'}, 'mekanism:jetpack', 1, 'Armour')
shaped('free_runners', ['C C', 'A A', 'T T'], {'C': C('basic'), 'A': 'mekanism:alloy_infused', 'T': 'mekanism:energy_tablet'}, 'mekanism:free_runners', 1, 'Armour')
shaped('scuba_mask', [' G ', 'GTG', 'SSS'], {'G': 'minecraft:glass', 'T': 'mekanism:basic_chemical_tank', 'S': 'mekanism:ingot_steel'}, 'mekanism:scuba_mask', 1, 'Armour')
for piece, rows in (('mekasuit_helmet', ['PCP', 'PSP']), ('mekasuit_bodyarmor', ['P P', 'PCP', 'PJP']), ('mekasuit_pants', ['PCP', 'P P', 'P P']), ('mekasuit_boots', ['P P', 'RCR'])):
    keys = {'P': 'mekanism:alloy_atomic', 'C': C('ultimate'), 'S': 'mekanism:scuba_mask', 'J': 'mekanism:jetpack', 'R': 'mekanism:free_runners'}
    keys = {k: v for k, v in keys.items() if any(k in r for r in rows)}
    shaped(piece, rows, keys, 'mekanism:' + piece, 1, 'Armour')
shaped('atomic_disassembler', ['AEA', 'ACA', ' O '], {'A': 'mekanism:alloy_atomic', 'E': 'mekanism:energy_tablet', 'C': C('ultimate'), 'O': 'mekanism:ingot_refined_obsidian'}, 'mekanism:atomic_disassembler', 1, 'Tool')

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
L.append('    -- wearables and electric tools')
L += wear_lua
L.append('')
L.append('    -- chemicals, machine tools and rechargeable items')
L += lua_extra
L.append('')
L.append('    -- machines: input slot (top), power / infusion slot (bottom), output (right)')
for mid, name, tx, rs, power in machines:
    t = ', '.join('%s = "%s"' % (k, v) for k, v in tx.items())
    opts = ['hardness = 3.5', 'processTicks = 200', 'power = POWER', 'energyCapacity = 40000', 'energyPerTick = 50']
    if mid == 'energized_smelter':
        opts.append('furnaceRecipes = true')
        opts.append('processTicks = 100')
        opts.remove('processTicks = 200')
    if rs:
        opts.append('recipes = ' + recipe_list(rs))
    L.append('    registerMachine("%s", "%s", { %s }, {\n        %s\n    })' % (mid, name, t, ',\n        '.join(opts)))
L.append('')
L.append('    -- energy: generators, energy cubes and universal cables (FE)')
for mid, name, tx, opts in energy_blocks:
    t = ', '.join('%s = "%s"' % (k, v) for k, v in tx.items())
    L.append('    registerMachine("%s", "%s", { %s }, { %s })' % (mid, name, t, opts))
L.append('')
L.append('    -- chemical machines, tanks, pipes, transporters, pump, miner, teleporter, multiblocks')
for mid, name, tx, opts in extra_machines:
    t = ', '.join('%s = "%s"' % (k, v) for k, v in tx.items() if v)
    L.append('    registerMachine("%s", "%s", { %s }, {\n        %s\n    })' % (mid, name, t, opts))
L.append('')
L.append('    -- factories (process several items at once)')
for mid, name, tx, opts, rs in factories:
    t = ', '.join('%s = "%s"' % (k, v) for k, v in tx.items() if v)
    o = opts + (', recipes = ' + recipe_list(rs) if rs else '')
    L.append('    registerMachine("%s", "%s", { %s }, {\n        %s\n    })' % (mid, name, t, o))
L.append('')
L.append('    -- crafting')
L += recipes
for sid, a, b, c, xp in smelting:
    L.append('    registerSmeltingRecipe("%s", "%s", "%s", { count = %d, xp = %.1f })' % (sid, a, b, c, xp))
L.append('')
L.append('    log("Mekanism: %d blocks, %d items, %d machines, %d energy blocks registered")' % (len(blocks), len(items) + len(TIERS) * 9, len(machines), len(energy_blocks)))
L.append('end')
open(os.path.join(OUT, 'module', 'client.lua'), 'w').write('\n'.join(L) + '\n')

S = ['-- Mekanism for Hellish Sight: world generation (server side).', '', 'function main()']
for oid, blk, size, count, ymin, ymax in ores:
    S.append('    registerOreFeature("%s", "%s", { size = %d, count = %d, yMin = %d, yMax = %d, target = "minecraft:stone", dimension = 0 })' % (oid, blk, size, count, ymin, ymax))
S.append('    log("Mekanism: ore generation registered")')
S.append('end')
open(os.path.join(OUT, 'module', 'server.lua'), 'w').write('\n'.join(S) + '\n')

json.dump({'name': 'Mekanism', 'version': '26.3-hs2', 'modId': 'mekanism',
           'creativeIcon': 'alloy_atomic',
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
    'bio fuel, energy tablet...) or the infusion/extra item, right slot = output.\n\n'
    'Guia rapido (pt-BR)\n'
    '-------------------\n'
    '* Agachar + clique direito com a mao vazia: mostra no chat energia, tanques, progresso,\n'
    '  upgrades e estado da estrutura. Canos/cabos/transportadores mostram o status sem agachar.\n'
    '* Configurator: clique numa face para alternar padrao / entrada / saida / desligada.\n'
    '  Na frente da maquina liga/desliga o auto-eject. Teleporter: muda a frequencia (0-15).\n'
    '  Digital Miner (face de cima): alterna entre so minerios e todos os blocos.\n'
    '  Logistical Transporter: a face marcada como "puxar itens" puxa de baus/fornalhas.\n'
    '* Upgrades (velocidade/energia/silenciador): clique direito na maquina com o upgrade (max. 8).\n'
    '* Baldes de agua/lava enchem tanques; balde vazio retira agua/lava.\n'
    '* Quimicos (hidrogenio, oxigenio, acidos, polpas...) saem automaticamente para canos e tanques\n'
    '  vizinhos; para mandar direto a outra maquina, marque a face como saida.\n'
    '* Multiblocos: monte uma caixa oca (minimo 3x3x3) com as carcacas e o controlador em uma\n'
    '  das faces: Induction Matrix (Induction Port + casing + cells/providers dentro),\n'
    '  Dynamic Tank (Dynamic Valve), Thermal Evaporation (Controller, quanto mais alto mais rapido),\n'
    '  Thermoelectric Boiler (Superheating Elements dentro), Industrial Turbine (Turbine Valve,\n'
    '  rotores e bobinas dentro), Fission Reactor (Fuel Assemblies dentro), Fusion Reactor (5x5x5 a 9x9x9)\n'
    '  e SPS (5x5x5).\n'
    '* Teleporter: fique em cima e agache para ir ao proximo teleporter da mesma frequencia.\n'
    '* Jetpack: segure pular; recarregue com hidrogenio num Chemical Tank (slot de baixo).\n'
    '  Energy Tablet, MekaSuit, Free Runners e Atomic Disassembler recarregam num Energy Cube.\n')
print('blocks', len(blocks), 'items', len(items) + len(TIERS) * 9, 'machines', len(machines), 'textures', len(used))
