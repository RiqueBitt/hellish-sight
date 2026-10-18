// Hellish Sight - "Shaders": Direct3D 11 port of the screen-space passes of
// MakeUp Ultra Fast by Javier Garduño - GNU LGPL v3.0.
//   https://github.com/javiergcim/MakeUpUltraFast
//   https://modrinth.com/shader/makeup-ultra-fast-shaders
//   https://www.curseforge.com/minecraft/customization/makeup-ultra-fast-shader
//   https://www.planetminecraft.com/mod/makeup-ultra-fast-shader/
// The HLSL below is a translation of MakeUp's ao.glsl (dbao), volumetric_light.glsl
// (ssGodrays), composite / composite1 / final fragment programs, bloom.glsl
// (mipmap_bloom), tone_maps.glsl (custom_sigmoid), dither.glsl and cas.glsl
// (AMD FidelityFX CAS, MIT). See ShaderPack_MakeUp_LICENSE.txt.

#include "stdafx.h"
#include "..\Common\ShaderPack.h"
#include <d3dcompiler.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>

#pragma comment(lib, "d3dcompiler.lib")

extern ID3D11Device *g_pd3dDevice;
extern ID3D11DeviceContext *g_pImmediateContext;

namespace
{
	const char *kSettingFile = "hellish-shaders.cfg";

	template <typename T> void SafeRelease(T *&p)
	{
		if (p) { p->Release(); p = nullptr; }
	}

	// ---------------------------------------------------------------------------------
	// HLSL
	// ---------------------------------------------------------------------------------
	const char *kCommonHLSL =
		"cbuffer ShaderPackCB : register(b0)\n"
		"{\n"
		"    float4 uvXform;   // viewport offset.xy, scale.zw (in texture uv)\n"
		"    float4 texel;     // 1/W, 1/H, W, H of the full textures\n"
		"    float4 depthP;    // projection m10, m11, m14, m15\n"
		"    float4 projP;     // m0, m5, depth value of the far plane, render distance\n"
		"    float4 lightP;    // light uv (viewport space), intensity, softLod\n"
		"    float4 lightDir;  // view space light dir, volumetricDayMixer\n"
		"    float4 volCol;    // volumetric light colour, rain strength\n"
		"    float4 dirCol;    // direct light colour, frame time\n"
		"    float4 misc;      // isEyeInWater, eyeBrightness, exposure lod, unused\n"
		"    float4 flags;     // ao, godrays, bloom, sharpening\n"
		"};\n"
		"SamplerState pointS : register(s0);\n"
		"SamplerState linearS : register(s1);\n"
		"float luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }\n"
		"float linZ(float d) { return abs((depthP.z - d * depthP.w) / (d * depthP.y - depthP.x)); }\n"
		"bool isFar(float d) { return abs(d - projP.z) < 0.000002; }\n"
		"float2 toTex(float2 uv) { return uvXform.xy + saturate(uv) * uvXform.zw; }\n"
		"float rDither(float2 p) { return frac(dot(p, float2(0.75487766624669276, 0.569840290998))); }\n"
		"float hash12(float2 p)\n"
		"{\n"
		"    p = 0.0002314814814814815 * p + float2(0.25, 0.0);\n"
		"    float state = frac(dot(p * p, float2(3571.0, 3571.0)));\n"
		"    return frac(state * state * 7142.0);\n"
		"}\n"
		"float semiblue(float2 p)\n"
		"{\n"
		"    float2 tile = floor(p * 0.25);\n"
		"    float flip = fmod(tile.x + tile.y, 2.0);\n"
		"    p = lerp(p, p.yx, flip);\n"
		"    return frac(dot(float2(0.75487766624669276, 0.569840290998), p) + hash12(tile));\n"
		"}\n";

	const char *kVS =
		"void main(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD0)\n"
		"{\n"
		"    uv = float2((id << 1) & 2, id & 2);\n"
		"    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);\n"
		"}\n";

