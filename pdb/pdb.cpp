#include "pdb.hpp"

#include "format.hpp"
#include "gsi/gsi.hpp"
#include "image/image.hpp"
#include "map_file/map_file.hpp"
#include "msf/msf.hpp"
#include "utils/buffer.hpp"
#include "utils/hash.hpp"

enum stream_index : uint16_t
{
	stream_old_directory,
	stream_info,
	stream_tpi,
	stream_dbi,
	stream_ipi,
	stream_link_info,
	stream_globals,
	stream_publics,
	stream_records,
	stream_section_headers,
	stream_module,
	stream_linker_module,
	stream_names,
	stream_count,
};

static const size_t max_record = 0xFF00;

// String table holding "" at offset 0 and s at offset 1, hashed into two buckets.
static std::vector<uint8_t> string_table(std::string_view s)
{
	uint32_t slot = hash::get().v1(s) % 2;

	buffer b;
	b.put((uint32_t)string_table_signature);
	b.put((uint32_t)1);
	b.put((uint32_t)(s.size() + 2));
	b.data.push_back(0);
	b.put_str(s);
	b.put((uint32_t)2);
	b.put((uint32_t)(slot == 0 ? 1 : 0));
	b.put((uint32_t)(slot == 1 ? 1 : 0));
	b.put((uint32_t)1);
	return b.data;
}

static std::vector<uint8_t> type_stream()
{
	tpi_header h = {};
	h.version = tpi_v80;
	h.header_size = sizeof(h);
	h.type_begin = 0x1000;
	h.type_end = 0x1000;
	h.hash_stream = 0xFFFF;
	h.hash_aux_stream = 0xFFFF;
	h.hash_key_size = 4;
	h.hash_buckets = 0x3FFFF;

	buffer b;
	b.put(h);
	return b.data;
}

static void put_record(buffer& out, uint16_t kind, const buffer& body)
{
	size_t size = (4 + body.data.size() + 3) & ~(size_t)3;
	out.put((uint16_t)(size - 2));
	out.put(kind);
	out.put_bytes(body.data.data(), body.data.size());
	out.data.resize(out.data.size() + size - 4 - body.data.size());
}

