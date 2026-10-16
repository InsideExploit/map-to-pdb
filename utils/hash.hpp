#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>

class hash
{
public:
	static hash& get()
	{
		static hash h;
		return h;
	}

	// Hasher used by the reference PDB implementation for name tables and symbol buckets.
	uint32_t v1(std::string_view s)
	{
		const uint8_t *p = (const uint8_t *)s.data();
		size_t n = s.size();

		uint32_t r = 0;
		size_t i = 0;
		for (; i + 4 <= n; i += 4)
		{
			uint32_t v;
			memcpy(&v, p + i, 4);
			r ^= v;
		}

		if (n - i >= 2)
		{
			uint16_t v;
			memcpy(&v, p + i, 2);
			r ^= v;
			i += 2;
		}

		if (i < n)
		{
			r ^= p[i];
		}

		r |= 0x20202020;
		r ^= r >> 11;
		return r ^ (r >> 16);
	}

private:
	hash() = default;
	hash(const hash&) = delete;
	hash& operator=(const hash&) = delete;
};
