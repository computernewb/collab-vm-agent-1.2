#pragma once
#include <Windows.h>

#ifdef _DEBUG
int printf(const char* format, ...);
int puts(const char * str);
#else
#define printf
#define puts
#endif

typedef struct AGENT_ARG_DATA
{
	HANDLE hCom;
	OVERLAPPED oRead;
	OVERLAPPED oWrite;
} *PAGENT_ARG_DATA;
