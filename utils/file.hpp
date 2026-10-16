#pragma once

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

class file
{
public:
	static file& get()
	{
		static file f;
		return f;
	}

	bool exists(const std::wstring& path)
	{
		return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
	}

	bool read(const std::wstring& path, std::vector<uint8_t>& out)
	{
		HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		if (h == INVALID_HANDLE_VALUE)
			return false;

		LARGE_INTEGER size = {};
		if (!GetFileSizeEx(h, &size))
		{
			CloseHandle(h);
			return false;
		}

		if (size.QuadPart >= 0x7FFFFFFF)
		{
			CloseHandle(h);
			return false;
		}

		out.resize((size_t)size.QuadPart);

		size_t done = 0;
		while (done < out.size())
		{
			DWORD got = 0;
			if (!ReadFile(h, out.data() + done, (DWORD)(out.size() - done), &got, nullptr))
			{
				break;
			}

			if (!got)
			{
				break;
			}

			done += got;
		}

		CloseHandle(h);
		return done == out.size();
	}

	bool write(const std::wstring& path, const std::vector<uint8_t>& data)
	{
		HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h == INVALID_HANDLE_VALUE)
			return false;

		size_t done = 0;
		while (done < data.size())
		{
			size_t left = data.size() - done;
			DWORD chunk = left > (1u << 30) ? (1u << 30) : (DWORD)left;
			DWORD put = 0;
			if (!WriteFile(h, data.data() + done, chunk, &put, nullptr))
			{
				break;
			}

			if (!put)
			{
				break;
			}

			done += put;
		}

		CloseHandle(h);

		bool ok = done == data.size();
		if (!ok)
		{
			DeleteFileW(path.c_str());
		}

		return ok;
	}

private:
	file() = default;
	file(const file&) = delete;
	file& operator=(const file&) = delete;
};
