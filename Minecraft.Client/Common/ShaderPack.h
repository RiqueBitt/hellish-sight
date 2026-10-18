#pragma once

// Hellish Sight - "Shaders": a Direct3D 11 port of the screen-space passes of the
// MakeUp Ultra Fast shaderpack by Javier Garduño, licensed under the
// GNU LGPL v3.0. Official pages:
//   https://github.com/javiergcim/MakeUpUltraFast
//   https://modrinth.com/shader/makeup-ultra-fast-shaders
//   https://www.curseforge.com/minecraft/customization/makeup-ultra-fast-shader
//   https://www.planetminecraft.com/mod/makeup-ultra-fast-shader/
//
// Ported passes: depth based ambient occlusion (dbao), screen-space god rays
// (ssGodrays), underwater / lava tint, mipmap bloom, auto exposure, the
// custom_sigmoid tonemap and AMD CAS sharpening. Toggle: Settings > Graphics >
// "Shaders" or F7 in game. The setting is stored in hellish-shaders.cfg.

struct ShaderFrameParams
{
	float projection[16];		// projection matrix used for the world (row-vector layout)
	float lightDirView[3];		// sun/moon direction in view space (x right, y up, z forward)
	float lightIntensity;		// abs(dayNightMix * 2 - 1)
	float volumetricColor[3];
	float volumetricDayMixer;
	float rainStrength;
	float eyeBrightness;		// 0..240 (sky light at the eyes * 16)
	float frameTime;			// seconds
	int isEyeInWater;			// 0 air, 1 water, 2 lava
	bool godrays;				// false in the Nether / End
};

class ShaderPack
{
public:
	static ShaderPack &GetInstance();

	bool IsEnabled() const { return m_enabled; }
	void SetEnabled(bool enabled);	// saves the setting
	void Toggle() { SetEnabled(!m_enabled); }

	// Called by GameRenderer::renderLevel
	void CaptureProjection(const float *matrix);
	void CaptureWorldDepth();			// before the depth clear for the hand
	void Apply(const ShaderFrameParams &params);	// after the hand was drawn

	void Cleanup();

private:
	ShaderPack();
	~ShaderPack();
	ShaderPack(const ShaderPack &) = delete;
	ShaderPack &operator=(const ShaderPack &) = delete;

	bool EnsureResources(unsigned int width, unsigned int height);
	bool EnsureDepthCopy(void *depthResource, int slot);
	bool CompileShaders();
	void ReleaseTargets();
	void LoadSetting();
	void SaveSetting() const;

	bool m_enabled = false;
	bool m_failed = false;
	bool m_haveWorldDepth = false;
	float m_projection[16];
	bool m_haveProjection = false;

	struct Impl;
	Impl *m_impl = nullptr;
};
