#include "image.hpp"

#include "utils/console.hpp"
#include "utils/file.hpp"

struct rsds_header
{
	uint32_t signature;
	GUID guid;
	uint32_t age;
};

static const uint32_t rsds_signature = 0x53445352;

template <typename T>
static bool read_at(const std::vector<uint8_t>& d, uint64_t off, T& out)
{
	if (off > d.size())
		return false;

	if (d.size() - off < sizeof(T))
		return false;

	memcpy(&out, d.data() + off, sizeof(T));
	return true;
}

static bool rva_to_offset(const std::vector<IMAGE_SECTION_HEADER>& sections, uint32_t rva, uint64_t& off)
{
	for (const IMAGE_SECTION_HEADER& s : sections)
	{
		if (rva < s.VirtualAddress)
		{
			continue;
		}

		uint32_t delta = rva - s.VirtualAddress;
		if (delta >= s.SizeOfRawData)
		{
			continue;
		}

		off = (uint64_t)s.PointerToRawData + delta;
		return true;
	}

	return false;
}

bool image::load(const std::wstring& path)
{
	std::vector<uint8_t> d;
	if (!file::get().read(path, d))
	{
		console::get().error("cannot read image '%ls'", path.c_str());
		return false;
	}

	IMAGE_DOS_HEADER dos = {};
	read_at(d, 0, dos);
	if (dos.e_magic != IMAGE_DOS_SIGNATURE)
	{
		console::get().error("'%ls' is not a PE image (no MZ header)", path.c_str());
		return false;
	}

	uint64_t nt = (uint32_t)dos.e_lfanew;
	uint32_t sig = 0;
	read_at(d, nt, sig);
	if (sig != IMAGE_NT_SIGNATURE)
	{
		console::get().error("'%ls' is not a PE image (no PE header)", path.c_str());
		return false;
	}

	IMAGE_FILE_HEADER fh = {};
	read_at(d, nt + 4, fh);
	machine = fh.Machine;
	timestamp = fh.TimeDateStamp;

	switch (machine)
	{
	case IMAGE_FILE_MACHINE_I386:
		arch = "x86";
		cv_machine = 0x07;
		break;
	case IMAGE_FILE_MACHINE_AMD64:
		arch = "x64";
		cv_machine = 0xD0;
		break;
	case IMAGE_FILE_MACHINE_ARMNT:
		arch = "arm";
		cv_machine = 0xF4;
		break;
	case IMAGE_FILE_MACHINE_ARM64:
		arch = "arm64";
		cv_machine = 0xF6;
		break;
	default:
		console::get().error("unsupported machine type 0x%04X in '%ls'", machine, path.c_str());
		return false;
	}

	uint64_t opt = nt + 4 + sizeof(IMAGE_FILE_HEADER);
	uint16_t magic = 0;
	read_at(d, opt, magic);

	IMAGE_DATA_DIRECTORY debug = {};
	if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
	{
		IMAGE_OPTIONAL_HEADER64 oh;
		if (!read_at(d, opt, oh))
		{
			console::get().error("'%ls' has a truncated optional header", path.c_str());
			return false;
		}

		base = oh.ImageBase;
		section_align = oh.SectionAlignment;
		if (oh.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_DEBUG)
		{
			debug = oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
		}
	}
	else if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
	{
		IMAGE_OPTIONAL_HEADER32 oh;
		if (!read_at(d, opt, oh))
		{
			console::get().error("'%ls' has a truncated optional header", path.c_str());
			return false;
		}

		base = oh.ImageBase;
		section_align = oh.SectionAlignment;
		if (oh.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_DEBUG)
		{
			debug = oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
		}
	}
	else
	{
		console::get().error("'%ls' has an unknown optional header magic 0x%04X", path.c_str(), magic);
		return false;
	}

	uint64_t sec = opt + fh.SizeOfOptionalHeader;
	for (uint16_t i = 0; i < fh.NumberOfSections; ++i)
	{
		IMAGE_SECTION_HEADER s;
		if (!read_at(d, sec + i * sizeof(s), s))
		{
			console::get().error("'%ls' has a truncated section table", path.c_str());
			return false;
		}

		sections.push_back(s);
	}

	if (!debug.VirtualAddress)
		return true;

	uint64_t dir = 0;
	if (!rva_to_offset(sections, debug.VirtualAddress, dir))
		return true;

	for (uint32_t i = 0; i < debug.Size / sizeof(IMAGE_DEBUG_DIRECTORY); ++i)
	{
		IMAGE_DEBUG_DIRECTORY e;
		if (!read_at(d, dir + i * sizeof(e), e))
		{
			break;
		}

		if (e.Type != IMAGE_DEBUG_TYPE_CODEVIEW)
		{
			continue;
		}

		uint64_t at = e.PointerToRawData;
		if (!at)
		{
			rva_to_offset(sections, e.AddressOfRawData, at);
		}

		rsds_header cv = {};
		read_at(d, at, cv);
		if (cv.signature != rsds_signature)
		{
			continue;
		}

		has_cv = true;
		guid = cv.guid;
		age = cv.age;

		uint64_t end = at + e.SizeOfData;
		if (end > d.size())
		{
			end = d.size();
		}

		for (uint64_t p = at + sizeof(cv); p < end; ++p)
		{
			if (!d[p])
			{
				break;
			}

			pdb_path.push_back((char)d[p]);
		}

		break;
	}

	return true;
}