bool pdb::write(const std::wstring& path, const image& img, const map_file& map, std::string_view module, std::string_view pdb_name)
{
	msf f;
	f.streams.resize(stream_count);

	// Symbol records: one S_PUB32 per map symbol, in name order like link.exe and lld-link.
	buffer records;
	std::vector<gsi_symbol> pubs;
	pubs.reserve(map.symbols.size());
	for (const map_symbol& s : map.symbols)
	{
		std::string_view name = s.name.substr(0, max_record - sizeof(pub32_header) - 1);
		if (name.size() < s.name.size())
		{
			truncated++;
		}

		size_t size = (sizeof(pub32_header) + name.size() + 1 + 3) & ~(size_t)3;

		pub32_header h = {};
		h.length = (uint16_t)(size - 2);
		h.kind = S_PUB32;
		h.flags = s.func ? pub_function : 0;
		h.offset = s.off;
		h.segment = s.seg;

		pubs.push_back({ name, (uint32_t)records.data.size(), s.off, s.seg });
		records.put(h);
		records.put_bytes(name.data(), name.size());
		records.data.resize(records.data.size() + size - sizeof(h) - name.size());
	}

	f.streams[stream_publics] = gsi::get().publics(pubs);
	f.streams[stream_globals] = gsi::get().hash_table({});
	f.streams[stream_records] = std::move(records.data);

	// PDB info stream with the identity debuggers match against the image.
	buffer info;
	info.put((uint32_t)pdb_info_vc70);
	info.put(img.timestamp);
	info.put(img.age);
	info.put(img.guid);

	struct named_stream
	{
		std::string_view name;
		uint32_t offset;
		uint32_t stream;
	};

	static const char names[] = "/LinkInfo\0/names";
	named_stream named[2] = { { "/names", 10, stream_names }, { "/LinkInfo", 0, stream_link_info } };
	int slots[4] = { -1, -1, -1, -1 };
	for (int i = 0; i < 2; ++i)
	{
		uint32_t s = (hash::get().v1(named[i].name) & 0xFFFF) % 4;
		while (slots[s] >= 0)
		{
			s = (s + 1) % 4;
		}

		slots[s] = i;
	}

	uint32_t present = 0;
	for (int s = 0; s < 4; ++s)
	{
		if (slots[s] >= 0)
		{
			present |= 1u << s;
		}
	}

	info.put((uint32_t)sizeof(names));
	info.put_bytes(names, sizeof(names));
	info.put((uint32_t)2);
	info.put((uint32_t)4);
	info.put((uint32_t)1);
	info.put(present);
	info.put((uint32_t)0);
	for (int s = 0; s < 4; ++s)
	{
		if (slots[s] < 0)
		{
			continue;
		}

		info.put(named[slots[s]].offset);
		info.put(named[slots[s]].stream);
	}

	info.put((uint32_t)0);
	f.streams[stream_info] = std::move(info.data);

	f.streams[stream_tpi] = type_stream();
	f.streams[stream_ipi] = type_stream();
	f.streams[stream_names] = string_table("");

	const IMAGE_SECTION_HEADER *headers = img.sections.data();
	f.streams[stream_section_headers].assign((const uint8_t *)headers, (const uint8_t *)(headers + img.sections.size()));

	buffer mod;
	mod.put((uint32_t)cv_signature_c13);
	mod.put((uint32_t)0);
	f.streams[stream_module] = std::move(mod.data);

	// The "* Linker *" module describes the image sections, as linkers emit it.
	buffer linker;
	linker.put((uint32_t)cv_signature_c13);

	buffer objname;
	objname.put((uint32_t)0);
	objname.put_str("* Linker *");
	put_record(linker, S_OBJNAME, objname);

	// Front end 0.0.0.0, back end 14.10.25019.0: the linker version lld-link reports, which tools accept.
	uint16_t versions[8] = { 0, 0, 0, 0, 14, 10, 25019, 0 };
	buffer compile;
	compile.put((uint32_t)cv_language_link);
	compile.put(img.cv_machine);
	compile.put(versions);
	compile.put_str("map-to-pdb");
	put_record(linker, S_COMPILE3, compile);

	unsigned long align = 0;
	_BitScanReverse(&align, img.section_align);

	for (size_t i = 0; i < img.sections.size(); ++i)
	{
		const IMAGE_SECTION_HEADER& s = img.sections[i];

		buffer sec;
		sec.put((uint16_t)(i + 1));
		sec.put((uint8_t)align);
		sec.put((uint8_t)0);
		sec.put(s.VirtualAddress);
		sec.put(s.Misc.VirtualSize);
		sec.put(s.Characteristics);
		sec.put_str(std::string_view((const char *)s.Name, strnlen((const char *)s.Name, sizeof(s.Name))));
		put_record(linker, S_SECTION, sec);
	}

	uint32_t linker_symbols = (uint32_t)linker.data.size();
	linker.put((uint32_t)0);
	f.streams[stream_linker_module] = std::move(linker.data);

	// DBI stream: modules, section contributions and map, files, names, debug headers.
	buffer contribs;
	contribs.put((uint32_t)section_contrib_v60);

	section_contrib first = {};
	first.section = 0xFFFF;
	first.size = 0xFFFFFFFF;
	first.module = 0xFFFF;

	for (size_t i = 0; i < img.sections.size(); ++i)
	{
		const IMAGE_SECTION_HEADER& s = img.sections[i];
		if (!s.Misc.VirtualSize)
		{
			continue;
		}

		section_contrib c = {};
		c.section = (uint16_t)(i + 1);
		c.size = s.Misc.VirtualSize;
		c.characteristics = s.Characteristics;
		contribs.put(c);

		if (first.section == 0xFFFF)
		{
			first = c;
		}
	}

	buffer modules;
	module_info m = {};
	m.contrib = first;
	m.stream = stream_module;
	m.symbol_bytes = sizeof(uint32_t);
	modules.put(m);
	modules.put_str(module);
	modules.put_str(module);
	modules.align(4);

	module_info lm = {};
	lm.mod = 1;
	lm.contrib.section = 0xFFFF;
	lm.contrib.size = 0xFFFFFFFF;
	lm.contrib.module = 0xFFFF;
	lm.stream = stream_linker_module;
	lm.symbol_bytes = linker_symbols;
	lm.pdb_name = 1;
	modules.put(lm);
	modules.put_str("* Linker *");
	modules.put_str("");
	modules.align(4);

	buffer section_map;
	uint16_t entries = (uint16_t)(img.sections.size() + 1);
	section_map.put(entries);
	section_map.put(entries);
	for (size_t i = 0; i < img.sections.size(); ++i)
	{
		uint32_t c = img.sections[i].Characteristics;

		section_map_entry e = {};
		e.flags = seg_selector;
		e.frame = (uint16_t)(i + 1);
		e.name = 0xFFFF;
		e.class_name = 0xFFFF;
		e.length = img.sections[i].Misc.VirtualSize;

		if (!(c & IMAGE_SCN_MEM_16BIT))
		{
			e.flags |= seg_32bit;
		}

		if (c & IMAGE_SCN_MEM_READ)
		{
			e.flags |= seg_read;
		}

		if (c & IMAGE_SCN_MEM_WRITE)
		{
			e.flags |= seg_write;
		}

		if (c & IMAGE_SCN_MEM_EXECUTE)
		{
			e.flags |= seg_execute;
		}

		section_map.put(e);
	}

	section_map_entry abs = {};
	abs.flags = seg_32bit | seg_absolute;
	abs.frame = entries;
	abs.name = 0xFFFF;
	abs.class_name = 0xFFFF;
	abs.length = 0xFFFFFFFF;
	section_map.put(abs);

	// Two modules, no source files: module indices 0 and 1, then a file count of 0 for each.
	uint16_t files[6] = { 2, 0, 0, 1, 0, 0 };
	std::vector<uint8_t> ec = string_table(pdb_name);

	uint16_t debug[debug_stream_count];
	for (uint16_t& d : debug)
	{
		d = 0xFFFF;
	}

	debug[debug_section_header] = stream_section_headers;

	dbi_header h = {};
	h.signature = -1;
	h.version = dbi_v70;
	h.age = img.age;
	h.globals_stream = stream_globals;
	h.build = 0x8E0B; // new version format, linker 14.11
	h.publics_stream = stream_publics;
	h.records_stream = stream_records;
	h.modules_size = (uint32_t)modules.data.size();
	h.contribs_size = (uint32_t)contribs.data.size();
	h.section_map_size = (uint32_t)section_map.data.size();
	h.files_size = sizeof(files);
	h.debug_header_size = sizeof(debug);
	h.ec_size = (uint32_t)ec.size();
	h.machine = img.machine;

	buffer dbi;
	dbi.put(h);
	dbi.put_bytes(modules.data.data(), modules.data.size());
	dbi.put_bytes(contribs.data.data(), contribs.data.size());
	dbi.put_bytes(section_map.data.data(), section_map.data.size());
	dbi.put(files);
	dbi.put_bytes(ec.data(), ec.size());
	dbi.put(debug);
	f.streams[stream_dbi] = std::move(dbi.data);

	return f.write(path);
}
