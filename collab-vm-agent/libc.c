#include <string.h>

#pragma function(memset)
void *memset(void *_Dst, int _Val, size_t _Size)
{
	unsigned char *p = _Dst;
	while (_Size-- > 0) *p++ = _Val;
	return _Dst;
}

#pragma function(memcpy)
void *memcpy(void *_dst, const void *_src, unsigned len)
{
	unsigned char *dst = _dst;
	const unsigned char *src = _src;
	while (len-- > 0) {
		*dst++ = *src++;
	}
	return _dst;
}

#pragma function(strlen)
size_t strlen(const char *str)
{
	register const char *s;

	for (s = str; *s; ++s);
	return(s - str);
}

#pragma function(strcpy)
char* strcpy(char *dest, const char *src)
{
	return memcpy(dest, src, strlen(src) + 1);
}
