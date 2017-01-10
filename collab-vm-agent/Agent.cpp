#include "Agent.h"

#ifdef _DEBUG
#include <stdarg.h>
int ConsoleOutput(const char* output, int len)
{
	int writtenTotal = 0;
	DWORD written;
	do
	{
		if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), output + writtenTotal, len - writtenTotal, &written, NULL))
			break;
		writtenTotal += written;
	} while (writtenTotal != written);
	return writtenTotal;
}

int printf(const char* format, ...)
{
	char buf[1024];
	int len;
	va_list vl;
	va_start(vl, format);
	len = wvsprintfA(buf, format, vl);
	va_end(vl);

	return ConsoleOutput(buf, len);
}

int puts(const char * str)
{
	size_t len = strlen(str);
	int written = ConsoleOutput(str, len);
	if (written != len)
		return written;
	written = ConsoleOutput("\n", 1);
	return len + written;
}
#endif