#include "msf.hpp"

#include "utils/buffer.hpp"
#include "utils/console.hpp"
#include "utils/file.hpp"

#pragma pack(push, 1)

struct superblock
{
	char magic[32];
	uint32_t block_size;
	uint32_t fpm_block;
	uint32_t block_count;
	uint32_t directory_size;
	uint32_t unknown;
	uint32_t block_map;
};

#pragma pack(pop)

static const char msf_magic[32] = "Microsoft C/C++ MSF 7.00\r\n\x1a" "DS\0\0";
static const uint32_t block_size = 4096;

bool msf::write(const std::wstring& path)
{
	// Blocks 1 and 2 of every block_size interval hold the two free page maps.
	uint32_t next = 3;
	auto alloc = [&]()
	{
		if (next % block_size == 1)
		{
			next += 2;
		}

		return next++;
	};

	std::vector<std::vector<uint32_t>> blocks(streams.size());
	for (size_t i = 0; i < streams.size(); ++i)
	{
		size_t n = (streams[i].size() + block_size - 1) / block_size;
		for (size_t j = 0; j < n; ++j)
		{
			blocks[i].push_back(alloc());
		}
	}

	buffer dir;
	dir.put((uint32_t)streams.size());
	for (const std::vector<uint8_t>& s : streams)
	{
		dir.put((uint32_t)s.size());
	}

	for (const std::vector<uint32_t>& b : blocks)
	{
		dir.put_bytes(b.data(), b.size() * sizeof(uint32_t));
	}

	std::vector<uint32_t> dir_blocks;
	size_t dir_count = (dir.data.size() + block_size - 1) / block_size;
	if (dir_count * sizeof(uint32_t) > block_size)
	{
		console::get().error("PDB would exceed the MSF directory limit");
		return false;
	}

	for (size_t i = 0; i < dir_count; ++i)
	{
		dir_blocks.push_back(alloc());
	}

	uint32_t map_block = alloc();

	// Every started interval must contain its free page map blocks.
	uint32_t count = next;
	uint32_t min_count = (count - 1) / block_size * block_size + 3;
	if (count < min_count)
	{
		count = min_count;
	}

	std::vector<uint8_t> out((size_t)count * block_size);

	superblock sb = {};
	memcpy(sb.magic, msf_magic, sizeof(sb.magic));
	sb.block_size = block_size;
	sb.fpm_block = 1;
	sb.block_count = count;
	sb.directory_size = (uint32_t)dir.data.size();
	sb.block_map = map_block;
	memcpy(out.data(), &sb, sizeof(sb));

	uint32_t intervals = (count + block_size - 1) / block_size;
	for (uint32_t k = 0; k < intervals; ++k)
	{
		memset(&out[((size_t)k * block_size + 1) * block_size], 0xFF, block_size * 2);
	}

	// A clear bit marks a used block; every block in the file is in use.
	for (uint32_t b = 0; b < count; ++b)
	{
		uint32_t byte = b / 8;
		size_t fpm = (size_t)(byte / block_size * block_size + 1) * block_size;
		out[fpm + byte % block_size] &= (uint8_t)~(1u << (b % 8));
	}

	for (size_t i = 0; i < streams.size(); ++i)
	{
		for (size_t j = 0; j < blocks[i].size(); ++j)
		{
			size_t n = streams[i].size() - j * block_size;
			if (n > block_size)
			{
				n = block_size;
			}

			memcpy(&out[(size_t)blocks[i][j] * block_size], streams[i].data() + j * block_size, n);
		}
	}

	for (size_t i = 0; i < dir_blocks.size(); ++i)
	{
		size_t n = dir.data.size() - i * block_size;
		if (n > block_size)
		{
			n = block_size;
		}

		memcpy(&out[(size_t)dir_blocks[i] * block_size], dir.data.data() + i * block_size, n);
	}

	memcpy(&out[(size_t)map_block * block_size], dir_blocks.data(), dir_blocks.size() * sizeof(uint32_t));

	if (!file::get().write(path, out))
	{
		console::get().error("cannot write '%ls'", path.c_str());
		return false;
	}

	return true;
}
