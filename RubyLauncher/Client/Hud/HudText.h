#pragma once

// UTF-8 <-> UTF-16/32 helpers for the Ruby Launcher HUD API. Lua strings and mod manifests are
// UTF-8; the engine's font works with wide strings. Invalid sequences become
// U+FFFD instead of being dropped, so bad input is visible rather than silent.

#include <string>

namespace RubyHud
{
	inline std::wstring utf8ToWide(const std::string &input)
	{
		std::wstring out;
		out.reserve(input.size());

		size_t i = 0;
		const size_t n = input.size();
		while (i < n)
		{
			const unsigned char c = static_cast<unsigned char>(input[i]);
			unsigned int cp = 0;
			int extra = 0;

			if (c < 0x80)              { cp = c; extra = 0; }
			else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
			else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
			else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
			else { out.push_back(static_cast<wchar_t>(0xFFFD)); ++i; continue; }

			bool valid = true;
			for (int k = 1; k <= extra; ++k)
			{
				if (i + k >= n) { valid = false; break; }
				const unsigned char cc = static_cast<unsigned char>(input[i + k]);
				if ((cc & 0xC0) != 0x80) { valid = false; break; }
				cp = (cp << 6) | (cc & 0x3F);
			}

			if (!valid)
			{
				out.push_back(static_cast<wchar_t>(0xFFFD));
				++i;
				continue;
			}
			i += static_cast<size_t>(extra) + 1;

			if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;

			if (sizeof(wchar_t) == 2 && cp > 0xFFFF)
			{
				cp -= 0x10000;
				out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
				out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
			}
			else
			{
				out.push_back(static_cast<wchar_t>(cp));
			}
		}
		return out;
	}

	inline std::string wideToUtf8(const std::wstring &input)
	{
		std::string out;
		out.reserve(input.size());

		for (size_t i = 0; i < input.size(); ++i)
		{
			unsigned int cp = static_cast<unsigned int>(input[i]);

			if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp <= 0xDBFF && i + 1 < input.size())
			{
				const unsigned int low = static_cast<unsigned int>(input[i + 1]);
				if (low >= 0xDC00 && low <= 0xDFFF)
				{
					cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
					++i;
				}
			}

			if (cp < 0x80)
			{
				out.push_back(static_cast<char>(cp));
			}
			else if (cp < 0x800)
			{
				out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
				out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			}
			else if (cp < 0x10000)
			{
				out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
				out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			}
			else
			{
				out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
				out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			}
		}
		return out;
	}
}