	// composite: AO (MakeUp applies it in gbuffers), water/lava, ssGodrays, bloom source
	const char *kCompositePS =
		"Texture2D sceneTex : register(t0);\n"
		"Texture2D<float> worldDepth : register(t1);\n"
		"Texture2D<float> handDepth : register(t2);\n"
		"Texture2D expTex : register(t3);\n"
		"struct PSOut { float4 color : SV_Target0; float4 bloom : SV_Target1; };\n"
		"float dbao(float2 tuv, float z, float dither)\n"
		"{\n"
		"    const int AOSTEPS = 5;\n"
		"    const float AO_STRENGTH = 0.70;\n"
		"    float ao = 0.0;\n"
		"    // one block around the pixel, projected to texture uv\n"
		"    float2 scale = float2(projP.x, projP.y) * 0.5 / max(z, 0.05) * uvXform.zw / AOSTEPS;\n"
		"    [unroll] for (int i = 0; i < AOSTEPS; i++)\n"
		"    {\n"
		"        float ds = i + dither;\n"
		"        float n = frac(ds * 1.6180339887) * 3.141592653589793;\n"
		"        float2 off = float2(cos(n), sin(n)) * ds * scale;\n"
		"        float cd = (z - linZ(worldDepth.SampleLevel(pointS, tuv + off, 0))) * 2.0;\n"
		"        float angle = saturate(0.5 - cd);\n"
		"        float dist = saturate(0.25 * cd - 1.0);\n"
		"        cd = (z - linZ(worldDepth.SampleLevel(pointS, tuv - off, 0))) * 2.0;\n"
		"        angle += saturate(0.5 - cd);\n"
		"        dist += saturate(0.25 * cd - 1.0);\n"
		"        ao += saturate(angle + dist);\n"
		"    }\n"
		"    ao /= AOSTEPS;\n"
		"    return sqrt(ao * AO_STRENGTH + (1.0 - AO_STRENGTH));\n"
		"}\n"
		"PSOut main(float4 pos : SV_Position, float2 uv : TEXCOORD0)\n"
		"{\n"
		"    float2 tuv = uvXform.xy + uv * uvXform.zw;\n"
		"    float4 c = sceneTex.SampleLevel(pointS, tuv, 0);\n"
		"    float d = worldDepth.SampleLevel(pointS, tuv, 0);\n"
		"    bool hand = !isFar(handDepth.SampleLevel(pointS, tuv, 0));\n"
		"    bool sky = isFar(d);\n"
		"    float z = linZ(d);\n"
		"    float ldv = saturate(z / projP.w);\n"
		"    float dither = rDither(pos.xy);\n"
		"    if (flags.x > 0.5 && !hand && !sky) c.rgb *= dbao(tuv, z, dither);\n"
		"    if (!hand)\n"
		"    {\n"
		"        if (misc.x > 0.5 && misc.x < 1.5)\n"
		"        {\n"
		"            float waterAbsorption = saturate(1.0 - pow(max(1.001 - ldv, 0.0), 5.0 + 4.0 * 0.10));\n"
		"            float3 waterCol = float3(0.05, 0.1, 0.11) * dirCol.rgb * ((misc.y * 0.8 + 48.0) * 0.004166666666666667);\n"
		"            c.rgb = lerp(c.rgb, waterCol * 6.0, waterAbsorption * 0.5);\n"
		"        }\n"
		"        else if (misc.x > 1.5)\n"
		"        {\n"
		"            c.rgb = lerp(c.rgb, float3(1.0, 0.1, 0.0), saturate(sqrt(z * 0.125)));\n"
		"        }\n"
		"        if (flags.y > 0.5)\n"
		"        {\n"
		"            float2 rayStep = (lightP.xy - uv) * 0.2;\n"
		"            float2 s = uv + rayStep * dither;\n"
		"            float light = 0.0;\n"
		"            [unroll] for (int i = 0; i < 4; i++)\n"
		"            {\n"
		"                light += isFar(worldDepth.SampleLevel(pointS, toTex(s), 0)) ? 1.0 : 0.0;\n"
		"                s += rayStep;\n"
		"            }\n"
		"            light *= 0.25;\n"
		"            float3 eyeDir = normalize(float3((uv.x * 2.0 - 1.0) / projP.x, (1.0 - uv.y * 2.0) / projP.y, 1.0));\n"
		"            float vi = saturate(lightDir.z) * dot(eyeDir, lightDir.xyz);\n"
		"            vi = pow(saturate(vi), lightDir.w) * 0.5 * lightP.z;\n"
		"            c.rgb = lerp(c.rgb, volCol.rgb * light, vi * (light * 0.5 + 0.5) * (1.0 - volCol.w));\n"
		"        }\n"
		"    }\n"
		"    float exposure = expTex.SampleLevel(pointS, float2(0.5, 0.5), 0).r;\n"
		"    if (exposure < 0.01) exposure = 1.0;\n"
		"    float bloomLuma = smoothstep(0.85, 1.0, luma(c.rgb * exposure)) * 0.5;\n"
		"    PSOut o;\n"
		"    o.color = float4(clamp(c.rgb, 0.0, 50.0), 1.0);\n"
		"    o.bloom = float4(o.color.rgb * bloomLuma, 1.0);\n"
		"    return o;\n"
		"}\n";

	// auto exposure (composite_vertex.glsl), written to a 1x1 target
	const char *kExposurePS =
		"Texture2D colorTex : register(t0);\n"
		"Texture2D expTex : register(t1);\n"
		"float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target\n"
		"{\n"
		"    float lod = misc.z;\n"
		"    float3 col = colorTex.SampleLevel(linearS, toTex(float2(0.5, 0.5)), lod).rgb;\n"
		"    col += colorTex.SampleLevel(linearS, toTex(float2(0.25, 0.25)), lod).rgb;\n"
		"    col += colorTex.SampleLevel(linearS, toTex(float2(0.75, 0.75)), lod).rgb;\n"
		"    col += colorTex.SampleLevel(linearS, toTex(float2(0.25, 0.75)), lod).rgb;\n"
		"    col += colorTex.SampleLevel(linearS, toTex(float2(0.75, 0.25)), lod).rgb;\n"
		"    float l = clamp(luma(col), 0.0005, 100.0);\n"
		"    float e = exp(-l) * 3.03 + 0.6;\n"
		"    // the vanilla image is already lit, so only half of MakeUp's correction is used\n"
		"    e = clamp(lerp(1.0, e / 1.1, 0.5), 0.8, 1.4);\n"
		"    float prev = expTex.SampleLevel(pointS, float2(0.5, 0.5), 0).r;\n"
		"    if (prev > 0.01) e = lerp(e, prev, exp(-dirCol.w * 1.25));\n"
		"    return float4(e, e, e, 1.0);\n"
		"}\n";

