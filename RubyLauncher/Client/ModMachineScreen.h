#pragma once
// Hellish Sight - machine kinds and the machine screen data layout. No engine includes so
// the UI can use it.

#include <string>

enum ModMachineKind
{
	MachineKind_Processor = 0, MachineKind_Generator, MachineKind_Storage, MachineKind_Cable,
	MachineKind_Tank, MachineKind_Pipe, MachineKind_Transporter, MachineKind_Pump,
	MachineKind_Miner, MachineKind_Teleporter,
	MachineKind_Heater, MachineKind_Conductor, MachineKind_Boiler, MachineKind_Fission, MachineKind_Fusion,
	MachineKind_Seismic, MachineKind_QioDrive, MachineKind_QioDashboard, MachineKind_QioImporter,
	MachineKind_QioExporter, MachineKind_Entangloporter, MachineKind_ModStation,
};

// Hellish Sight: layout of FurnaceTileEntity::hsData (machine screen). Values are shorts,
// big numbers are split into three 15-bit chunks.
enum ModMachineScreenData
{
	HSD_KIND = 0,			// machine kind + 1 (0 = not a mod machine)
	HSD_ENERGY = 1,			// 3 chunks
	HSD_ENERGY_CAP = 4,		// 3 chunks
	HSD_TANK_CAP = 7,		// 3 chunks
	HSD_TANK0 = 10,			// per tank: chemical index + 1, then 3 chunks of mB (4 tanks)
	HSD_HEAT = 26,			// 3 chunks (temperature in K)
	HSD_HEAT_MAX = 29,		// max temperature / 10
	HSD_UPGRADES = 30,		// speed | energy << 4 | muffling << 8 | filter << 12
	HSD_FORMED = 31,		// 0 no structure, 1 incomplete, 2 formed
	HSD_FREQ = 32,
	HSD_DAMAGE = 33,		// reactor damage %
	HSD_PROGRESS = 34,		// 0..100
	HSD_EJECT = 35,
	HSD_MB_VOLUME = 36,
	HSD_EXTRA = 37,			// miner: % scanned, reactor: burn rate...
	HSD_COUNT = 38,
};

inline void hsPutBig(int *data, int at, long long v)
{
	if (v < 0) v = 0;
	data[at] = static_cast<int>(v & 0x7FFF);
	data[at + 1] = static_cast<int>((v >> 15) & 0x7FFF);
	data[at + 2] = static_cast<int>((v >> 30) & 0x7FFF);
}

inline long long hsGetBig(const int *data, int at)
{
	return (static_cast<long long>(data[at] & 0x7FFF)) | (static_cast<long long>(data[at + 1] & 0x7FFF) << 15) | (static_cast<long long>(data[at + 2] & 0x7FFF) << 30);
}


namespace ModMachines
{
	bool chemicalByIndex(int index, std::wstring &name, int &color);
}
