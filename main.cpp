#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <string>
#include <string_view>

#include "image/image.hpp"
#include "map_file/map_file.hpp"
#include "pdb/pdb.hpp"
#include "utils/console.hpp"
#include "utils/file.hpp"

static const char *version = "1.0.0";

static const char *usage =
	"map-to-pdb %s\n"
	"\n"
	"Builds a PDB from an MSVC linker map file. The PDB carries every map symbol as a\n"
	"public symbol and is stamped with the image's CodeView identity, so debuggers and\n"
	"disassemblers load it automatically next to the image.\n"
	"\n"
	"Usage: map-to-pdb <file.map> <image> [options]\n"
	"\n"
	"Options:\n"
	"  -o, --output <path>  Output file. Default: the PDB name recorded in the image,\n"
	"                       placed next to the image.\n"
	"  -f, --force          Continue when the map does not match the image (timestamp\n"
	"                       or symbol addresses) or the image has no CodeView record.\n"
	"  -y, --overwrite      Replace the output file if it already exists.\n"
	"  -h, --help           Show this help.\n"
	"  -v, --version        Show the version.\n";

static bool option(std::wstring_view arg, std::wstring_view short_name, std::wstring_view long_name)
{
	if (arg == short_name)
		return true;

	return arg == long_name;
}

static std::wstring_view file_name(std::wstring_view path)
{
	size_t slash = path.find_last_of(L"\\/");
	if (slash == std::wstring_view::npos)
		return path;

	return path.substr(slash + 1);
}

static std::string utf8(std::wstring_view w)
{
	int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string s(n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
	return s;
}

int wmain(int argc, wchar_t **argv)
{
	std::wstring map_path;
	std::wstring image_path;
	std::wstring out_path;
	bool force = false;
	bool overwrite = false;

	for (int i = 1; i < argc; ++i)
	{
		std::wstring_view arg = argv[i];

		if (option(arg, L"-h", L"--help"))
		{
			printf(usage, version);
			return 0;
		}

		if (option(arg, L"-v", L"--version"))
		{
			console::get().info("map-to-pdb %s", version);
			return 0;
		}

		if (option(arg, L"-f", L"--force"))
		{
			force = true;
			continue;
		}

		if (option(arg, L"-y", L"--overwrite"))
		{
			overwrite = true;
			continue;
		}

		if (option(arg, L"-o", L"--output"))
		{
			if (i + 1 == argc)
			{
				console::get().error("%ls needs a path", argv[i]);
				return 2;
			}

			out_path = argv[++i];
			continue;
		}

		if (arg.starts_with(L'-'))
		{
			console::get().error("unknown option '%ls', run 'map-to-pdb --help' for usage", argv[i]);
			return 2;
		}

		if (map_path.empty())
		{
			map_path = arg;
			continue;
		}

		if (image_path.empty())
		{
			image_path = arg;
			continue;
		}

		console::get().error("unexpected argument '%ls'", argv[i]);
		return 2;
	}

	if (image_path.empty())
	{
		console::get().error("expected a map file and an image, run 'map-to-pdb --help' for usage");
		return 2;
	}

	image img;
	if (!img.load(image_path))
		return 1;

	map_file map;
	if (!map.load(map_path))
		return 1;

	if (!map.unparsed.empty())
	{
		console::get().warn("skipped %zu symbol lines that could not be parsed, first: '%s'", map.unparsed.size(), map.unparsed[0].c_str());
	}

	if (!map.has_timestamp)
	{
		console::get().warn("the map has no timestamp, so it cannot be confirmed to belong to this image");
	}
	else if (map.timestamp != img.timestamp)
	{
		if (!force)
		{
			console::get().error("map timestamp %08X does not match image timestamp %08X (use --force to continue)", map.timestamp, img.timestamp);
			return 1;
		}

		console::get().warn("map timestamp %08X does not match image timestamp %08X", map.timestamp, img.timestamp);
	}

	uint64_t base = map.has_base ? map.base : img.base;
	auto mismatched = [&](const map_symbol& s)
	{
		if (s.seg > img.sections.size())
			return true;

		return s.va - base != (uint64_t)img.sections[s.seg - 1].VirtualAddress + s.off;
	};

	auto bad = std::find_if(map.symbols.begin(), map.symbols.end(), mismatched);
	if (bad != map.symbols.end())
	{
		size_t count = std::count_if(map.symbols.begin(), map.symbols.end(), mismatched);
		std::string example(bad->name);

		if (!force)
		{
			console::get().error("%zu symbols do not match the image's sections, first: %s at %04X:%08X (use --force to drop them)", count, example.c_str(), bad->seg, bad->off);
			return 1;
		}

		console::get().warn("dropped %zu symbols that do not match the image's sections, first: %s", count, example.c_str());
		std::erase_if(map.symbols, mismatched);
	}

	if (map.symbols.empty())
	{
		console::get().error("no symbols found in '%ls'", map_path.c_str());
		return 1;
	}

	bool flagged = std::any_of(map.symbols.begin(), map.symbols.end(), [](const map_symbol& s)
	{
		return s.func;
	});

	// lld-link maps have no function column, so fall back to the section's execute permission.
	if (!flagged)
	{
		for (map_symbol& s : map.symbols)
		{
			s.func = (img.sections[s.seg - 1].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
		}

		console::get().info("note: the map has no function flags, treating symbols in executable sections as functions");
	}

	if (!img.has_cv)
	{
		if (!force)
		{
			console::get().error("the image has no CodeView debug record, so no PDB can match it (use --force to write one with a random GUID)");
			return 1;
		}

		CoCreateGuid(&img.guid);
		img.age = 1;
		console::get().warn("the image has no CodeView debug record, debuggers will only load this PDB when forced to");
	}

	if (out_path.empty())
	{
		std::wstring_view image_name = file_name(image_path);
		std::wstring name;

		if (!img.pdb_path.empty())
		{
			int n = MultiByteToWideChar(CP_UTF8, 0, img.pdb_path.data(), (int)img.pdb_path.size(), nullptr, 0);
			std::wstring recorded(n, L'\0');
			MultiByteToWideChar(CP_UTF8, 0, img.pdb_path.data(), (int)img.pdb_path.size(), recorded.data(), n);
			name = file_name(recorded);
		}

		if (name.empty())
		{
			name = image_name.substr(0, image_name.find_last_of(L'.'));
			name += L".pdb";
		}

		out_path = image_path.substr(0, image_path.size() - image_name.size()) + name;
	}

	if (!overwrite)
	{
		if (file::get().exists(out_path))
		{
			console::get().error("'%ls' already exists (use --overwrite to replace it)", out_path.c_str());
			return 1;
		}
	}

	pdb out;
	if (!out.write(out_path, img, map, utf8(file_name(map_path)), utf8(file_name(out_path))))
		return 1;

	if (out.truncated)
	{
		console::get().warn("truncated %zu names longer than a PDB record allows", out.truncated);
	}

	size_t functions = std::count_if(map.symbols.begin(), map.symbols.end(), [](const map_symbol& s)
	{
		return s.func;
	});

	const GUID& g = img.guid;
	console::get().info("image   %ls (%s, %zu sections, timestamp %08X)", image_path.c_str(), img.arch, img.sections.size(), img.timestamp);
	console::get().info("map     %zu symbols, %zu functions (skipped %zu absolute and %zu discarded, merged %zu repeated)", map.symbols.size(), functions, map.absolute, map.discarded, map.repeated);
	console::get().info("pdb     %ls", out_path.c_str());
	console::get().info("match   {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X} age %u", g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7], img.age);
	return 0;
}