	// final: CAS, bloom add (composite1), exposure, custom_sigmoid tonemap
	const char *kFinalPS =
		"Texture2D colorTex : register(t0);\n"
		"Texture2D bloomTex : register(t1);\n"
		"Texture2D expTex : register(t2);\n"
		"float3 fetch(float2 t) { return colorTex.SampleLevel(pointS, t, 0).rgb; }\n"
		"float3 CAS(float2 t)\n"
		"{\n"
		"    float px = texel.x, py = texel.y;\n"
		"    float3 e = fetch(t);\n"
		"    float3 left = fetch(t + float2(-px, 0.0));\n"
		"    float3 right = fetch(t + float2(px, 0.0));\n"
		"    float3 down = fetch(t + float2(0.0, py));\n"
		"    float3 up = fetch(t + float2(0.0, -py));\n"
		"    float3 ul = fetch(t + float2(-px, -py));\n"
		"    float3 ur = fetch(t + float2(px, -py));\n"
		"    float3 dl = fetch(t + float2(-px, py));\n"
		"    float3 dr = fetch(t + float2(px, py));\n"
		"    float3 mnRGB = min(min(min(left, e), min(right, up)), down);\n"
		"    float3 mnRGB2 = min(min(min(mnRGB, ul), min(dl, ur)), dr);\n"
		"    mnRGB += mnRGB2;\n"
		"    float3 mxRGB = max(max(max(left, e), max(right, up)), down);\n"
		"    float3 mxRGB2 = max(max(max(mxRGB, ul), max(dl, ur)), dr);\n"
		"    mxRGB += mxRGB2;\n"
		"    float3 rcpMxRGB = 1.0 / max(mxRGB, 0.0001);\n"
		"    float3 ampRGB = saturate(min(mnRGB, 2.0 - mxRGB) * rcpMxRGB);\n"
		"    ampRGB = rsqrt(max(ampRGB, 0.0001));\n"
		"    const float SHARPENING = 0.5;\n"
		"    float peak = 8.0 - 3.0 * SHARPENING;\n"
		"    float3 wRGB = -1.0 / (ampRGB * peak);\n"
		"    float3 rcpWeightRGB = 1.0 / (1.0 + 4.0 * wRGB);\n"
		"    float3 window = (up + down) + (left + right);\n"
		"    return saturate((window * wRGB + e) * rcpWeightRGB);\n"
		"}\n"
		"float3 mipmapBloom(float2 uv, float dither)\n"
		"{\n"
		"    const int BLOOM_SAMPLES = 3;\n"
		"    float aspectInv = (texel.w * uvXform.w) / (texel.z * uvXform.z);\n"
		"    float2 radius = float2(0.1 * aspectInv, 0.1) * (1.0 / BLOOM_SAMPLES);\n"
		"    float3 blur = 0.0;\n"
		"    [unroll] for (int i = 0; i < BLOOM_SAMPLES; i++)\n"
		"    {\n"
		"        float ds = i + dither;\n"
		"        float n = frac(ds * 1.6180339887) * 6.283185307179586;\n"
		"        float2 o1 = float2(cos(n), sin(n)) * ds * radius;\n"
		"        float2 o2 = float2(-o1.y * 1.25, o1.x * 1.25);\n"
		"        blur += bloomTex.SampleLevel(linearS, toTex(uv + o1), lightP.w).rgb;\n"
		"        blur += bloomTex.SampleLevel(linearS, toTex(uv - o1), lightP.w).rgb;\n"
		"        blur += bloomTex.SampleLevel(linearS, toTex(uv + o2), lightP.w).rgb;\n"
		"        blur += bloomTex.SampleLevel(linearS, toTex(uv - o2), lightP.w).rgb;\n"
		"    }\n"
		"    return blur / (BLOOM_SAMPLES * 4.0);\n"
		"}\n"
		"float3 customSigmoid(float3 color)\n"
		"{\n"
		"    color = 1.4 * max(color, 0.0);\n"
		"    color = color / pow(pow(color, 2.5) + 1.0, 0.4);\n"
		"    return pow(color, 1.15);\n"
		"}\n"
		"float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target\n"
		"{\n"
		"    float2 tuv = uvXform.xy + uv * uvXform.zw;\n"
		"    float3 c = flags.w > 0.5 ? CAS(tuv) : fetch(tuv);\n"
		"    if (flags.z > 0.5) c += mipmapBloom(uv, semiblue(pos.xy));\n"
		"    float exposure = expTex.SampleLevel(pointS, float2(0.5, 0.5), 0).r;\n"
		"    if (exposure < 0.01) exposure = 1.0;\n"
		"    c = customSigmoid(c * exposure);\n"
		"    return float4(c, 1.0);\n"
		"}\n";

	struct CBData
	{
		float uvXform[4];
		float texel[4];
		float depthP[4];
		float projP[4];
		float lightP[4];
		float lightDir[4];
		float volCol[4];
		float dirCol[4];
		float misc[4];
		float flags[4];
	};

