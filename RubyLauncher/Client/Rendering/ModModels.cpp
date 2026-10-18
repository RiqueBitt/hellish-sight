#include "stdafx.h"
#include "ModModels.h"

#include "HSModel.h"
#include "IconRegister.h"
#include "Loader.h"
#include "Host/RubyLauncherHost.h"
#include "Common/ModPaths.h"

#include "json/json.hpp"
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>

namespace ModModels
{
	namespace
	{
		int faceIndex(const std::string &name)
		{
			if (name == "down") return 0;
			if (name == "up") return 1;
			if (name == "north") return 2;
			if (name == "south") return 3;
			if (name == "west") return 4;
			if (name == "east") return 5;
			return -1;
		}

		std::string stemOf(const std::string &path)
		{
			size_t slash = path.find_last_of("/\\");
			std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
			const size_t dot = name.find_last_of('.');
			if (dot != std::string::npos) name = name.substr(0, dot);
			return name;
		}

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
	}

	int load(const std::string &modFolder, const std::string &modId, const std::string &file)
	{
		const std::string full = RubyPaths::resolveModTexturePath(modFolder, file);
		if (full.empty())
		{
			Loader::_debugPrint("model '" + file + "' not found");
			return -1;
		}

		nlohmann::json j;
		try
		{
			j = nlohmann::json::parse(readFile(full));
		}
		catch (...)
		{
			Loader::_debugPrint("model '" + file + "' is not valid JSON");
			return -1;
		}

		HSModel *model = new HSModel();
		model->rotateWithFacing = j.value("rotate", true);

		if (j.contains("textures") && j["textures"].is_array())
		{
			for (const auto &t : j["textures"])
			{
				const std::string rel = t.is_string() ? t.get<std::string>() : std::string();
				const std::string iconName = rubyModTextureIconName(modId, "mdl_" + stemOf(rel));
				RubyModTexture request;
				request.iconName = iconName;
				request.filePath = RubyPaths::resolveModTexturePath(modFolder, rel);
				request.fallback = "iron_block";
				request.block = true;
				RubyLoader::addModTexture(request);
				model->iconNames.push_back(RubyPaths::toWide(iconName));
			}
		}
		model->icons.assign(model->iconNames.size(), nullptr);

		if (j.contains("elements") && j["elements"].is_array())
		{
			for (const auto &je : j["elements"])
			{
				HSModelElement e;
				if (je.contains("from") && je["from"].is_array() && je["from"].size() == 3)
					for (int k = 0; k < 3; ++k) e.from[k] = num(je["from"][k], 0.0f);
				if (je.contains("to") && je["to"].is_array() && je["to"].size() == 3)
					for (int k = 0; k < 3; ++k) e.to[k] = num(je["to"][k], 16.0f);
				if (je.contains("rotation") && je["rotation"].is_object())
				{
					const auto &r = je["rotation"];
					const std::string axis = r.value("axis", std::string("y"));
					e.rotAxis = axis == "x" ? 0 : (axis == "z" ? 2 : 1);
					e.rotAngle = r.value("angle", 0.0f);
					e.rotCos = cosf(e.rotAngle * 3.14159265f / 180.0f);
					e.rotSin = sinf(e.rotAngle * 3.14159265f / 180.0f);
					e.rescale = r.value("rescale", false);
					if (r.contains("origin") && r["origin"].is_array() && r["origin"].size() == 3)
						for (int k = 0; k < 3; ++k) e.origin[k] = num(r["origin"][k], 8.0f);
				}
				e.shade = je.value("shade", true);
				e.emission = je.value("emission", 0);
				if (je.contains("faces") && je["faces"].is_object())
				{
					for (auto it = je["faces"].begin(); it != je["faces"].end(); ++it)
					{
						const int f = faceIndex(it.key());
						if (f < 0 || !it.value().is_object()) continue;
						const auto &jf = it.value();
						HSModelFace &face = e.faces[f];
						face.present = true;
						face.tex = jf.value("t", 0);
						if (jf.contains("uv") && jf["uv"].is_array() && jf["uv"].size() == 4)
							for (int k = 0; k < 4; ++k) face.uv[k] = num(jf["uv"][k], k < 2 ? 0.0f : 16.0f);
						face.rotation = ((jf.value("rotation", 0) % 360) + 360) % 360;
						face.cull = jf.contains("cull") && jf["cull"].is_string() ? faceIndex(jf["cull"].get<std::string>()) : -1;
						face.tint = jf.value("tint", -1) >= 0;
					}
				}
				model->elements.push_back(e);
			}
		}
		return HSModels::add(model);
	}

	int makeConnector(const std::wstring &coreIconName, const std::wstring &armIconName, float thickness,
		const float coreUV[4], const float armU[2], bool (*connects)(LevelSource *, int, int, int, int))
	{
		HSModel *model = new HSModel();
		model->connector = true;
		model->rotateWithFacing = false;
		model->thickness = thickness;
		model->connects = connects;
		for (int k = 0; k < 4; ++k) model->coreUV[k] = coreUV[k];
		model->armU[0] = armU[0];
		model->armU[1] = armU[1];
		model->iconNames.push_back(coreIconName);
		if (!armIconName.empty()) model->iconNames.push_back(armIconName);
		model->icons.assign(model->iconNames.size(), nullptr);
		return HSModels::add(model);
	}

	void registerIcons(int modelIndex, IconRegister *iconRegister)
	{
		if (modelIndex < 0 || modelIndex >= static_cast<int>(HSModels::all().size()) || iconRegister == nullptr) return;
		HSModel *model = HSModels::all()[modelIndex];
		for (size_t i = 0; i < model->iconNames.size(); ++i) model->icons[i] = iconRegister->registerIcon(model->iconNames[i]);
		if (model->activeModel >= 0 && model->activeModel != modelIndex) registerIcons(model->activeModel, iconRegister);
	}
}
