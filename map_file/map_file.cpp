#include "map_file.hpp"

#include <algorithm>
#include <charconv>

#include "utils/console.hpp"
#include "utils/file.hpp"

static bool parse_hex(std::string_view s, uint64_t& v)
{
	if (s.empty())
		return false;

	auto r = std::from_chars(s.data(), s.data() + s.size(), v, 16);
	if (r.ec != std::errc())
		return false;

	return r.ptr == s.data() + s.size();
}

static bool header_value(std::string_view line, std::string_view key, uint64_t& v)
{
	size_t at = line.find(key);
	if (at == std::string_view::npos)
		return false;

	std::string_view rest = line.substr(at + key.size());
	return parse_hex(rest.substr(0, rest.find(' ')), v);
}

// " 0001:00000030       ?OnTick@CActivateState@@UEAA_NAEAVIGameApplication@@@Z 0000000140001030 f   activateState.obj"
static bool parse_symbol(std::string_view l, map_symbol& s)
{
	uint64_t seg = 0;
	uint64_t off = 0;
	if (!parse_hex(l.substr(1, 4), seg))
		return false;

	if (!parse_hex(l.substr(6, 8), off))
		return false;

	if (l[14] != ' ')
		return false;

	size_t name = l.find_first_not_of(' ', 14);
	if (name == std::string_view::npos)
		return false;

	size_t name_end = l.find(' ', name);
	if (name_end == std::string_view::npos)
		return false;

	size_t va = l.find_first_not_of(' ', name_end);
	if (va == std::string_view::npos)
		return false;

	size_t va_end = std::min(l.find(' ', va), l.size());
	std::string_view va_text = l.substr(va, va_end - va);
	if (va_text.size() < 8)
		return false;

	if (!parse_hex(va_text, s.va))
		return false;

	std::string_view flags = l.substr(va_end);
	s.name = l.substr(name, name_end - name);
	s.seg = (uint16_t)seg;
	s.off = (uint32_t)off;
	s.func = flags.size() > 1 && flags[1] == 'f';
	return true;
}

bool map_file::load(const std::wstring& path)
{
	if (!file::get().read(path, text))
	{
		console::get().error("cannot read map file '%ls'", path.c_str());
		return false;
	}

	std::string_view all((const char *)text.data(), text.size());
	bool in_symbols = false;

	size_t pos = 0;
	while (pos < all.size())
	{
		size_t end = all.find('\n', pos);
		if (end == std::string_view::npos)
		{
			end = all.size();
		}

		std::string_view l = all.substr(pos, end - pos);
		pos = end + 1;

		if (l.ends_with('\r'))
		{
			l.remove_suffix(1);
		}

		if (!in_symbols)
		{
			uint64_t v = 0;
			if (header_value(l, "Timestamp is ", v))
			{
				timestamp = (uint32_t)v;
				has_timestamp = true;
			}

			if (header_value(l, "Preferred load address is ", v))
			{
				base = v;
				has_base = true;
			}

			in_symbols = l.find("Publics by Value") != std::string_view::npos;
			continue;
		}

		if (l.size() < 15)
		{
			continue;
		}

		if (l[0] != ' ')
		{
			continue;
		}

		if (l[5] != ':')
		{
			continue;
		}

		map_symbol s = {};
		if (!parse_symbol(l, s))
		{
			unparsed.emplace_back(l);
			continue;
		}

		if (!s.seg)
		{
			absolute++;
			continue;
		}

		// Newer linkers list symbols of discarded sections at the image base (RVA 0).
		if (s.va == base)
		{
			discarded++;
			continue;
		}

		symbols.push_back(s);
	}

	std::sort(symbols.begin(), symbols.end(), [](const map_symbol& a, const map_symbol& b)
	{
		if (a.name != b.name)
			return a.name < b.name;

		if (a.seg != b.seg)
			return a.seg < b.seg;

		return a.off < b.off;
	});

	auto last = std::unique(symbols.begin(), symbols.end(), [](const map_symbol& a, const map_symbol& b)
	{
		if (a.name != b.name)
			return false;

		if (a.seg != b.seg)
			return false;

		return a.off == b.off;
	});

	repeated = symbols.end() - last;
	symbols.erase(last, symbols.end());
	return true;
}