	struct DepthCopy
	{
		ID3D11Texture2D *tex = nullptr;
		ID3D11ShaderResourceView *srv = nullptr;
		DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
		UINT w = 0, h = 0;
		void release() { SafeRelease(srv); SafeRelease(tex); fmt = DXGI_FORMAT_UNKNOWN; w = h = 0; }
	};

	struct RenderTex
	{
		ID3D11Texture2D *tex = nullptr;
		ID3D11ShaderResourceView *srv = nullptr;
		ID3D11RenderTargetView *rtv = nullptr;
		void release() { SafeRelease(rtv); SafeRelease(srv); SafeRelease(tex); }
		bool create(UINT w, UINT h, DXGI_FORMAT fmt, bool mips)
		{
			D3D11_TEXTURE2D_DESC td = {};
			td.Width = w;
			td.Height = h;
			td.MipLevels = mips ? 0 : 1;
			td.ArraySize = 1;
			td.Format = fmt;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
			td.MiscFlags = mips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
			if (FAILED(g_pd3dDevice->CreateTexture2D(&td, nullptr, &tex))) return false;
			if (FAILED(g_pd3dDevice->CreateShaderResourceView(tex, nullptr, &srv))) return false;
			D3D11_RENDER_TARGET_VIEW_DESC rd = {};
			rd.Format = fmt;
			rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			rd.Texture2D.MipSlice = 0;
			return SUCCEEDED(g_pd3dDevice->CreateRenderTargetView(tex, &rd, &rtv));
		}
	};

	bool DepthFormats(DXGI_FORMAT dsFormat, DXGI_FORMAT &typeless, DXGI_FORMAT &srvFormat)
	{
		switch (dsFormat)
		{
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24G8_TYPELESS:
			typeless = DXGI_FORMAT_R24G8_TYPELESS; srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; return true;
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R32_TYPELESS:
			typeless = DXGI_FORMAT_R32_TYPELESS; srvFormat = DXGI_FORMAT_R32_FLOAT; return true;
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32G8X24_TYPELESS:
			typeless = DXGI_FORMAT_R32G8X24_TYPELESS; srvFormat = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS; return true;
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_R16_TYPELESS:
			typeless = DXGI_FORMAT_R16_TYPELESS; srvFormat = DXGI_FORMAT_R16_UNORM; return true;
		default:
			return false;
		}
	}

	DXGI_FORMAT TypelessColor(DXGI_FORMAT f)
	{
		switch (f)
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			return DXGI_FORMAT_R8G8B8A8_TYPELESS;
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
			return DXGI_FORMAT_B8G8R8A8_TYPELESS;
		default:
			return f;
		}
	}

	// Saves and restores every piece of pipeline state the passes touch, so the 4J
	// renderer (which caches its own state) continues exactly where it was.
	struct StateBackup
	{
		ID3D11RenderTargetView *rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		ID3D11DepthStencilView *dsv = nullptr;
		D3D11_VIEWPORT vps[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
		UINT numVps = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		ID3D11InputLayout *layout = nullptr;
		D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		ID3D11VertexShader *vs = nullptr;
		ID3D11ClassInstance *vsInst[256];
		UINT vsInstCount = 256;
		ID3D11PixelShader *ps = nullptr;
		ID3D11ClassInstance *psInst[256];
		UINT psInstCount = 256;
		ID3D11GeometryShader *gs = nullptr;
		ID3D11ShaderResourceView *srvs[4] = {};
		ID3D11SamplerState *samplers[2] = {};
		ID3D11Buffer *psCB = nullptr;
		ID3D11RasterizerState *rs = nullptr;
		ID3D11DepthStencilState *ds = nullptr;
		UINT stencilRef = 0;
		ID3D11BlendState *bs = nullptr;
		FLOAT blendFactor[4] = {};
		UINT sampleMask = 0xFFFFFFFF;

		void save(ID3D11DeviceContext *c)
		{
			c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);
			c->RSGetViewports(&numVps, vps);
			c->IAGetInputLayout(&layout);
			c->IAGetPrimitiveTopology(&topo);
			c->VSGetShader(&vs, vsInst, &vsInstCount);
			c->PSGetShader(&ps, psInst, &psInstCount);
			c->GSGetShader(&gs, nullptr, nullptr);
			c->PSGetShaderResources(0, 4, srvs);
			c->PSGetSamplers(0, 2, samplers);
			c->PSGetConstantBuffers(0, 1, &psCB);
			c->RSGetState(&rs);
			c->OMGetDepthStencilState(&ds, &stencilRef);
			c->OMGetBlendState(&bs, blendFactor, &sampleMask);
		}

		void restore(ID3D11DeviceContext *c)
		{
			c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, dsv);
			c->RSSetViewports(numVps, vps);
			c->IASetInputLayout(layout);
			c->IASetPrimitiveTopology(topo);
			c->VSSetShader(vs, vsInst, vsInstCount);
			c->PSSetShader(ps, psInst, psInstCount);
			c->GSSetShader(gs, nullptr, 0);
			c->PSSetShaderResources(0, 4, srvs);
			c->PSSetSamplers(0, 2, samplers);
			c->PSSetConstantBuffers(0, 1, &psCB);
			c->RSSetState(rs);
			c->OMSetDepthStencilState(ds, stencilRef);
			c->OMSetBlendState(bs, blendFactor, sampleMask);

			for (auto &r : rtvs) SafeRelease(r);
			SafeRelease(dsv);
			SafeRelease(layout);
			SafeRelease(vs);
			for (UINT i = 0; i < vsInstCount; i++) SafeRelease(vsInst[i]);
			SafeRelease(ps);
			for (UINT i = 0; i < psInstCount; i++) SafeRelease(psInst[i]);
			SafeRelease(gs);
			for (auto &s : srvs) SafeRelease(s);
			for (auto &s : samplers) SafeRelease(s);
			SafeRelease(psCB);
			SafeRelease(rs);
			SafeRelease(ds);
			SafeRelease(bs);
		}
	};
}

