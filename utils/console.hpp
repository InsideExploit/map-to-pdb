#pragma once

#include <cstdarg>
#include <cstdio>

class console
{
public:
	static console& get()
	{
		static console c;
		return c;
	}

	void info(const char *fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		vfprintf(stdout, fmt, args);
		va_end(args);
		fputc('\n', stdout);
	}

	void warn(const char *fmt, ...)
	{
		fputs("warning: ", stderr);

		va_list args;
		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);
		fputc('\n', stderr);
	}

	void error(const char *fmt, ...)
	{
		fputs("error: ", stderr);

		va_list args;
		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);
		fputc('\n', stderr);
	}

private:
	console() = default;
	console(const console&) = delete;
	console& operator=(const console&) = delete;
};
