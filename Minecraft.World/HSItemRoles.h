#pragma once
// Hellish Sight: special behaviour of mod wearables / tools, set from Lua with
// registerItemRole (Mekanism: free runners, jetpack, gas mask, MekaSuit...).

enum HSItemRole
{
	HSRole_FreeRunners = 1 << 0,	// boots: no fall damage
	HSRole_Jetpack     = 1 << 1,	// chest: hold jump to fly, uses the item's durability as fuel
	HSRole_GasMask     = 1 << 2,	// helmet: breathe under water
	HSRole_NightVision = 1 << 3,	// helmet: lets the player see in the dark (handled by the mod)
};

inline unsigned int *hsItemRoleTable()
{
	static unsigned int s_roles[32000] = {};
	return s_roles;
}

inline unsigned int hsItemRoles(int id)
{
	return (id > 0 && id < 32000) ? hsItemRoleTable()[id] : 0u;
}

inline void hsSetItemRole(int id, unsigned int role)
{
	if (id > 0 && id < 32000) hsItemRoleTable()[id] |= role;
}