struct ShaderPack::Impl
{
	ID3D11VertexShader *vs = nullptr;
	ID3D11PixelShader *psComposite = nullptr;
	ID3D11PixelShader *psExposure = nullptr;
	ID3D11PixelShader *psFinal = nullptr;
	ID3D11Buffer *cb = nullptr;
	ID3D11SamplerState *pointS = nullptr;
	ID3D11SamplerState *linearS = nullptr;
	ID3D11RasterizerState *rs = nullptr;
	ID3D11DepthStencilState *ds = nullptr;
	ID3D11BlendState *bs = nullptr;

	// colour copy of the frame
	ID3D11Texture2D *scene = nullptr;
	ID3D11ShaderResourceView *sceneSRV = nullptr;
	DXGI_FORMAT sceneFmt = DXGI_FORMAT_UNKNOWN;

	RenderTex color;	// composite output (mipped: exposure + bloom)
	RenderTex bloom;	// bloom source (mipped)
	RenderTex exposure[2];
	int expIndex = 0;
	UINT width = 0, height = 0;

	DepthCopy depth[2];	// 0 = world (before the hand), 1 = after the hand

	LARGE_INTEGER freq = {};
	LARGE_INTEGER last = {};
};

ShaderPack &ShaderPack::GetInstance()
{
	static ShaderPack instance;
	return instance;
}

ShaderPack::ShaderPack()
{
	memset(m_projection, 0, sizeof(m_projection));
	m_impl = new Impl();
	QueryPerformanceFrequency(&m_impl->freq);
	LoadSetting();
}

ShaderPack::~ShaderPack()
{
	Cleanup();
	delete m_impl;
	m_impl = nullptr;
}

void ShaderPack::LoadSetting()
{
	FILE *f = nullptr;
	if (fopen_s(&f, kSettingFile, "r") != 0 || f == nullptr) return;
	char line[64] = {};
	while (fgets(line, sizeof(line), f))
	{
		if (strncmp(line, "enabled=", 8) == 0) m_enabled = line[8] == '1';
	}
	fclose(f);
}

void ShaderPack::SaveSetting() const
{
	FILE *f = nullptr;
	if (fopen_s(&f, kSettingFile, "w") != 0 || f == nullptr) return;
	fprintf(f, "# Hellish Sight shaders (MakeUp Ultra Fast port). F7 toggles in game.\nenabled=%d\n", m_enabled ? 1 : 0);
	fclose(f);
}

void ShaderPack::SetEnabled(bool enabled)
{
	if (m_enabled == enabled) return;
	m_enabled = enabled;
	m_failed = false;	// allow a retry after a failure
	SaveSetting();
}

void ShaderPack::CaptureProjection(const float *matrix)
{
	if (matrix == nullptr) return;
	memcpy(m_projection, matrix, sizeof(m_projection));
	m_haveProjection = true;
}

bool ShaderPack::CompileShaders()
{
	Impl &d = *m_impl;
	if (d.vs) return true;

	auto compile = [](const char *body, bool common, const char *name, const char *target, ID3DBlob **out) -> bool
	{
		std::string src = common ? std::string(kCommonHLSL) + body : std::string(body);
		ID3DBlob *err = nullptr;
		HRESULT hr = D3DCompile(src.c_str(), src.size(), name, nullptr, nullptr, "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err);
		if (err)
		{
			if (FAILED(hr)) OutputDebugStringA(static_cast<const char *>(err->GetBufferPointer()));
			err->Release();
		}
		return SUCCEEDED(hr);
	};

	ID3DBlob *blob = nullptr;
	if (!compile(kVS, false, "HS_ShaderVS", "vs_4_0", &blob)) return false;
	HRESULT hr = g_pd3dDevice->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &d.vs);
	blob->Release();
	if (FAILED(hr)) return false;

	struct { const char *src; const char *name; ID3D11PixelShader **out; } ps[] =
	{
		{ kCompositePS, "HS_Composite", &d.psComposite },
		{ kExposurePS, "HS_Exposure", &d.psExposure },
		{ kFinalPS, "HS_Final", &d.psFinal },
	};
	for (auto &p : ps)
	{
		blob = nullptr;
		if (!compile(p.src, true, p.name, "ps_4_0", &blob)) return false;
		hr = g_pd3dDevice->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, p.out);
		blob->Release();
		if (FAILED(hr)) return false;
	}

	D3D11_BUFFER_DESC cbd = {};
	cbd.ByteWidth = sizeof(CBData);
	cbd.Usage = D3D11_USAGE_DYNAMIC;
	cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	if (FAILED(g_pd3dDevice->CreateBuffer(&cbd, nullptr, &d.cb))) return false;

	D3D11_SAMPLER_DESC sd = {};
	sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.MaxLOD = D3D11_FLOAT32_MAX;
	if (FAILED(g_pd3dDevice->CreateSamplerState(&sd, &d.pointS))) return false;
	sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	if (FAILED(g_pd3dDevice->CreateSamplerState(&sd, &d.linearS))) return false;

	D3D11_RASTERIZER_DESC rd = {};
	rd.FillMode = D3D11_FILL_SOLID;
	rd.CullMode = D3D11_CULL_NONE;
	rd.DepthClipEnable = FALSE;
	if (FAILED(g_pd3dDevice->CreateRasterizerState(&rd, &d.rs))) return false;

	D3D11_DEPTH_STENCIL_DESC dsd = {};
	dsd.DepthEnable = FALSE;
	dsd.StencilEnable = FALSE;
	if (FAILED(g_pd3dDevice->CreateDepthStencilState(&dsd, &d.ds))) return false;

	D3D11_BLEND_DESC bd = {};
	bd.RenderTarget[0].BlendEnable = FALSE;
	bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	bd.RenderTarget[1] = bd.RenderTarget[0];
	if (FAILED(g_pd3dDevice->CreateBlendState(&bd, &d.bs))) return false;

	for (int i = 0; i < 2; i++)
	{
		if (!d.exposure[i].create(1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, false)) return false;
		const float zero[4] = { 0, 0, 0, 0 };
		g_pImmediateContext->ClearRenderTargetView(d.exposure[i].rtv, zero);
	}
	return true;
}

