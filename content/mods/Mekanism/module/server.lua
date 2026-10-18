-- Mekanism for Hellish Sight: world generation (server side).

function main()
    registerOreFeature("osmium_ore_gen", "mekanism:osmium_ore", { size = 8, count = 12, yMin = 0, yMax = 60, target = "minecraft:stone", dimension = 0 })
    registerOreFeature("tin_ore_gen", "mekanism:tin_ore", { size = 9, count = 14, yMin = 0, yMax = 60, target = "minecraft:stone", dimension = 0 })
    registerOreFeature("copper_ore_gen", "mekanism:copper_ore", { size = 9, count = 16, yMin = 0, yMax = 64, target = "minecraft:stone", dimension = 0 })
    registerOreFeature("lead_ore_gen", "mekanism:lead_ore", { size = 9, count = 8, yMin = 0, yMax = 64, target = "minecraft:stone", dimension = 0 })
    registerOreFeature("uranium_ore_gen", "mekanism:uranium_ore", { size = 7, count = 6, yMin = 0, yMax = 48, target = "minecraft:stone", dimension = 0 })
    registerOreFeature("fluorite_ore_gen", "mekanism:fluorite_ore", { size = 7, count = 4, yMin = 0, yMax = 32, target = "minecraft:stone", dimension = 0 })
    log("Mekanism: ore generation registered")
end
