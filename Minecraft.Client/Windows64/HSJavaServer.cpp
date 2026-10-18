// Hellish Sight - Java Edition servers through the embedded Reunion proxy. See HSJavaServer.h.

#include "stdafx.h"
#include "HSJavaServer.h"
#include "..\Common\UI\UI.h"
#include "..\Common\Network\SessionInfo.h"
#include "Network\WinsockNetLayer.h"
#include "KeyboardMouseInput.h"
#include "..\Minecraft.h"
#include "..\User.h"
#include "..\StringTable.h"
#include "..\..\Minecraft.World\Packet.h"
#include "Registry/Item/ItemRegistry.h"

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>

namespace HSJavaServer
{
namespace
{
	const int kListenPort = 25599;

	HANDLE g_job = nullptr;
	HANDLE g_process = nullptr;
	std::thread g_waiter;
	std::atomic<int> g_state(0);	// 0 idle, 1 starting, 2 ready, 3 failed
	int g_pad = 0;
	std::wstring g_target;
	std::wstring g_error;
	bool g_wasInGame = false;
	DWORD g_joinTick = 0;

	std::wstring exeDir()
	{
		wchar_t path[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, path, MAX_PATH);
		std::wstring p = path;
		const size_t slash = p.find_last_of(L"\\/");
		return slash == std::wstring::npos ? L"." : p.substr(0, slash);
	}

	bool fileExists(const std::wstring &p)
	{
		const DWORD a = GetFileAttributesW(p.c_str());
		return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
	}

	std::string narrow(const std::wstring &w)
	{
		std::string s;
		for (wchar_t c : w) s.push_back(c < 128 ? static_cast<char>(c) : '?');
		return s;
	}

	unsigned int dynString(int slot, const std::wstring &text)
	{
		static int ids[6] = { -1, -1, -1, -1, -1, -1 };
		if (ids[slot] < 0) ids[slot] = ItemRegistry::nextItemNameId();
		ItemRegistry::langList[ids[slot]] = text;
		if (app.getStringTable() != nullptr) app.getStringTable()->registerString(ids[slot], text);
		return static_cast<unsigned int>(ids[slot]);
	}

	int okReturned(void *, int, const C4JStorage::EMessageResult) { return 0; }

	void showMessage(const std::wstring &title, const std::wstring &text)
	{
		UINT ok[1];
		ok[0] = dynString(2, L"OK");
		ui.RequestAlertMessage(dynString(0, title), dynString(1, text), ok, 1, g_pad, &okReturned, nullptr, nullptr);
	}

	std::wstring loadLastAddress()
	{
		FILE *f = nullptr;
		std::wstring out;
		if (_wfopen_s(&f, (exeDir() + L"\\hellish-javaserver.cfg").c_str(), L"r, ccs=UTF-8") == 0 && f)
		{
			wchar_t buf[300] = {};
			if (fgetws(buf, 299, f)) out = buf;
			fclose(f);
		}
		while (!out.empty() && (out.back() == L'\n' || out.back() == L'\r' || out.back() == L' ')) out.pop_back();
		return out;
	}

	void saveLastAddress(const std::wstring &a)
	{
		FILE *f = nullptr;
		if (_wfopen_s(&f, (exeDir() + L"\\hellish-javaserver.cfg").c_str(), L"w, ccs=UTF-8") == 0 && f)
		{
			fputws(a.c_str(), f);
			fclose(f);
		}
	}

	bool parseAddress(std::wstring in, std::string &host, int &port)
	{
		while (!in.empty() && in.front() == L' ') in.erase(in.begin());
		while (!in.empty() && in.back() == L' ') in.pop_back();
		if (in.empty()) return false;
		port = 25565;
		size_t colon = in.find_last_of(L':');
		std::wstring h = in;
		if (colon != std::wstring::npos && in.find(L':') == colon)	// not an IPv6 literal
		{
			h = in.substr(0, colon);
			port = _wtoi(in.substr(colon + 1).c_str());
		}
		if (h.empty() || port <= 0 || port > 65535) return false;
		for (wchar_t c : h)
			if (!(iswalnum(c) || c == L'.' || c == L'-' || c == L'_')) return false;
		host = narrow(h);
		return true;
	}

