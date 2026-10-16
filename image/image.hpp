#pragma once

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

class image
{
public:
	uint16_t machine = 0;
	uint16_t cv_machine = 0;
	const char *arch = "";
	uint32_t timestamp = 0;
	uint64_t base = 0;
	uint32_t section_align = 0;
	std::vector<IMAGE_SECTION_HEADER> sections;

	bool has_cv = false;
	GUID guid = {};
	uint32_t age = 0;
	std::string pdb_path;

	bool load(const std::wstring& path);
};
