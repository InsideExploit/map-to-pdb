#pragma once

#include <string>
#include <string_view>

class image;
class map_file;

class pdb
{
public:
	size_t truncated = 0;

	bool write(const std::wstring& path, const image& img, const map_file& map, std::string_view module, std::string_view pdb_name);
};
