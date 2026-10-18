#include "stdafx.h"
#include "MeshRender.h"

#include "HSMesh.h"
#include "Minecraft.h"
#include "Textures.h"
#include "Tesselator.h"
#include "BufferedImage.h"
#include "Lighting.h"
#include "TileEntityRenderer.h"
#include "HumanoidModel.h"
#include "ModelPart.h"
#include "net.minecraft.world.level.h"
#include "net.minecraft.world.level.tile.entity.h"
#include "net.minecraft.world.entity.h"
#include "net.minecraft.world.item.h"
#include "Common/ModPaths.h"
#include "Loader.h"
#include "json/json.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace HSMeshRender
{
	namespace
	{
		const float RAD2DEG = 57.29577951f;

		std::string readFile(const std::string &path)
		{
			std::ifstream in(path, std::ios::binary);
			std::stringstream ss;
			ss << in.rdbuf();
			return ss.str();
		}

		float num(const nlohmann::json &j, float fallback)
		{
			return j.is_number() ? j.get<float>() : fallback;
		}

		void readVec3(const nlohmann::json &j, const char *key, float out[3])
		{
			if (j.contains(key) && j[key].is_array() && j[key].size() == 3)
				for (int k = 0; k < 3; ++k) out[k] = num(j[key][k], out[k]);
		}

		int texture(HSMesh &mesh, int index)
		{
			if (index < 0 || index >= static_cast<int>(mesh.texturePaths.size())) return -1;
			if (mesh.glTextures[index] != -1) return mesh.glTextures[index];
			mesh.glTextures[index] = -2;	// failed unless loaded below
			D3DXIMAGE_INFO info;
			ZeroMemory(&info, sizeof(info));
			int *data = nullptr;
			if (RenderManager.LoadTextureData(wstringtofilename(RubyPaths::toWide(mesh.texturePaths[index])), &info, &data) == ERROR_SUCCESS && data != nullptr)
			{
				BufferedImage *img = new BufferedImage(info.Width, info.Height, BufferedImage::TYPE_INT_ARGB);	// owned by the texture
				memcpy(img->getData(), data, info.Width * info.Height * sizeof(int));
				delete[] data;
				mesh.glTextures[index] = Minecraft::GetInstance()->textures->getTexture(img, C4JRender::TEXTURE_FORMAT_RxGyBzAw, false);
			}
			return mesh.glTextures[index];
		}

		void emitQuad(Tesselator *t, const HSMeshQuad &q)
		{
			t->normal(q.n[0], q.n[1], q.n[2]);
			for (int c = 0; c < 4; ++c) t->vertexUV(q.v[c][0], q.v[c][1], q.v[c][2], q.v[c][3], q.v[c][4]);
		}

		// load time: normals + quads sorted into runs of one texture (one bind and one draw per run)
		void prepare(HSMeshPart &part)
		{
			for (HSMeshQuad &q : part.quads)
			{
				const float ax = q.v[1][0] - q.v[0][0], ay = q.v[1][1] - q.v[0][1], az = q.v[1][2] - q.v[0][2];
				const float bx = q.v[2][0] - q.v[0][0], by = q.v[2][1] - q.v[0][1], bz = q.v[2][2] - q.v[0][2];
				float nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
				const float len = sqrtf(nx * nx + ny * ny + nz * nz);
				if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }
				q.n[0] = nx; q.n[1] = ny; q.n[2] = nz;
			}
			std::stable_sort(part.quads.begin(), part.quads.end(), [](const HSMeshQuad &a, const HSMeshQuad &b) {
				return a.tex != b.tex ? a.tex < b.tex : (a.emissive < b.emissive);
			});
			part.runs.clear();
			for (int i = 0; i < static_cast<int>(part.quads.size()); ++i)
			{
				const HSMeshQuad &q = part.quads[i];
				if (part.runs.empty() || part.runs.back().tex != q.tex || part.runs.back().emissive != q.emissive)
					part.runs.push_back({ q.tex, q.emissive, i, 0 });
				part.runs.back().count++;
			}
		}

		int g_light = -1;	// lightmap value for non-emissive quads (-1: keep the GL state)

		void drawPart(HSMesh &mesh, const HSMeshPart &part)
		{
			if (part.quads.empty()) return;
			Tesselator *t = Tesselator::getInstance();
			Textures *textures = Minecraft::GetInstance()->textures;
			int bound = -3;
			for (const HSMeshRun &run : part.runs)
			{
				const int id = texture(mesh, run.tex);
				if (id < 0) continue;
				if (id != bound) { textures->bind(id); bound = id; }
				t->begin();
				if (run.emissive) t->tex2(0x00F000F0);
				else if (g_light >= 0) t->tex2(g_light);
				for (int i = run.first; i < run.first + run.count; ++i) emitQuad(t, part.quads[i]);
				t->end();
			}
		}

		void drawWithPivot(HSMesh &mesh, const HSMeshPart &part, float spinAngle = 0.0f, int spinAxis = 2)
		{
			if (!part.hasPivot) { drawPart(mesh, part); return; }
			glPushMatrix();
			glTranslatef(part.pivot[0], part.pivot[1], part.pivot[2]);
			if (spinAngle != 0.0f) glRotatef(spinAngle, spinAxis == 0 ? 1.0f : 0.0f, spinAxis == 1 ? 1.0f : 0.0f, spinAxis == 2 ? 1.0f : 0.0f);
			drawPart(mesh, part);
			glPopMatrix();
		}

		void drawAll(HSMesh &mesh)
		{
			for (const HSMeshPart &p : mesh.parts) drawWithPivot(mesh, p);
		}

		// scales and centres the mesh so its largest side is `size` blocks, centred on (0.5, 0.5, 0.5)
		void fitToBlock(const HSMesh &mesh, float size)
		{
			float ext = 0.0f, c[3];
			for (int k = 0; k < 3; ++k)
			{
				ext = (std::max)(ext, mesh.boundsMax[k] - mesh.boundsMin[k]);
				c[k] = (mesh.boundsMax[k] + mesh.boundsMin[k]) * 0.5f;
			}
			if (ext <= 1e-4f) return;
			const float k = size / ext;
			glTranslatef(0.5f, 0.5f, 0.5f);
			glScalef(k, k, k);
			glTranslatef(-c[0], -c[1], -c[2]);
		}

		void applyDisplay(const HSMeshDisplay &d)
		{
			glTranslatef(d.translation[0] / 16.0f, d.translation[1] / 16.0f, d.translation[2] / 16.0f);
			glRotatef(d.rotation[0], 1, 0, 0);
			glRotatef(d.rotation[1], 0, 1, 0);
			glRotatef(d.rotation[2], 0, 0, 1);
			glScalef(d.scale[0], d.scale[1], d.scale[2]);
		}

		void applyInverseDisplay(const HSMeshDisplay &d)
		{
			glScalef(1.0f / d.scale[0], 1.0f / d.scale[1], 1.0f / d.scale[2]);
			glRotatef(-d.rotation[2], 0, 0, 1);
			glRotatef(-d.rotation[1], 0, 1, 0);
			glRotatef(-d.rotation[0], 1, 0, 0);
			glTranslatef(-d.translation[0] / 16.0f, -d.translation[1] / 16.0f, -d.translation[2] / 16.0f);
		}

		// the Java "block" item transforms; the engine already draws blocks like this
		HSMeshDisplay blockDisplay(int context)
		{
			HSMeshDisplay d;
			d.present = true;
			switch (context)
			{
			case Item_Gui: d.rotation[0] = 30; d.rotation[1] = 225; d.scale[0] = d.scale[1] = d.scale[2] = 0.625f; break;
			case Item_FirstPerson: d.rotation[1] = 45; d.scale[0] = d.scale[1] = d.scale[2] = 0.4f; break;
			case Item_ThirdPerson: d.rotation[0] = 75; d.rotation[1] = 45; d.translation[1] = 2.5f; d.scale[0] = d.scale[1] = d.scale[2] = 0.375f; break;
			default: d.translation[1] = 3.0f; d.scale[0] = d.scale[1] = d.scale[2] = 0.25f; break;
			}
			return d;
		}

		// spin angles of machine parts, per block position
		std::unordered_map<long long, float> &angles()
		{
			static std::unordered_map<long long, float> *m = new std::unordered_map<long long, float>();
			return *m;
		}

		long long posKey(int x, int y, int z)
		{
			return (static_cast<long long>(x & 0x3FFFFFF) << 38) | (static_cast<long long>(z & 0x3FFFFFF) << 12) | (y & 0xFFF);
		}

		class MachineMeshRenderer : public TileEntityRenderer
		{
		public:
			virtual void render(shared_ptr<TileEntity> te, double x, double y, double z, float a, bool setColor, float alpha, bool useCompiled)
			{
				if (te == nullptr || te->level == nullptr) return;
				const int tileId = te->level->getTile(te->x, te->y, te->z);
				const HSMeshes::MachineModel *mm = HSMeshes::machineModel(tileId);
				if (mm == nullptr) return;
				HSMesh *mesh = HSMeshes::mesh(mm->mesh);
				if (mesh == nullptr) return;

				const int data = te->level->getData(te->x, te->y, te->z);
				const bool active = (data & 8) != 0;
				float facingAngle = 0.0f;
				switch (data & 7)
				{
				case 3: facingAngle = 180.0f; break;	// south
				case 4: facingAngle = 90.0f; break;		// west
				case 5: facingAngle = -90.0f; break;	// east
				default: break;
				}

				// spinning part: keeps its angle when it stops
				float &angle = angles()[posKey(te->x, te->y, te->z)];
				if (mm->spinSpeed != 0.0f && (active || !mm->spinWhenActiveOnly))
				{
					const float t = static_cast<float>(te->level->getGameTime() % 72000) + a;
					angle = fmodf(t * mm->spinSpeed, 360.0f);
				}

				// light: the machine block is opaque (its own light is 0), use the brightest neighbour
				int light = 0;
				static const int off[6][3] = { {0,1,0}, {1,0,0}, {-1,0,0}, {0,0,1}, {0,0,-1}, {0,-1,0} };
				for (int i = 0; i < 6; ++i)
				{
					const int l = te->level->getLightColor(te->x + off[i][0], te->y + off[i][1], te->z + off[i][2], 0);
					if (((l >> 20) & 0xF) + ((l >> 4) & 0xF) > ((light >> 20) & 0xF) + ((light >> 4) & 0xF)) light = l;
				}
				g_light = light;
				glPushMatrix();
				glTranslatef(static_cast<float>(x) + 0.5f, static_cast<float>(y), static_cast<float>(z) + 0.5f);
				glRotatef(facingAngle, 0, 1, 0);
				glTranslatef(-0.5f, 0.0f, -0.5f);
				glColor4f(1, 1, 1, 1);
				for (const HSMeshPart &p : mesh->parts)
				{
					const bool spins = !mm->spinPart.empty() && p.name == mm->spinPart;
					drawWithPivot(*mesh, p, spins ? angle : 0.0f, mm->spinAxis);
				}
				glPopMatrix();
				g_light = -1;
			}
		};

		// MekaSuit-style armour parts: bone + offset (blocks) from the part name
		struct BoneInfo { int bone; float ox, oy; };
		bool boneFor(const std::string &name, BoneInfo &out)
		{
			if (name.find("head") != std::string::npos) { out = { 0, 0.0f, 0.0f }; return true; }
			if (name.find("left_arm") != std::string::npos) { out = { 3, -0.3125f, -0.125f }; return true; }
			if (name.find("right_arm") != std::string::npos) { out = { 2, 0.3125f, -0.125f }; return true; }
			if (name.find("left_leg") != std::string::npos) { out = { 5, -0.125f, -0.75f }; return true; }
			if (name.find("right_leg") != std::string::npos) { out = { 4, 0.125f, -0.75f }; return true; }
			if (name.find("body") != std::string::npos || name.find("chest") != std::string::npos || name.find("boots") != std::string::npos
				|| name.find("leggings") != std::string::npos)
			{
				out = { 1, 0.0f, 0.0f };
				return name.find("wing") == std::string::npos;
			}
			return false;
		}

		const char *slotWord(int slot)
		{
			switch (slot)
			{
			case HSArmor_Head: return "helmet";
			case HSArmor_Chest: return "chest";
			case HSArmor_Legs: return "leggings";
			default: return "boots";
			}
		}

		void boneTransform(ModelPart *p)
		{
			if (p == nullptr) return;
			glTranslatef(p->x / 16.0f, p->y / 16.0f, p->z / 16.0f);
			if (p->zRot != 0) glRotatef(p->zRot * RAD2DEG, 0, 0, 1);
			if (p->yRot != 0) glRotatef(p->yRot * RAD2DEG, 0, 1, 0);
			if (p->xRot != 0) glRotatef(p->xRot * RAD2DEG, 1, 0, 0);
		}
	}

	// ==========================================================================================
	int load(const std::string &modFolder, const std::string &file)
	{
		const std::string full = RubyPaths::resolveModTexturePath(modFolder, file);
		if (full.empty()) { Loader::_debugPrint("mesh '" + file + "' not found"); return -1; }
		nlohmann::json j;
		try { j = nlohmann::json::parse(readFile(full)); }
		catch (...) { Loader::_debugPrint("mesh '" + file + "' is not valid JSON"); return -1; }

		HSMesh *mesh = new HSMesh();
		if (j.contains("textures") && j["textures"].is_array())
		{
			for (const auto &t : j["textures"])
			{
				const std::string path = t.is_string() ? RubyPaths::resolveModTexturePath(modFolder, t.get<std::string>()) : std::string();
				mesh->texturePaths.push_back(path);
			}
		}
		mesh->glTextures.assign(mesh->texturePaths.size(), -1);

		if (j.contains("parts") && j["parts"].is_object())
		{
			for (auto it = j["parts"].begin(); it != j["parts"].end(); ++it)
			{
				HSMeshPart part;
				part.name = it.key();
				if (!it.value().is_array()) continue;
				for (const auto &jq : it.value())
				{
					if (!jq.is_array() || jq.size() < 22) continue;
					HSMeshQuad q;
					q.tex = jq[0].get<int>();
					q.emissive = jq[1].get<int>() != 0;
					for (int c = 0; c < 4; ++c)
						for (int k = 0; k < 5; ++k) q.v[c][k] = num(jq[2 + c * 5 + k], 0.0f);
					part.quads.push_back(q);
				}
				if (j.contains("pivots") && j["pivots"].contains(part.name))
				{
					const auto &pv = j["pivots"][part.name];
					if (pv.is_array() && pv.size() == 3)
					{
						for (int k = 0; k < 3; ++k) part.pivot[k] = num(pv[k], 0.5f);
						part.hasPivot = true;
					}
				}
				prepare(part);
				mesh->parts.push_back(part);
			}
		}
		{	// bounds (pivoted parts are relative to their pivot)
			bool first = true;
			for (const HSMeshPart &p : mesh->parts)
				for (const HSMeshQuad &q : p.quads)
					for (int c = 0; c < 4; ++c)
						for (int k = 0; k < 3; ++k)
						{
							const float v = q.v[c][k] + (p.hasPivot ? p.pivot[k] : 0.0f);
							if (first) { mesh->boundsMin[0] = mesh->boundsMax[0] = q.v[c][0] + (p.hasPivot ? p.pivot[0] : 0.0f);
								mesh->boundsMin[1] = mesh->boundsMax[1] = q.v[c][1] + (p.hasPivot ? p.pivot[1] : 0.0f);
								mesh->boundsMin[2] = mesh->boundsMax[2] = q.v[c][2] + (p.hasPivot ? p.pivot[2] : 0.0f); first = false; }
							mesh->boundsMin[k] = (std::min)(mesh->boundsMin[k], v);
							mesh->boundsMax[k] = (std::max)(mesh->boundsMax[k], v);
						}
		}
		if (j.contains("display") && j["display"].is_object())
		{
			static const char *keys[HSDisplay_Count] = { "gui", "firstperson", "thirdperson", "ground" };
			for (int k = 0; k < HSDisplay_Count; ++k)
			{
				if (!j["display"].contains(keys[k])) continue;
				const auto &d = j["display"][keys[k]];
				HSMeshDisplay &out = mesh->display[k];
				out.present = true;
				readVec3(d, "rotation", out.rotation);
				readVec3(d, "translation", out.translation);
				readVec3(d, "scale", out.scale);
			}
		}
		HSMeshes::all().push_back(mesh);
		return static_cast<int>(HSMeshes::all().size()) - 1;
	}

	bool registerItemModel(int itemId, int mesh)
	{
		if (itemId <= 0 || itemId >= static_cast<int>(HSMeshes::items().size()) || HSMeshes::mesh(mesh) == nullptr) return false;
		HSMeshes::items()[itemId].mesh = mesh;
		return true;
	}

	bool registerArmorModel(int itemId, int mesh, int slot)
	{
		if (itemId <= 0 || itemId >= static_cast<int>(HSMeshes::armor().size()) || HSMeshes::mesh(mesh) == nullptr) return false;
		HSMeshes::armor()[itemId].mesh = mesh;
		HSMeshes::armor()[itemId].slot = slot;
		return true;
	}

	bool registerMachineModel(int tileId, int mesh, const std::string &spinPart, int spinAxis, float spinSpeed, bool onlyWhenActive)
	{
		if (tileId <= 0 || tileId >= static_cast<int>(HSMeshes::machines().size()) || HSMeshes::mesh(mesh) == nullptr) return false;
		HSMeshes::MachineModel &m = HSMeshes::machines()[tileId];
		m.mesh = mesh;
		m.spinPart = spinPart;
		m.spinAxis = spinAxis;
		m.spinSpeed = spinSpeed;
		m.spinWhenActiveOnly = onlyWhenActive;
		return true;
	}

	bool hasItemModel(int itemId)
	{
		return HSMeshes::itemMesh(itemId) != nullptr;
	}

	bool renderGuiItem(int itemId, float x, float y, float scaleX, float scaleY, float alpha)
	{
		HSMesh *mesh = HSMeshes::itemMesh(itemId);
		if (mesh == nullptr) return false;
		HSMeshDisplay d = mesh->display[HSDisplay_Gui].present ? mesh->display[HSDisplay_Gui] : blockDisplay(Item_Gui);
		glPushMatrix();
		glTranslatef(x + 8.0f * scaleX, y + 8.0f * scaleY, 50.0f);
		glScalef(16.0f * scaleX, -16.0f * scaleY, 16.0f);
		glRotatef(d.rotation[0], 1, 0, 0);
		glRotatef(d.rotation[1], 0, 1, 0);
		glRotatef(d.rotation[2], 0, 0, 1);
		glScalef(0.62f, 0.62f, 0.62f);		// the rotated box must stay inside the slot
		glTranslatef(-0.5f, -0.5f, -0.5f);
		fitToBlock(*mesh, 1.0f);
		glDisable(GL_CULL_FACE);
		glEnable(GL_RESCALE_NORMAL);
		Lighting::turnOnGui();
		glColor4f(1, 1, 1, alpha);
		g_light = 0x00F000F0;
		drawAll(*mesh);
		g_light = -1;
		Lighting::turnOff();
		glDisable(GL_RESCALE_NORMAL);
		glPopMatrix();
		return true;
	}

	bool renderItemAsBlock(int itemId, int context)
	{
		HSMesh *mesh = HSMeshes::itemMesh(itemId);
		if (mesh == nullptr) return false;
		const int kind = context == Item_FirstPerson ? HSDisplay_FirstPerson : (context == Item_ThirdPerson ? HSDisplay_ThirdPerson : HSDisplay_Ground);
		// the engine already placed a held / dropped block here (a unit cube at the origin):
		// draw the mesh at the size of that cube, turned like the Java model asks
		glPushMatrix();
		if (mesh->display[kind].present)
		{
			const HSMeshDisplay &d = mesh->display[kind];
			glRotatef(d.rotation[0], 1, 0, 0);
			glRotatef(d.rotation[1], 0, 1, 0);
			glRotatef(d.rotation[2], 0, 0, 1);
		}
		const float size = (context == Item_Ground) ? 1.0f : 1.25f;
		glTranslatef(-0.5f, -0.5f, -0.5f);
		fitToBlock(*mesh, size);
		drawAll(*mesh);
		glPopMatrix();
		return true;
	}

	bool replacesArmorLayer(int itemId)
	{
		return HSMeshes::armorModel(itemId) != nullptr;
	}

	void renderWornArmor(LivingEntity *mob, HumanoidModel *model, float a)
	{
		if (mob == nullptr || model == nullptr) return;
		bool worn[4] = { false, false, false, false };
		int meshOf[4] = { -1, -1, -1, -1 };
		for (int pos = 0; pos < 4; ++pos)	// pos: 0 boots .. 3 helmet
		{
			shared_ptr<ItemInstance> it = mob->getArmor(pos);
			if (it == nullptr) continue;
			const HSMeshes::ArmorModel *am = HSMeshes::armorModel(it->id);
			if (am == nullptr) continue;
			worn[am->slot] = true;
			meshOf[am->slot] = am->mesh;
		}
		ModelPart *bones[6] = { model->head, model->body, model->arm0, model->arm1, model->leg0, model->leg1 };

		for (int slot = 0; slot < 4; ++slot)
		{
			if (!worn[slot]) continue;
			HSMesh *mesh = HSMeshes::mesh(meshOf[slot]);
			if (mesh == nullptr) continue;
			const char *word = slotWord(slot);
			// shared parts are drawn by one of the two neighbouring pieces, exclusive parts only alone
			const int adjacent = (slot == HSArmor_Head) ? HSArmor_Chest : (slot == HSArmor_Chest ? HSArmor_Head : (slot == HSArmor_Legs ? HSArmor_Feet : HSArmor_Legs));
			const bool adjacentHigher = (slot == HSArmor_Chest || slot == HSArmor_Feet);	// helmet > chest, legs > boots
			for (const HSMeshPart &part : mesh->parts)
			{
				if (part.name.find(word) == std::string::npos) continue;
				if (part.name.compare(0, 5, "excl_") == 0 && worn[adjacent]) continue;
				if (part.name.compare(0, 7, "shared_") == 0 && worn[adjacent] && adjacentHigher) continue;
				BoneInfo b;
				if (!boneFor(part.name, b)) continue;
				glPushMatrix();
				boneTransform(bones[b.bone]);
				glTranslatef(b.ox, b.oy, 0.0f);
				glTranslatef(0.0f, 1.5f, 0.0f);
				glScalef(-1.0f, -1.0f, 1.0f);	// the OBJ is upright (y up, feet at 0), model space is y down
				drawPart(*mesh, part);
				glPopMatrix();
			}
		}
		glColor4f(1, 1, 1, 1);
	}

	TileEntityRenderer *createMachineRenderer()
	{
		return new MachineMeshRenderer();
	}

	bool machineHidesBlock(int tileId)
	{
		return HSMeshes::machineModel(tileId) != nullptr;
	}
}
