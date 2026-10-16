#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

struct gsi_symbol
{
	std::string_view name;
	uint32_t record;
	uint32_t off;
	uint16_t seg;
};

// Global Symbol Index: the name hash tables over the symbol record stream.
class gsi
{
public:
	static gsi& get()
	{
		static gsi g;
		return g;
	}

	std::vector<uint8_t> hash_table(const std::vector<gsi_symbol>& syms);
	std::vector<uint8_t> publics(const std::vector<gsi_symbol>& syms);

private:
	gsi() = default;
	gsi(const gsi&) = delete;
	gsi& operator=(const gsi&) = delete;
};