	bool writeConfig(const std::wstring &dir, const std::string &host, int port)
	{
		FILE *f = nullptr;
		if (_wfopen_s(&f, (dir + L"\\config.yml").c_str(), L"w") != 0 || !f) return false;
		fprintf(f,
			"# Written by Hellish Sight every time you connect - edit in game instead.\n"
			"connection:\n"
			"  java-host: \"%s\"\n"
			"  java-port: %d\n"
			"  listen-port: %d\n"
			"  player-prefix: \"\"\n"
			"auth:\n"
			"  online-mode: false\n"
			"forwarding:\n"
			"  mode: NONE\n"
			"  velocity-secret: \"\"\n"
			"  bungee-guard-token: \"\"\n"
			"gameplay:\n"
			"  auto-accept-resource-packs: true\n"
			"  infinite-world-hack: false\n"
			"  offload-chunks-to-disk: false\n"
			"  release-item-on-interact: true\n"
			"  max-players: 4\n"
			"  min-model-size-blocks: 0.5\n"
			"  max-model-size-blocks: 2.0\n"
			"  max-model-byte-size: 131072\n"
			"  enchantment-table-hack: true\n"
			"network:\n"
			"  network-mode: false\n"
			"  default-server: lobby\n"
			"  netty-threads: 2\n"
			"  servers:\n"
			"    lobby:\n"
			"      host: \"%s\"\n"
			"      port: %d\n"
			"via:\n"
			"  enabled: true\n"
			"  target-version: \"auto\"\n",
			host.c_str(), port, kListenPort, host.c_str(), port);
		fclose(f);
		return true;
	}

	void stopProxy()
	{
		if (g_waiter.joinable()) g_waiter.join();
		if (g_process)
		{
			TerminateProcess(g_process, 0);
			WaitForSingleObject(g_process, 2000);
			CloseHandle(g_process);
			g_process = nullptr;
		}
	}

