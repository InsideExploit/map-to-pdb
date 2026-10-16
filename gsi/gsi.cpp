#include "gsi.hpp"

#include <algorithm>
#include <numeric>

#include "pdb/format.hpp"
#include "utils/buffer.hpp"
#include "utils/hash.hpp"

static const uint32_t bucket_count = 4096;

static bool is_ascii(std::string_view s)
{
	for (char c : s)
	{
		if ((uint8_t)c >= 0x80)
			return false;
	}

	return true;
}

static uint8_t lower(char c)
{
	if (c < 'A')
		return (uint8_t)c;

	if (c > 'Z')
		return (uint8_t)c;

	return (uint8_t)(c + ('a' - 'A'));
}

std::vector<uint8_t> gsi::hash_table(const std::vector<gsi_symbol>& syms)
{
	std::vector<uint32_t> bucket(syms.size());
	std::vector<uint32_t> sizes(bucket_count);
	for (size_t i = 0; i < syms.size(); ++i)
	{
		bucket[i] = hash::get().v1(syms[i].name) % bucket_count;
		sizes[bucket[i]]++;
	}

	// Readers stop scanning a bucket early, so records must follow the reference order:
	// shorter names first, then a case-insensitive compare for ASCII names.
	std::vector<uint32_t> order(syms.size());
	std::iota(order.begin(), order.end(), 0);
	std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b)
	{
		if (bucket[a] != bucket[b])
			return bucket[a] < bucket[b];

		std::string_view x = syms[a].name;
		std::string_view y = syms[b].name;
		if (x.size() != y.size())
			return x.size() < y.size();

		bool ascii = is_ascii(x);
		if (ascii)
		{
			ascii = is_ascii(y);
		}

		if (ascii)
		{
			for (size_t i = 0; i < x.size(); ++i)
			{
				uint8_t p = lower(x[i]);
				uint8_t q = lower(y[i]);
				if (p != q)
					return p < q;
			}
		}
		else
		{
			int c = memcmp(x.data(), y.data(), x.size());
			if (c)
				return c < 0;
		}

		return syms[a].record < syms[b].record;
	});

	std::vector<uint32_t> bitmap((bucket_count + 32) / 32);
	std::vector<uint32_t> starts;
	uint32_t first = 0;
	for (uint32_t b = 0; b < bucket_count; ++b)
	{
		if (!sizes[b])
		{
			continue;
		}

		bitmap[b / 32] |= 1u << (b % 32);

		// Offsets are stored as if each in-memory record were 12 bytes.
		starts.push_back(first * 12);
		first += sizes[b];
	}

	gsi_header h = {};
	h.signature = 0xFFFFFFFF;
	h.version = gsi_hash_v70;
	h.records_size = (uint32_t)(syms.size() * 8);
	h.buckets_size = (uint32_t)((bitmap.size() + starts.size()) * sizeof(uint32_t));

	buffer out;
	out.data.reserve(sizeof(h) + h.records_size + h.buckets_size);
	out.put(h);
	for (uint32_t i : order)
	{
		out.put(syms[i].record + 1);
		out.put((uint32_t)1);
	}

	out.put_bytes(bitmap.data(), bitmap.size() * sizeof(uint32_t));
	out.put_bytes(starts.data(), starts.size() * sizeof(uint32_t));
	return out.data;
}

std::vector<uint8_t> gsi::publics(const std::vector<gsi_symbol>& syms)
{
	std::vector<uint8_t> table = hash_table(syms);

	std::vector<uint32_t> order(syms.size());
	std::iota(order.begin(), order.end(), 0);
	std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b)
	{
		if (syms[a].seg != syms[b].seg)
			return syms[a].seg < syms[b].seg;

		if (syms[a].off != syms[b].off)
			return syms[a].off < syms[b].off;

		return syms[a].name < syms[b].name;
	});

	publics_header h = {};
	h.hash_size = (uint32_t)table.size();
	h.addr_map_size = (uint32_t)(syms.size() * sizeof(uint32_t));

	buffer out;
	out.data.reserve(sizeof(h) + table.size() + h.addr_map_size);
	out.put(h);
	out.put_bytes(table.data(), table.size());
	for (uint32_t i : order)
	{
		out.put(syms[i].record);
	}

	return out.data;
}
