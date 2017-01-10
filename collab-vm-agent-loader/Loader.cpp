#include "Agent.h"
#include "include/AgentProtocol.h"
#include <tchar.h>
#include <Setupapi.h>
#include "MemoryModule.h"

#define READ_TIMEOUT_MS 3000

DWORD Read(PAGENT_ARG_DATA data, LPVOID outBuf, DWORD bufSize)
{
	DWORD dwErr;
	if (!ReadFile(data->hCom, outBuf, bufSize, NULL, &data->oRead) && (dwErr = GetLastError()) != ERROR_IO_PENDING)
	{
		printf("ReadFile failed with error %d\n", dwErr);
		return FALSE;
	}
	DWORD read;
	if (GetOverlappedResult(data->hCom, &data->oRead, &read, TRUE))
	{
		return read;
	}
	else
	{
		printf("GetOverlappedResult failed with error code %d\n", GetLastError());
		return FALSE;
	}
}

BOOL ReadAll(PAGENT_ARG_DATA data, LPVOID outBuf, DWORD bufSize)
{
	LPBYTE buf = static_cast<LPBYTE>(outBuf);
	DWORD received = 0;
	do
	{
		DWORD read = Read(data, buf, bufSize - received);
		if (read == FALSE)
			return FALSE;
		received += read;
		buf += read;
	} while (received < bufSize);
	return TRUE;
}

DWORD Write(PAGENT_ARG_DATA data, LPCVOID inBuf, DWORD bufSize)
{
	DWORD dwErr;
	if (!WriteFile(data->hCom, inBuf, bufSize, NULL, &data->oWrite) && (dwErr = GetLastError()) != ERROR_IO_PENDING)
	{
		printf("WriteFile failed with error %d.\n", dwErr);
		return FALSE;
	}
	DWORD written;
	if (GetOverlappedResult(data->hCom, &data->oWrite, &written, TRUE))
	{
		return written;
	}
	else
	{
		printf("GetOverlappedResult failed with error code %d\n", GetLastError());
		return FALSE;
	}
}

BOOL WriteAll(PAGENT_ARG_DATA data, LPCVOID inBuf, DWORD bufSize)
{
	LPCBYTE buf = static_cast<LPCBYTE>(inBuf);
	DWORD sent = 0;
	do
	{
		DWORD wrote = Write(data, buf, bufSize - sent);
		if (wrote == FALSE)
			return FALSE;
		sent += wrote;
		buf += wrote;
	} while (sent < bufSize);
	return TRUE;
}

#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        EXTERN_C const GUID DECLSPEC_SELECTANY name \
                = { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }

DEFINE_GUID(GUID_VIOSERIAL_PORT,
	0x6fde7521, 0x1b65, 0x48ae, 0xb6, 0x28, 0x80, 0xbe, 0x62, 0x1, 0x60, 0x26);
// {6FDE7521-1B65-48ae-B628-80BE62016026}

PWCHAR GetVirtioSerialDevicePath(OUT LPVOID& lpDevice)
{
	HDEVINFO HardwareDeviceInfo;
	SP_DEVICE_INTERFACE_DATA DeviceInterfaceData;
	PSP_DEVICE_INTERFACE_DETAIL_DATA_W DeviceInterfaceDetailData = NULL;
	ULONG Length, RequiredLength = 0;
	BOOL bResult;

	HardwareDeviceInfo = SetupDiGetClassDevsW(
		&GUID_VIOSERIAL_PORT,
		NULL,
		NULL,
		(DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)
	);

	if (HardwareDeviceInfo == INVALID_HANDLE_VALUE)
	{
		puts("Cannot get class devices.");
		return NULL;
	}

	DeviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	bResult = SetupDiEnumDeviceInterfaces(HardwareDeviceInfo,
		0,
		&GUID_VIOSERIAL_PORT,
		0,
		&DeviceInterfaceData
	);

	if (bResult == FALSE) {
		puts("Cannot get enumerate device interfaces.");
		SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
		return NULL;
	}

	SetupDiGetDeviceInterfaceDetailW(
		HardwareDeviceInfo,
		&DeviceInterfaceData,
		NULL,
		0,
		&RequiredLength,
		NULL
	);

	lpDevice = DeviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)HeapAlloc(GetProcessHeap(), 0, RequiredLength);

	if (DeviceInterfaceDetailData == NULL)
	{
		puts("Cannot allocate memory.");
		SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
		return NULL;
	}

	DeviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

	Length = RequiredLength;

	bResult = SetupDiGetDeviceInterfaceDetailW(
		HardwareDeviceInfo,
		&DeviceInterfaceData,
		DeviceInterfaceDetailData,
		Length,
		&RequiredLength,
		NULL
	);

	if (bResult == FALSE)
	{
		puts("Cannot get device interface details.");
		SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
		HeapFree(GetProcessHeap(), 0, DeviceInterfaceDetailData);
		return NULL;
	}

	return DeviceInterfaceDetailData->DevicePath;
}