void ShaderPack::ReleaseTargets()
{
	Impl &d = *m_impl;
	SafeRelease(d.sceneSRV);
	SafeRelease(d.scene);
	d.sceneFmt = DXGI_FORMAT_UNKNOWN;
	d.color.release();
	d.bloom.release();
	d.width = d.height = 0;
}

bool ShaderPack::EnsureResources(unsigned int width, unsigned int height)
{
	Impl &d = *m_impl;
	if (d.width == width && d.height == height && d.color.tex) return true;
	ReleaseTargets();
	if (!d.color.create(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, true)) return false;
	if (!d.bloom.create(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, true)) return false;
	d.width = width;
	d.height = height;
	return true;
}

bool ShaderPack::EnsureDepthCopy(void *depthResource, int slot)
{
	Impl &d = *m_impl;
	ID3D11Texture2D *src = static_cast<ID3D11Texture2D *>(depthResource);
	D3D11_TEXTURE2D_DESC sdsc;
	src->GetDesc(&sdsc);
	if (sdsc.SampleDesc.Count != 1) return false;
	DXGI_FORMAT typeless, srvFmt;
	if (!DepthFormats(sdsc.Format, typeless, srvFmt)) return false;

	DepthCopy &dc = d.depth[slot];
	if (dc.tex && dc.fmt == typeless && dc.w == sdsc.Width && dc.h == sdsc.Height) return true;
	dc.release();

	D3D11_TEXTURE2D_DESC td = {};
	td.Width = sdsc.Width;
	td.Height = sdsc.Height;
	td.MipLevels = 1;
	td.ArraySize = 1;
	td.Format = typeless;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	if (FAILED(g_pd3dDevice->CreateTexture2D(&td, nullptr, &dc.tex))) return false;
	D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
	vd.Format = srvFmt;
	vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	vd.Texture2D.MipLevels = 1;
	if (FAILED(g_pd3dDevice->CreateShaderResourceView(dc.tex, &vd, &dc.srv))) { dc.release(); return false; }
	dc.fmt = typeless;
	dc.w = sdsc.Width;
	dc.h = sdsc.Height;
	return true;
}

void ShaderPack::CaptureWorldDepth()
{
	m_haveWorldDepth = false;
	if (!m_enabled || m_failed || !g_pImmediateContext) return;

	ID3D11DepthStencilView *dsv = nullptr;
	g_pImmediateContext->OMGetRenderTargets(0, nullptr, &dsv);
	if (!dsv) return;
	ID3D11Resource *res = nullptr;
	dsv->GetResource(&res);
	dsv->Release();
	if (!res) return;
	if (EnsureDepthCopy(res, 0))
	{
		g_pImmediateContext->CopyResource(m_impl->depth[0].tex, res);
		m_haveWorldDepth = true;
	}
	res->Release();
}

