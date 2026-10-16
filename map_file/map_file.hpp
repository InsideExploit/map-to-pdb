#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct map_symbol
{
	std::string_view name;
	uint64_t va;
	uint32_t off;
	uint16_t seg;
	bool func;
};

class map_file
{
public:
	uint32_t timestamp = 0;
	bool has_timestamp = false;
	uint64_t base = 0;
	bool has_base = false;

	// Sorted by name, then address. Names point into the loaded text.
	std::vector<map_symbol> symbols;

	size_t absolute = 0;
	size_t discarded = 0;
	size_t repeated = 0;
	std::vector<std::string> unparsed;

	map_file() = default;
	map_file(const map_file&) = delete;
	map_file& operator=(const map_file&) = delete;

	bool load(const std::wstring& path);

private:
	std::vector<uint8_t> text;
};
