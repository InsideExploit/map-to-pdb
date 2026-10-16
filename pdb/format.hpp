#pragma once

#include <cstdint>

// On-disk structures of the PDB 7.0 format, as written by link.exe and lld-link.

enum pdb_version : uint32_t
{
	pdb_info_vc70 = 20000404,
	tpi_v80 = 20040203,
	dbi_v70 = 19990903,
	section_contrib_v60 = 0xF12EBA2D,
	gsi_hash_v70 = 0xF12F091A,
	string_table_signature = 0xEFFEEFFE,
	cv_signature_c13 = 4,
};

enum cv_symbol_kind : uint16_t
{
	S_OBJNAME = 0x1101,
	S_PUB32 = 0x110E,
	S_SECTION = 0x1136,
	S_COMPILE3 = 0x113C,
};

enum cv_public_flag : uint32_t
{
	pub_function = 0x2,
};

enum cv_language : uint32_t
{
	cv_language_link = 7,
};

enum dbi_debug_stream
{
	debug_fpo,
	debug_exception,
	debug_fixup,
	debug_omap_to_src,
	debug_omap_from_src,
	debug_section_header,
	debug_token_rid_map,
	debug_xdata,
	debug_pdata,
	debug_new_fpo,
	debug_section_header_orig,
	debug_stream_count,
};

enum section_map_flag : uint16_t
{
	seg_read = 0x1,
	seg_write = 0x2,
	seg_execute = 0x4,
	seg_32bit = 0x8,
	seg_selector = 0x100,
	seg_absolute = 0x200,
};

#pragma pack(push, 1)

struct tpi_header
{
	uint32_t version;
	uint32_t header_size;
	uint32_t type_begin;
	uint32_t type_end;
	uint32_t record_bytes;
	uint16_t hash_stream;
	uint16_t hash_aux_stream;
	uint32_t hash_key_size;
	uint32_t hash_buckets;
	int32_t hash_values_off;
	uint32_t hash_values_len;
	int32_t index_offsets_off;
	uint32_t index_offsets_len;
	int32_t hash_adjust_off;
	uint32_t hash_adjust_len;
};

struct dbi_header
{
	int32_t signature;
	uint32_t version;
	uint32_t age;
	uint16_t globals_stream;
	uint16_t build;
	uint16_t publics_stream;
	uint16_t dll_version;
	uint16_t records_stream;
	uint16_t dll_build;
	uint32_t modules_size;
	uint32_t contribs_size;
	uint32_t section_map_size;
	uint32_t files_size;
	uint32_t type_server_size;
	uint32_t mfc_index;
	uint32_t debug_header_size;
	uint32_t ec_size;
	uint16_t flags;
	uint16_t machine;
	uint32_t reserved;
};

struct section_contrib
{
	uint16_t section;
	uint16_t pad1;
	uint32_t offset;
	uint32_t size;
	uint32_t characteristics;
	uint16_t module;
	uint16_t pad2;
	uint32_t data_crc;
	uint32_t reloc_crc;
};

struct module_info
{
	uint32_t mod;
	section_contrib contrib;
	uint16_t flags;
	uint16_t stream;
	uint32_t symbol_bytes;
	uint32_t c11_bytes;
	uint32_t c13_bytes;
	uint16_t files;
	uint16_t pad;
	uint32_t file_names_off;
	uint32_t source_name;
	uint32_t pdb_name;
};

struct section_map_entry
{
	uint16_t flags;
	uint16_t overlay;
	uint16_t group;
	uint16_t frame;
	uint16_t name;
	uint16_t class_name;
	uint32_t offset;
	uint32_t length;
};

struct gsi_header
{
	uint32_t signature;
	uint32_t version;
	uint32_t records_size;
	uint32_t buckets_size;
};

struct publics_header
{
	uint32_t hash_size;
	uint32_t addr_map_size;
	uint32_t thunks;
	uint32_t thunk_size;
	uint16_t thunk_section;
	uint16_t pad;
	uint32_t thunk_offset;
	uint32_t sections;
};

struct pub32_header
{
	uint16_t length;
	uint16_t kind;
	uint32_t flags;
	uint32_t offset;
	uint16_t segment;
};

#pragma pack(pop)

static_assert(sizeof(tpi_header) == 56);
static_assert(sizeof(dbi_header) == 64);
static_assert(sizeof(section_contrib) == 28);
static_assert(sizeof(module_info) == 64);
static_assert(sizeof(section_map_entry) == 20);
static_assert(sizeof(publics_header) == 28);
static_assert(sizeof(pub32_header) == 14);
