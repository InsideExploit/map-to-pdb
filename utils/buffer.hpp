#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

class buffer
{
public:
	std::vector<uint8_t> data;

	template <typename T>
	void put(const T& v)
	{
		size_t at = data.size();
		data.resize(at + sizeof(T));
		memcpy(data.data() + at, &v, sizeof(T));
	}

	void put_bytes(const void *p, size_t n)
	{
		if (!n)
			return;

		size_t at = data.size();
		data.resize(at + n);
		memcpy(data.data() + at, p, n);
	}

	void put_str(std::string_view s)
	{
		put_bytes(s.data(), s.size());
		data.push_back(0);
	}

	void align(size_t a)
	{
		data.resize((data.size() + a - 1) / a * a);
	}
};