	bool portOpen()
	{
		SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (s == INVALID_SOCKET) return false;
		sockaddr_in a = {};
		a.sin_family = AF_INET;
		a.sin_port = htons(kListenPort);
		a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		const bool ok = connect(s, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0;
		closesocket(s);
		return ok;
	}

	bool startProxy(const std::string &host, int port)
	{
		const std::wstring dir = exeDir() + L"\\javaproxy";
		const std::wstring java = dir + L"\\jre\\bin\\java.exe";
		const std::wstring jar = dir + L"\\reunion.jar";
		if (!fileExists(java) || !fileExists(jar))
		{
			g_error = L"A pasta javaproxy (proxy Reunion + Java) n\u00E3o foi encontrada ao lado do jogo. Baixe a vers\u00E3o completa do Hellish Sight.";
			return false;
		}
		if (!writeConfig(dir, host, port))
		{
			g_error = L"N\u00E3o foi poss\u00EDvel escrever javaproxy\\config.yml.";
			return false;
		}

		stopProxy();
		if (!g_job)
		{
			// everything in this job dies with the game, even after a crash
			g_job = CreateJobObjectW(nullptr, nullptr);
			if (g_job)
			{
				JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
				info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
				SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &info, sizeof(info));
			}
		}

		SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
		HANDLE log = CreateFileW((dir + L"\\proxy.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		STARTUPINFOW si = { sizeof(si) };
		si.dwFlags = STARTF_USESHOWWINDOW | (log != INVALID_HANDLE_VALUE ? STARTF_USESTDHANDLES : 0);
		si.wShowWindow = SW_HIDE;
		si.hStdOutput = log;
		si.hStdError = log;
		si.hStdInput = nullptr;
		PROCESS_INFORMATION pi = {};
		std::wstring cmd = L"\"" + java + L"\" -Xms64m -Xmx512m -XX:+UseSerialGC -jar \"" + jar + L"\"";
		const BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, dir.c_str(), &si, &pi);
		if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
		if (!ok)
		{
			g_error = L"N\u00E3o foi poss\u00EDvel iniciar o Java do proxy.";
			return false;
		}
		if (g_job) AssignProcessToJobObject(g_job, pi.hProcess);
		ResumeThread(pi.hThread);
		CloseHandle(pi.hThread);
		g_process = pi.hProcess;

		g_state = 1;
		g_waiter = std::thread([]() {
			const DWORD start = GetTickCount();
			while (GetTickCount() - start < 30000)
			{
				if (portOpen()) { g_state = 2; return; }
				if (g_process && WaitForSingleObject(g_process, 0) == WAIT_OBJECT_0) break;	// java exited
				Sleep(250);
			}
			g_state = 3;
		});
		return true;
	}

	void joinProxy()
	{
		Packet::s_legacyWire = true;
		g_joinTick = GetTickCount();

		auto sessionInfo = std::make_unique<FriendSessionInfo>();
		static const wchar_t *kLabel = L"Servidor Java";
		strncpy_s(sessionInfo->data.hostIP, "127.0.0.1", sizeof(sessionInfo->data.hostIP) - 1);
		sessionInfo->data.hostPort = kListenPort;
		const size_t len = wcslen(kLabel);
		sessionInfo->displayLabel = new wchar_t[len + 1];
		wcscpy_s(sessionInfo->displayLabel, len + 1, kLabel);
		sessionInfo->displayLabelLength = static_cast<unsigned char>(len);
		sessionInfo->displayLabelViewableStartIndex = 0;
		wcsncpy_s(sessionInfo->data.hostName, XUSER_NAME_SIZE, kLabel, _TRUNCATE);
		sessionInfo->sessionId = static_cast<uint64_t>(inet_addr("127.0.0.1")) | static_cast<uint64_t>(kListenPort) << 32;
		sessionInfo->data.isReadyToJoin = true;
		sessionInfo->data.isJoinable = true;

		DWORD mask = CGameNetworkManager::GetLocalPlayerMask(ProfileManager.GetPrimaryPad());
		CGameNetworkManager::eJoinGameResult result = g_NetworkManager.JoinGame(sessionInfo.get(), mask);
		if (result == CGameNetworkManager::JOINGAME_PENDING)
		{
			ConnectionProgressParams *param = new ConnectionProgressParams();
			param->iPad = ProfileManager.GetPrimaryPad();
			param->stringId = IDS_PROGRESS_CONNECTING;
			param->showTooltips = true;
			param->setFailTimer = false;
			param->timerTime = 0;
			param->cancelFunc = nullptr;
			param->cancelFuncParam = nullptr;
			ui.NavigateToScene(ProfileManager.GetPrimaryPad(), eUIScene_ConnectingProgress, param);
		}
		else
		{
			Packet::s_legacyWire = false;
			showMessage(L"Servidores Java", L"N\u00E3o foi poss\u00EDvel conectar ao proxy local.");
		}
	}

	int keyboardReturned(LPVOID, const bool accepted)
	{
		if (!accepted) return 0;
		uint16_t text[256];
		ZeroMemory(text, sizeof(text));
		Win64_GetKeyboardText(text, 256);
		const std::wstring address = reinterpret_cast<wchar_t *>(text);
		std::string host;
		int port = 0;
		if (!parseAddress(address, host, port))
		{
			showMessage(L"Servidores Java", L"Endere\u00E7o inv\u00E1lido. Use IP:porta, por exemplo mc.exemplo.com:25565.");
			return 0;
		}
		saveLastAddress(address);
		g_target = address;
		g_error.clear();
		if (!startProxy(host, port))
		{
			showMessage(L"Servidores Java", g_error);
		}
		return 0;
	}
}

void ShowConnectDialog(int iPad)
{
	g_pad = iPad;
	if (g_state.load() == 1) return;	// already starting
	static std::wstring s_default;
	s_default = loadLastAddress();
	UIKeyboardInitData kbData;
	kbData.title = L"Servidor Java (IP:porta) - vers\u00E3o 1.8 ou mais nova";
	kbData.defaultText = s_default.c_str();
	kbData.maxChars = 255;
	kbData.callback = &keyboardReturned;
	kbData.lpParam = nullptr;
	kbData.pcMode = g_KBMInput.IsKBMActive();
	ui.NavigateToScene(iPad, eUIScene_Keyboard, &kbData, eUILayer_Fullscreen, eUIGroup_Fullscreen);
}

void Tick()
{
	const int st = g_state.load();
	if (st == 2 || st == 3)
	{
		if (g_waiter.joinable()) g_waiter.join();
		g_state = 0;
		if (st == 2) joinProxy();
		else showMessage(L"Servidores Java", L"O proxy n\u00E3o respondeu. Veja javaproxy\\proxy.log (o servidor precisa aceitar contas sem login da Microsoft, online-mode=false).");
	}

	// leave the console-protocol mode once the proxy session is over
	if (Packet::s_legacyWire)
	{
		const bool inGame = app.GetGameStarted();
		if (inGame) g_wasInGame = true;
		else if (g_wasInGame || GetTickCount() - g_joinTick > 60000)
		{
			Packet::s_legacyWire = false;
			g_wasInGame = false;
		}
	}
}

void Shutdown()
{
	stopProxy();
	if (g_job) { CloseHandle(g_job); g_job = nullptr; }
}
}
