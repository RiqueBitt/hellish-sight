#pragma once

// Hellish Sight - "Distant Terrain" (LOD): renders a simplified version of the
// terrain far beyond the normal render distance, in the spirit of Voxy / Distant
// Horizons. This is an original implementation for this engine (C++ / D3D11); no
// code from those mods is used.
//
//  * Ingestion: every chunk the client has loaded is sampled into 2x2-block cells
//    (top height, surface colour with biome tint, water level) on the main thread.
//  * Storage: cells live in 32x32-chunk regions, kept in memory around the player
//    and saved to hellish-lod/<world>/<dimension>/ so explored terrain stays visible
//    the next time the world is opened.
//  * Meshing: 64x64-block sections are meshed on a worker thread as blocky
//    heightfields (tops + walls) with coarser cells further away (2 to 16 blocks).
//  * Rendering: drawn into the same colour and depth buffers as the world, with the
//    world projection, so the MakeUp shader passes (AO, god rays, bloom, exposure)
//    and the fog treat it like normal terrain. The area covered by real chunks is
//    clipped in the pixel shader, so the two never overlap.
//
// Settings: Settings > Graphics > "Terreno distante" (0 = off, up to 256 chunks), F8
// toggles it in game. Stored in hellish-settings.cfg (lodDistance, lodEnabled).

class Minecraft;

namespace HSLod
{
	static const int kMaxDistanceChunks = 256;
	static const int kSliderStep = 8;		// slider unit = 8 chunks

	int GetDistanceChunks();				// saved setting, 0 = off
	void SetDistanceChunks(int chunks);
	bool IsEnabled();						// F8 toggle (and distance > 0)
	void Toggle();

	// Distance in blocks the LOD reaches this frame, 0 when inactive (setting off,
	// Nether/End, no level). Used to extend the far plane and the fog.
	float ActiveDistanceBlocks(Minecraft *mc);

	// Main thread, once per client tick: samples loaded chunks.
	void Tick(Minecraft *mc);

	// Main thread, inside GameRenderer::renderLevel before the terrain is drawn.
	void Render(Minecraft *mc, double camX, double camY, double camZ, float a);

	// World left: saves and frees everything.
	void Reset();
}