void Main()
{
	AGENT_ARG_DATA data{};
	LPVOID lpDevice;
	LPWSTR lpwszVirtio = GetVirtioSerialDevicePath(lpDevice);
	if (lpwszVirtio != NULL)
	{
		printf("Virtio serial device path: \"%ls\"\n", lpwszVirtio);
		data.hCom = CreateFileW(lpwszVirtio,
			GENERIC_READ | GENERIC_WRITE,
			0,
			NULL,
			OPEN_EXISTING,
			FILE_FLAG_OVERLAPPED,
			NULL);

		HeapFree(GetProcessHeap(), 0, lpDevice);

		if (data.hCom == INVALID_HANDLE_VALUE)
		{
			// Handle the error. 
			printf("CreateFile failed with error %d.\n", GetLastError());
			return;
		}

		goto start;
	}
	else
	{
		puts("Using serial port \"COM1\"");
		data.hCom = CreateFile(_T("\\\\.\\COM1"),
			GENERIC_READ | GENERIC_WRITE,
			0,
			NULL,
			OPEN_EXISTING,
			FILE_FLAG_OVERLAPPED,
			NULL);
	}

	if (data.hCom == INVALID_HANDLE_VALUE)
	{
		// Handle the error. 
		printf("CreateFile failed with error %d.\n", GetLastError());
		return;
	}

	DCB dcb;
	if (!GetCommState(data.hCom, &dcb))
	{
		printf("GetCommState failed with error %d.\n", GetLastError());
		return;
	}

	// MSDN recommends using the BuildCommDCB function instead of
	// modifying the DCB structure directly
	//dcb.ByteSize = 8;
	if (!BuildCommDCBA("baud=115200 parity=e data=8 stop=1", &dcb))
	{
		printf("BuildCommDCBA failed with error %d.\n", GetLastError());
		return;
	}

	printf("Buad Rate: %u, Parity Checking: %hu, Parity: %hu, Stop Bits: %hu\n", dcb.BaudRate, dcb.fParity, dcb.Parity, dcb.StopBits);

	//if (!SetCommState(data.hCom, &dcb))
	//{
	//	printf("SetCommState failed with error %d.\n", GetLastError());
	//	return;
	//}

	// Set input and output buffer sizes
	if (!SetupComm(data.hCom, AgentProtocol::kBufferSize, AgentProtocol::kBufferSize))
	{
		printf("SetupComm failed with error %d.\n", GetLastError());
		return;
	}

start:
	if (!(data.oRead.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL)) || 
		!(data.oWrite.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL)))
	{
		printf("CreateEvent failed with error %d.\n", GetLastError());
		return;
	}

	DWORD read;
	SIZE_T index = 0;
	DWORD agent_size;
	// Read agent size
	do
	{
		if (!ReadFile(data.hCom, reinterpret_cast<LPBYTE>(&agent_size) + index, sizeof(DWORD), NULL, &data.oRead))
		{
			DWORD dwRet = GetLastError();
			if (dwRet == ERROR_IO_PENDING)
			{
				puts("ReadFile is pending...");

				// Send a ping byte until we receive the agent module size
				do
				{
					const UCHAR ping = AgentProtocol::ClientOpcode::kGetAgent;
					if (!WriteAll(&data, reinterpret_cast<LPCVOID>(&ping), 1))
					{
						puts("Failed to write ping.");
						return;
					}
					// Wait for data to be read
				} while ((dwRet = WaitForSingleObject(data.oRead.hEvent, READ_TIMEOUT_MS)) == WAIT_TIMEOUT);

				if (dwRet == WAIT_FAILED)
				{
					puts("Failed to wait for event.");
					return;
				}
			}
			else
			{
				printf("ReadFile failed with error %d.\n", dwRet);
				return;
			}
		}

		if (!GetOverlappedResult(data.hCom, &data.oRead, &read, FALSE))
		{
			printf("GetOverlappedResult failed with error code %d\n", GetLastError());
			return;
		}
		index += read;
	} while (index < sizeof(DWORD));

	printf("Agent size is %u bytes\n", agent_size);

	LPVOID agent = HeapAlloc(GetProcessHeap(), 0, agent_size);
	if (!ReadAll(&data, agent, agent_size))
	{
		printf("Failed to read agent. Read: %u\n", read);
		return;
	}

	puts("Loading agent module...");

	MemoryLoadLibrary(agent, &data);

	HeapFree(GetProcessHeap(), 0, agent);
	//CloseHandle(data.oRead.hEvent);
	//CloseHandle(data.oWrite.hEvent);
	CloseHandle(data.hCom);
}

#ifdef _DEBUG

void DebugMain()
{
	AGENT_ARG_DATA data{};
	data.hCom = CreateFile(_T("\\\\.\\COM1"),
						   GENERIC_READ | GENERIC_WRITE,
						   0,    // exclusive access
						   NULL, // default security attributes
						   OPEN_EXISTING,
						   FILE_FLAG_OVERLAPPED,
						   NULL
	);

	if (data.hCom == INVALID_HANDLE_VALUE)
	{
		// Handle the error. 
		printf("CreateFile failed with error %d.\n", GetLastError());
		return;
	}

	// Set input and output buffer sizes
	if (!SetupComm(data.hCom, AgentProtocol::kBufferSize, AgentProtocol::kBufferSize))
	{
		printf("SetupComm failed with error %d.\n", GetLastError());
		return;
	}

	if (!(data.oRead.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL)) ||
		!(data.oWrite.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL)))
	{
		printf("CreateEvent failed with error %d.\n", GetLastError());
		return;
	}

	HANDLE hAgent = CreateFile("collab-vm-agent.dll", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);

	if (hAgent == INVALID_HANDLE_VALUE)
		return;

	DWORD agent_size = GetFileSize(hAgent, NULL);
	printf("Agent size is %u bytes\n", agent_size);

	HANDLE hAgentMap = CreateFileMapping(hAgent, NULL, PAGE_READONLY, 0, 0, NULL);
	if (hAgentMap == NULL)
		return;
	LPVOID agent = MapViewOfFile(hAgentMap, FILE_MAP_READ, 0, 0, 0);
	if (agent == NULL)
		return;

	puts("Loading agent module...");
	printf("Agent data address: %p\n", &data);

	MemoryLoadLibrary(agent, &data);

	UnmapViewOfFile(hAgentMap);
	CloseHandle(hAgent);

	CloseHandle(data.hCom);
}

#endif