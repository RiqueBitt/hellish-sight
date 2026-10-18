#pragma once

// Hellish Sight - play on Java Edition servers (1.8 protocol, newer through ViaVersion).
//
// The game ships Reunion (https://github.com/Briiqn/Reunion, AGPL-3.0) with a small
// Java runtime in "javaproxy\". Choosing "Servidores Java" asks for IP:port, writes
// javaproxy\config.yml, starts the proxy hidden, waits for it to listen on
// 127.0.0.1:25599 and joins it like a normal console server. While connected
// through it the client speaks the original console protocol 78 that Reunion
// understands (Packet::s_legacyWire). The proxy is closed with the game.

namespace HSJavaServer
{
	void ShowConnectDialog(int iPad);	// main menu button
	void Tick();						// main loop: finishes a pending connect
	void Shutdown();
}
