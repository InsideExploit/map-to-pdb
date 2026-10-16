#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Multi-Stream File container (the "big MSF" 7.00 layout used by every PDB).
class msf
{
public:
	std::vector<std::vector<uint8_t>> streams;

	bool write(const std::wstring& path);
};