void ShaderPack::Apply(const ShaderFrameParams &p)
{
	if (!m_enabled || m_failed || !m_haveWorldDepth || !m_haveProjection || !g_pd3dDevice || !g_pImmediateContext) return;
	m_haveWorldDepth = false;
	Impl &d = *m_impl;
	ID3D11DeviceContext *ctx = g_pImmediateContext;

	if (!CompileShaders())
	{
		m_failed = true;
		return;
	}

	StateBackup state;
	state.save(ctx);

	ID3D11RenderTargetView *target = state.rtvs[0];
	if (!target || state.numVps == 0)
	{
		state.restore(ctx);
		return;
	}

	ID3D11Resource *targetRes = nullptr;
	target->GetResource(&targetRes);
	ID3D11Texture2D *targetTex = static_cast<ID3D11Texture2D *>(targetRes);
	D3D11_TEXTURE2D_DESC tdsc;
	targetTex->GetDesc(&tdsc);

	bool ok = tdsc.SampleDesc.Count == 1 && EnsureResources(tdsc.Width, tdsc.Height);

	// colour copy of the target
	if (ok && (!d.scene || d.sceneFmt != tdsc.Format))
	{
		SafeRelease(d.sceneSRV);
		SafeRelease(d.scene);
		D3D11_TEXTURE2D_DESC sd = {};
		sd.Width = tdsc.Width;
		sd.Height = tdsc.Height;
		sd.MipLevels = 1;
		sd.ArraySize = 1;
		sd.Format = TypelessColor(tdsc.Format);
		sd.SampleDesc.Count = 1;
		sd.Usage = D3D11_USAGE_DEFAULT;
		sd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
		vd.Format = (tdsc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || tdsc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) ? tdsc.Format :
			(sd.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS ? DXGI_FORMAT_R8G8B8A8_UNORM : (sd.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS ? DXGI_FORMAT_B8G8R8A8_UNORM : tdsc.Format));
		vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		vd.Texture2D.MipLevels = 1;
		ok = SUCCEEDED(g_pd3dDevice->CreateTexture2D(&sd, nullptr, &d.scene)) &&
			SUCCEEDED(g_pd3dDevice->CreateShaderResourceView(d.scene, &vd, &d.sceneSRV));
		d.sceneFmt = ok ? tdsc.Format : DXGI_FORMAT_UNKNOWN;
	}

	// depth after the hand (only the hand differs from the far plane)
	bool haveHand = false;
	if (ok && state.dsv)
	{
		ID3D11Resource *dres = nullptr;
		state.dsv->GetResource(&dres);
		if (dres)
		{
			if (EnsureDepthCopy(dres, 1))
			{
				ctx->CopyResource(d.depth[1].tex, dres);
				haveHand = true;
			}
			dres->Release();
		}
	}

	if (!ok)
	{
		targetRes->Release();
		state.restore(ctx);
		m_failed = true;
		return;
	}

	ctx->CopyResource(d.scene, targetRes);
	targetRes->Release();

	// ---- constants ----
	const D3D11_VIEWPORT vp = state.vps[0];
	const float texW = static_cast<float>(d.width), texH = static_cast<float>(d.height);
	CBData cb = {};
	cb.uvXform[0] = vp.TopLeftX / texW;
	cb.uvXform[1] = vp.TopLeftY / texH;
	cb.uvXform[2] = vp.Width / texW;
	cb.uvXform[3] = vp.Height / texH;
	cb.texel[0] = 1.0f / texW;
	cb.texel[1] = 1.0f / texH;
	cb.texel[2] = texW;
	cb.texel[3] = texH;

	// projection (row-vector layout: clip = v * M). Some renderers store the transpose.
	const float *m = m_projection;
	float m10 = m[10], m11 = m[11], m14 = m[14], m15 = m[15];
	if (fabsf(m11) < 0.5f && fabsf(m14) > 0.5f)
	{
		m11 = m[14];
		m14 = m[11];
	}
	cb.depthP[0] = m10;
	cb.depthP[1] = m11;
	cb.depthP[2] = m14;
	cb.depthP[3] = m15;
	const float p0 = fabsf(m[0]) > 0.0001f ? fabsf(m[0]) : 1.0f;
	const float p5 = fabsf(m[5]) > 0.0001f ? fabsf(m[5]) : 1.0f;
	cb.projP[0] = p0;
	cb.projP[1] = p5;
	// depth stored for the far plane: 1 normally, 0 with reversed depth
	{
		const float zNear = fabsf(m14 / (m10 != 0.0f ? m10 : 1.0f));
		const float dNear = (m11 * -zNear != 0.0f) ? (m10 * -zNear + m14) / (m11 * -zNear + m15) : 0.0f;
		cb.projP[2] = dNear > 0.5f ? 0.0f : 1.0f;
		// zFar from the matrix; MakeUp's "far" is the render distance (half of it)
		const float dF = cb.projP[2];
		const float denom = dF * m11 - m10;
		const float zFar = fabsf(denom) > 1e-6f ? fabsf((m14 - dF * m15) / denom) : 256.0f;
		cb.projP[3] = zFar > 1.0f && zFar < 100000.0f ? zFar * 0.5f : 128.0f;
	}

	// light position in viewport uv
	float lx = p.lightDirView[0], ly = p.lightDirView[1], lz = p.lightDirView[2];
	float lightU, lightV;
	if (lz > 0.01f)
	{
		lightU = 0.5f + 0.5f * (lx / lz) * p0;
		lightV = 0.5f - 0.5f * (ly / lz) * p5;
	}
	else
	{
		const float len = sqrtf(lx * lx + ly * ly) + 1e-4f;
		lightU = 0.5f + 10.0f * lx / len;
		lightV = 0.5f - 10.0f * ly / len;
	}
	const float vpH = vp.Height > 1.0f ? vp.Height : texH;
	cb.lightP[0] = lightU;
	cb.lightP[1] = lightV;
	cb.lightP[2] = p.lightIntensity;
	cb.lightP[3] = (std::max)(0.0f, log2f((std::max)(1.0f, vpH / 128.0f)));	// softLod
	cb.lightDir[0] = lx;
	cb.lightDir[1] = ly;
	cb.lightDir[2] = lz;
	cb.lightDir[3] = p.volumetricDayMixer;
	cb.volCol[0] = p.volumetricColor[0];
	cb.volCol[1] = p.volumetricColor[1];
	cb.volCol[2] = p.volumetricColor[2];
	cb.volCol[3] = p.rainStrength;
	cb.dirCol[0] = p.volumetricColor[0] / 1.2f;
	cb.dirCol[1] = p.volumetricColor[1] / 1.2f;
	cb.dirCol[2] = p.volumetricColor[2] / 1.2f;

	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	float frameTime = p.frameTime;
	if (frameTime <= 0.0f)
	{
		frameTime = d.last.QuadPart ? static_cast<float>(now.QuadPart - d.last.QuadPart) / static_cast<float>(d.freq.QuadPart) : 0.016f;
		if (frameTime > 1.0f) frameTime = 1.0f;
	}
	d.last = now;
	cb.dirCol[3] = frameTime;
	cb.misc[0] = static_cast<float>(p.isEyeInWater);
	cb.misc[1] = p.eyeBrightness;
	cb.misc[2] = (std::max)(0.0f, log2f((std::max)(1.0f, (std::min)(vp.Width, vp.Height))) - 1.0f);
	cb.flags[0] = 1.0f;							// AO
	cb.flags[1] = p.godrays ? 1.0f : 0.0f;		// VOL_LIGHT 1
	cb.flags[2] = 1.0f;							// BLOOM
	cb.flags[3] = 1.0f;							// sharpening (CAS 0.5)

	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(ctx->Map(d.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
	{
		state.restore(ctx);
		return;
	}
	memcpy(mapped.pData, &cb, sizeof(cb));
	ctx->Unmap(d.cb, 0);

	// ---- common state ----
	ID3D11ShaderResourceView *nullSRV[4] = {};
	ID3D11SamplerState *samplers[2] = { d.pointS, d.linearS };
	const float blendFactor[4] = { 0, 0, 0, 0 };
	ctx->IASetInputLayout(nullptr);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->VSSetShader(d.vs, nullptr, 0);
	ctx->GSSetShader(nullptr, nullptr, 0);
	ctx->PSSetSamplers(0, 2, samplers);
	ctx->PSSetConstantBuffers(0, 1, &d.cb);
	ctx->RSSetState(d.rs);
	ctx->OMSetDepthStencilState(d.ds, 0);
	ctx->OMSetBlendState(d.bs, blendFactor, 0xFFFFFFFF);

	const int prevExp = d.expIndex;
	const int curExp = 1 - d.expIndex;

	// 1) composite -> color + bloom source
	{
		ID3D11RenderTargetView *rtvs[2] = { d.color.rtv, d.bloom.rtv };
		ctx->OMSetRenderTargets(2, rtvs, nullptr);
		ctx->RSSetViewports(1, &vp);
		ID3D11ShaderResourceView *srvs[4] = { d.sceneSRV, d.depth[0].srv, haveHand ? d.depth[1].srv : d.depth[0].srv, d.exposure[prevExp].srv };
		ctx->PSSetShaderResources(0, 4, srvs);
		ctx->PSSetShader(d.psComposite, nullptr, 0);
		ctx->Draw(3, 0);
		ctx->PSSetShaderResources(0, 4, nullSRV);
		ID3D11RenderTargetView *nullRTV[2] = {};
		ctx->OMSetRenderTargets(2, nullRTV, nullptr);
	}
	ctx->GenerateMips(d.color.srv);
	ctx->GenerateMips(d.bloom.srv);

	// 2) exposure (1x1, smoothed with the previous value)
	{
		ctx->OMSetRenderTargets(1, &d.exposure[curExp].rtv, nullptr);
		D3D11_VIEWPORT one = { 0, 0, 1, 1, 0, 1 };
		ctx->RSSetViewports(1, &one);
		ID3D11ShaderResourceView *srvs[2] = { d.color.srv, d.exposure[prevExp].srv };
		ctx->PSSetShaderResources(0, 2, srvs);
		ctx->PSSetShader(d.psExposure, nullptr, 0);
		ctx->Draw(3, 0);
		ctx->PSSetShaderResources(0, 4, nullSRV);
		d.expIndex = curExp;
	}

	// 3) final -> the game's render target, same viewport
	{
		ctx->OMSetRenderTargets(1, &target, nullptr);
		ctx->RSSetViewports(1, &vp);
		ID3D11ShaderResourceView *srvs[3] = { d.color.srv, d.bloom.srv, d.exposure[curExp].srv };
		ctx->PSSetShaderResources(0, 3, srvs);
		ctx->PSSetShader(d.psFinal, nullptr, 0);
		ctx->Draw(3, 0);
		ctx->PSSetShaderResources(0, 4, nullSRV);
	}

	state.restore(ctx);
}

void ShaderPack::Cleanup()
{
	if (!m_impl) return;
	Impl &d = *m_impl;
	ReleaseTargets();
	for (auto &e : d.exposure) e.release();
	for (auto &dc : d.depth) dc.release();
	SafeRelease(d.bs);
	SafeRelease(d.ds);
	SafeRelease(d.rs);
	SafeRelease(d.linearS);
	SafeRelease(d.pointS);
	SafeRelease(d.cb);
	SafeRelease(d.psFinal);
	SafeRelease(d.psExposure);
	SafeRelease(d.psComposite);
	SafeRelease(d.vs);
}
