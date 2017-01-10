#include <memory.h>
#include "Agent.h"
#include "include/AgentProtocol.h"
#include <Shlwapi.h>
#include <ShlObj.h>

#define STRLEN(x) (sizeof(x) - 1)
#define WSTRLEN(x) (STRLEN(x)/sizeof(WCHAR))
#define LONG_PATH_PREFIX L"\\\\?\\"
#define NEW_NAME_PREFIX L" ("
#define NEW_NAME_SUFFIX L")"
#define NEW_NAME NEW_NAME_PREFIX L"2" NEW_NAME_SUFFIX

const DWORD kHeartbeatIntervalMs = 3000;

#pragma function(wcslen)
size_t wcslen(const wchar_t* s)
{
	const wchar_t *p;

	p = s;
	while (*p)
		p++;

	return p - s;
}

DWORD Write(PAGENT_ARG_DATA data, LPCVOID inBuf, DWORD bufSize)
{
	DWORD dwErr;
	if (!WriteFile(data->hCom, inBuf, bufSize, NULL, &data->oWrite) && (dwErr = GetLastError()) != ERROR_IO_PENDING)
	{
		printf("WriteFile failed with error %d\n", dwErr);
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

ULONGLONG GetTimestamp()
{
	FILETIME fileTime;
	GetSystemTimeAsFileTime(&fileTime);
	ULARGE_INTEGER ulTimestamp;
	ulTimestamp.LowPart = fileTime.dwLowDateTime;
	ulTimestamp.HighPart = fileTime.dwHighDateTime;
	// Convert to milliseconds
	 return ulTimestamp.QuadPart / 10000UL;
}

// Used to tell FreeDesktopPath when the string should be
// freed with CoTaskMemFree or LocalFree
BOOL bFreeWithCoTaskMem;

/**
 * Get the path of the current user's desktop directory.
 * The string should be null terminated and could
 * possibly end with a backslash. It should be freed using FreeDesktopPath.
 */
LPWSTR GetDesktopPath()
{
	// SHGetKnownFolderPath does not guarantee a path size less than MAX_PATH
	// so the path buffer is allocated from the heap for all functions
	LPWSTR wszPath;
	HMODULE hShell32 = LoadLibrary(L"Shell32.dll");
	if (hShell32)
	{
		// Windows Vista and above should use SHGetKnownFolderPath
		typedef HRESULT(WINAPI *SHGetKnownFolderPathT)(
			_In_     REFKNOWNFOLDERID rfid,
			_In_     DWORD            dwFlags,
			_In_opt_ HANDLE           hToken,
			_Out_    PWSTR            *ppszPath
			);

		SHGetKnownFolderPathT proc = (SHGetKnownFolderPathT)GetProcAddress(hShell32, "SHGetKnownFolderPath");
		// wszPath should be freed with CoTaskMemFree
		if (proc && proc(FOLDERID_Desktop, 0, NULL, &wszPath) == S_OK)
		{
			bFreeWithCoTaskMem = TRUE;
			return wszPath;
		}
	}

	bFreeWithCoTaskMem = FALSE;

	// Use deprecated function on Windows XP
	wszPath = (LPWSTR)LocalAlloc(0, MAX_PATH * sizeof(WCHAR));
	if (SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, wszPath) == S_OK)
		return wszPath;

	// Use unsupported function for Windows 2000
	if (SHGetSpecialFolderPathW(0, wszPath, CSIDL_DESKTOPDIRECTORY, TRUE) == TRUE)
		return wszPath;

	return NULL;
}

void FreeDesktopPath(LPWSTR wszPath)
{
	if (bFreeWithCoTaskMem)
		CoTaskMemFree(wszPath);
	else
		LocalFree(wszPath);
}

// The prefix "pull" in "pullNextHeartbeat" means pointer to ulonglong
BOOL SendHeartbeat(PAGENT_ARG_DATA agent, PULONGLONG pullNextHeartbeat)
{
	const BYTE heartbeatPacket[] = { 0, 0, AgentProtocol::ClientOpcode::kHeartbeat };
	if (!WriteAll(agent, &heartbeatPacket, sizeof(heartbeatPacket)))
		return FALSE;
	*pullNextHeartbeat = GetTimestamp() + kHeartbeatIntervalMs;
	return TRUE;
}

/**
* Write a length-prefixed registry value into a buffer.
* The buffer must be large enough to hold a one byte length prefix
* plus a 255 character WCHAR string. This function will fail if the
* data type of the value is not REG_SZ or if the string is longer
* than 255 characters.
*/
SIZE_T WriteRegValue(HKEY hkKey, LPCWSTR lpValueName, LPBYTE buf)
{
	DWORD dwType;
	DWORD written = UCHAR_MAX * sizeof(WCHAR);
	if (RegQueryValueExW(hkKey, lpValueName, NULL, &dwType, buf + 1, &written) == ERROR_SUCCESS &&
		dwType == REG_SZ && // The data type should be a string
		written % 2 == 0) // The size should always be even because it's a unicode string
	{
		written /= sizeof(WCHAR);
		// Don't send the null-terminator
		if (buf[written - 1] == '\0')
			written--;
		// Length prefix
		buf[0] = written;
		return 1 + written * sizeof(WCHAR);
	}
	// String length is zero bytes
	buf[0] = 0;
	return 1;
}

/**
* Writes the name of the OS, the service pack, the computer name,
* and the username into the buffer and returns the number
* of bytes written. The buffer should be a minimum of (1+255*2)*4 bytes.
*/
SIZE_T WriteConnectInfo(LPBYTE lpBuf)
{
	LPBYTE lpBufStart = lpBuf;
	SIZE_T cbWritten;
	HKEY hkKey;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
		0, KEY_READ, &hkKey) == ERROR_SUCCESS)
	{
		HMODULE hAdvapi32 = LoadLibrary(L"advapi32.dll");
		if (hAdvapi32)
		{
			typedef LONG (WINAPI *RegDisableReflectionKeyT)(
					__in HKEY hBase
				);
			RegDisableReflectionKeyT lpRegDisable = (RegDisableReflectionKeyT)
														GetProcAddress(hAdvapi32, "RegDisableReflectionKey");
			if (lpRegDisable)
				lpRegDisable(hkKey);
		}
		// Get OS name
		lpBuf += WriteRegValue(hkKey, L"ProductName", lpBuf);
		// Get service pack
		cbWritten = WriteRegValue(hkKey, L"CSDVersion", lpBuf);
		if (cbWritten == 1)
		{
			// If the service pack could not be obtained from the registry,
			// try to use GetVersionEx
			OSVERSIONINFOW info;
			info.dwOSVersionInfoSize = sizeof(OSVERSIONINFOW);
			if (GetVersionExW(&info))
			{
				cbWritten = wcslen(info.szCSDVersion);
				if (cbWritten)
				{
					lpBuf[0] = cbWritten;
					// Convert string length to byte count
					cbWritten *= sizeof(WCHAR);
					memcpy(lpBuf + 1, info.szCSDVersion, cbWritten);
					// Add one for the length prefix
					cbWritten++;
				}
			}
		}
		lpBuf += cbWritten;
		RegCloseKey(hkKey);
	}

	// Get computer name
	cbWritten = UCHAR_MAX;
	if (GetComputerNameW((LPWSTR)(lpBuf + 1), &cbWritten))
	{
		lpBuf[0] = cbWritten;
		lpBuf += cbWritten * sizeof(WCHAR) + 1;
	}
	else
	{
		lpBuf[0] = 0;
		lpBuf += 1;
	}

	// Get username
	cbWritten = UCHAR_MAX;
	if (GetUserNameW((LPWSTR)(lpBuf + 1), &cbWritten))
	{
		// cbWritten includes the null-terminator for GetUsername
		// so subtract one
		lpBuf[0] = --cbWritten;
		lpBuf += cbWritten * sizeof(WCHAR) + 1;
	}
	else
	{
		lpBuf[0] = 0;
		lpBuf += 1;
	}

	// Determine number of written bytes by subtracting the
	// start of the buffer from the end of the buffer
	return lpBuf - lpBufStart;
}

/**
 * Increments a number inside of a string.
 * @param begin Pointer to the most-significant digit.
 * @param end Pointer to the least-significant digit.
 * @ret True when the number was incremented successfully.
 * When the buffer pointed to by begin and end is not
 * big enough to store the incremented number all digits will
 * be set zero and this function will return false.
 */
BOOL IncrementNumber(PWCHAR pBegin, PWCHAR pEnd)
{
	PWCHAR p = pEnd;
	while (p >= pBegin)
	{
		WCHAR c = *p;
		if (c == '9')
		{
			*p-- = '0';
		}
		else
		{
			*p = c + 1;
			return TRUE;
		}
	}
	return FALSE;
}

/**
 * Find an available filename and return an open handle to it.
 * @param hFile The handle to the opened file.
 * @ret ERROR_SUCCESS if a filename was found, ERROR_FILE_EXISTS if
 * there was not enough space in the buffer, or the error from CreateFile.
 */
DWORD FindFilename(_Out_ PHANDLE hFile, LPWSTR wszFilePath, LPWSTR wszFilename, DWORD dwFilenameLen, DWORD dwMaxFilename)
{
	if (dwFilenameLen <= dwMaxFilename - WSTRLEN(NEW_NAME))
	{
		// Attempt to give the file a new name
		PWSTR pFileExt = PathFindExtensionW(wszFilename);

		if (pFileExt == wszFilename)
		{
			// There is no file extension
			pFileExt = wszFilename + dwFilenameLen * sizeof(WCHAR);
		}
		else
		{
			// Make space for the " (X)" by copying the file extension and the
			// null terminator to the right
			// memcpy cannot be used here because the two buffers could overlap
			//memcpy(pFileExt + WSTRLEN(NEW_NAME), pFileExt,
			//	((wszFilename + dwFilenameLen) - pFileExt + 1) * sizeof(WCHAR));
			for (PWCHAR p = &pFileExt[wszFilename + dwFilenameLen - pFileExt]; p >= pFileExt; p--)
				p[WSTRLEN(NEW_NAME)] = *p;
		}
		memcpy(pFileExt, NEW_NAME, WSTRLEN(NEW_NAME) * sizeof(WCHAR));

		// Pointers to the beginning and end of the number
		PWCHAR pBegin = pFileExt + WSTRLEN(NEW_NAME_PREFIX);
		PWCHAR pEnd = pBegin;

		while (TRUE)
		{
			do
			{
				*hFile = CreateFileW(wszFilePath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
					CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);

				if (*hFile != INVALID_HANDLE_VALUE)
					return ERROR_SUCCESS;

				DWORD dwErr = GetLastError();
				if (dwErr != ERROR_FILE_EXISTS)
					return dwErr;

			} while (IncrementNumber(pBegin, pEnd));

			DWORD numDigits = pEnd - pBegin + 1;
			// Check if there is enough space in the path to add another digit
			if (dwFilenameLen + WSTRLEN(NEW_NAME_PREFIX) +
				numDigits + WSTRLEN(NEW_NAME_SUFFIX) >= dwMaxFilename)
				return ERROR_FILE_EXISTS;

			DWORD fileExtLen = wszFilename + dwFilenameLen - pFileExt;
			// Copy the number and the file extension one character to the right
			// memcpy cannot be used here because the two buffers overlap
			for (DWORD i = numDigits + fileExtLen + WSTRLEN(NEW_NAME_SUFFIX) + 1; i > 0; i--)
				pBegin[i] = pBegin[i - 1];

			// Set the most significant digit to one
			*pBegin = '1';
			pEnd++;
		}
	}
	return ERROR_FILE_EXISTS;
}

BOOL ReserveFileSpace(HANDLE hFile, DWORD dwSpace)
{
	return SetFilePointer(hFile, dwSpace, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER && 
		SetEndOfFile(hFile) &&
		SetFilePointer(hFile, 0, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER;
}

BOOL ReadAll(PAGENT_ARG_DATA agent, LPVOID outBuf, DWORD bufSize, PULONGLONG pullNextHeartbeat)
{
	LPBYTE buf = static_cast<LPBYTE>(outBuf);
	DWORD received = 0;
	DWORD dwErr;
	do
	{
		if (!ReadFile(agent->hCom, buf, bufSize - received, NULL, &agent->oRead))
		{
			dwErr = GetLastError();
			if (dwErr == ERROR_IO_PENDING)
			{
				ULONGLONG ulTimestamp = GetTimestamp();
				ULONGLONG ulRemaining;
				if (ulTimestamp >= *pullNextHeartbeat)
				{
					if (!SendHeartbeat(agent, pullNextHeartbeat))
						return FALSE;
					ulRemaining = kHeartbeatIntervalMs;
				}
				else
				{
					ulRemaining = *pullNextHeartbeat - ulTimestamp;
				}
				// Wait until data has been read or until we need to send a heartbeat
				while ((dwErr = WaitForSingleObject(agent->oRead.hEvent, ulRemaining)) == WAIT_TIMEOUT)
				{
					if (!SendHeartbeat(agent, pullNextHeartbeat))
						return FALSE;
					ulRemaining = kHeartbeatIntervalMs;
				}
				if (dwErr != WAIT_OBJECT_0)
				{
					printf("WaitForSingleObject failed with error %u\n", dwErr);
					return FALSE;
				}
			}
			else
			{
				printf("ReadFile failed with error %u\n", dwErr);
				return FALSE;
			}
		}
		DWORD read;
		if (!GetOverlappedResult(agent->hCom, &agent->oRead, &read, FALSE))
		{
			//if (dwErr = GetLastError() != ERROR_IO_PENDING)
			{
				printf("GetOverlappedResult failed with error code %u\n", GetLastError());
				return FALSE;
			}
		}
		received += read;
		buf += read;
	} while (received < bufSize);
	return TRUE;
}

void Main(PAGENT_ARG_DATA agent)
{
	using AgentProtocol::ClientOpcode;
	using AgentProtocol::ServerOpcode;

	puts("Agent module has been loaded");

//#ifndef _DEBUG
	LPWSTR wszDesktopPath = GetDesktopPath();
	if (wszDesktopPath == NULL)
		return;
	printf("Target upload directory is \"%ls\"\n", wszDesktopPath);

	SIZE_T cxDesktopPathLen = wcslen(wszDesktopPath);
	if (!cxDesktopPathLen)
		return;
//#else
//	WCHAR wszDesktopPath[] = L"C:\\Users\\test\\Desktop";
//	SIZE_T cxDesktopPathLen = WSTRLEN(wszDesktopPath);
//#endif

	// Set the current directory to the upload path so we can specify
	// NULL for GetVolumeInformation and ShellExecute
	if (cxDesktopPathLen < MAX_PATH-2)
		SetCurrentDirectoryW(wszDesktopPath);

	LPWSTR wszUploadPath = (LPWSTR)LocalAlloc(0, (/*WSTRLEN(LONG_PATH_PREFIX) + */cxDesktopPathLen +
		WSTRLEN('\\') + 255 + WSTRLEN('\0')) * sizeof(WCHAR));

	// Combine prefix + desktop path + \ + null terminator
	//memcpy(wszUploadPath, LONG_PATH_PREFIX, WSTRLEN(LONG_PATH_PREFIX) * sizeof(WCHAR));
	//memcpy(&wszUploadPath[WSTRLEN(LONG_PATH_PREFIX)], wszDesktopPath, (cxDesktopPathLen + 1) * sizeof(WCHAR));
	//cxDesktopPathLen += WSTRLEN(LONG_PATH_PREFIX);

	memcpy(wszUploadPath, wszDesktopPath, (cxDesktopPathLen + 1) * sizeof(WCHAR));
	if (wszUploadPath[cxDesktopPathLen - 1] != '\\')
	{
		wszUploadPath[cxDesktopPathLen] = '\\';
		wszUploadPath[cxDesktopPathLen + 1] = '\0';
		cxDesktopPathLen++;
	}

	FreeDesktopPath(wszDesktopPath);

	DWORD dwMaxComponent;
	if (!GetVolumeInformationW(NULL, NULL, NULL, NULL, &dwMaxComponent, NULL, NULL, NULL))
	{
		printf("GetVolumeInformation failed with error %u\n", GetLastError());
		return;
	}

	printf("Max file path component is %u characters\n", dwMaxComponent);

	HANDLE hDesktop;
	if (wszUploadPath)
	{
		hDesktop = CreateFileW(wszUploadPath, GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	}

	LPWSTR wszFilename = wszUploadPath + cxDesktopPathLen;

	USHORT cbPacketSize = AgentProtocol::kHeaderSize;
	BYTE opcode;
	BYTE buf[AgentProtocol::kBufferSize];

	LPBYTE p = buf + sizeof(uint16_t);
	AgentProtocol::WriteUint8(ClientOpcode::kConnect, &p);
	AgentProtocol::WriteUint32(dwMaxComponent, &p);
	p += WriteConnectInfo(p);
	// Length prefix
	AgentProtocol::WriteUint16(p - (buf + sizeof(uint16_t) + 1), buf);
	if (!WriteAll(agent, buf, p - buf))
	{
		puts("Failed to send connect packet");
		return;
	}

	enum ReadState { kHeader, kBody } state = ReadState::kHeader;

	ULONGLONG ullNextHeartbeat = 0ULL;

	// Handle to a file that is being uploaded
	HANDLE hFile;

	while (TRUE)
	{
		if (!ReadAll(agent, buf, cbPacketSize, &ullNextHeartbeat))
			return;

		p = buf;
		if (state == ReadState::kHeader)
		{
			cbPacketSize = AgentProtocol::ReadUint16(&p);
			opcode = AgentProtocol::ReadUint8(&p);
			state = ReadState::kBody;
			continue;
		}

		//printf("Received packet with opcode: %u and size: %u\n", opcode, cbPacketSize);
		switch (opcode)
		{
		case ServerOpcode::kFileDlBegin:
		{
			// Read length of filename
			BYTE filenameLen = AgentProtocol::ReadUint8(&p);
			if (filenameLen > dwMaxComponent)
				break;
			// Append the filename to the upload path
			memcpy(wszFilename, p, filenameLen * sizeof(WCHAR));
			wszFilename[filenameLen] = '\0';
			p += filenameLen * sizeof(WCHAR);
			// Read total file size
			DWORD dwFileSize = AgentProtocol::ReadUint32(&p);

			printf("Beginning file upload for \"%ls\" (%u bytes)\n", wszFilename, dwFileSize);
			// Reset pointer for writing
			p = buf;
			hFile = CreateFileW(wszUploadPath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
								CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
			if (hFile == INVALID_HANDLE_VALUE)
			{
				DWORD dwErr = GetLastError();
				if (dwErr == ERROR_FILE_EXISTS)
				{
					dwErr = FindFilename(&hFile, wszUploadPath, wszFilename, filenameLen, dwMaxComponent);
					if (dwErr == ERROR_SUCCESS)
					{
						printf("File was renamed to \"%ls\"\n", wszFilename);
						if (dwFileSize)
							ReserveFileSpace(hFile, dwFileSize);

						filenameLen = wcslen(wszFilename);
						AgentProtocol::WriteUint16(1 + filenameLen * sizeof(WCHAR), &p);
						AgentProtocol::WriteUint8(ClientOpcode::kFileCreationNewName, &p);
						AgentProtocol::WriteUint8(filenameLen, &p);
						memcpy(p, wszFilename, filenameLen * sizeof(WCHAR));
						p += filenameLen * sizeof(WCHAR);
						if (!WriteAll(agent, buf, p - buf))
							return;
						break;
					}
				}

				AgentProtocol::WriteUint16(sizeof(uint32_t), &p);
				AgentProtocol::WriteUint8(AgentProtocol::ClientOpcode::kFileCreateFailed, &p);
				AgentProtocol::WriteUint32(dwErr, &p);
				if (!WriteAll(agent, buf, p - buf))
					return;
			}
			else
			{
				if (dwFileSize)
					ReserveFileSpace(hFile, dwFileSize);

				AgentProtocol::WriteUint16(0, &p);
				AgentProtocol::WriteUint8(ClientOpcode::kFileCreationSuccess, &p);
				if (!WriteAll(agent, buf, p - buf))
					return;
			}
			break;
		}
		case ServerOpcode::kFileDlPart:
		{
			DWORD read;
			if (!WriteFile(hFile, p, cbPacketSize, &read, NULL) || read != cbPacketSize)
			{
				CloseHandle(hFile);

				AgentProtocol::WriteUint16(0, &p);
				AgentProtocol::WriteUint8(ClientOpcode::kFileWriteFailed, &p);
				if (!WriteAll(agent, buf, p - buf))
					return;
			}
			break;
		}
		case ServerOpcode::kFileDlEndShellExec:
		{
			UCHAR len = AgentProtocol::ReadUint8(&p);
			LPWSTR wszArgs;
			if (len)
			{
				wszArgs = (LPWSTR)p;
				// Add one for null-terminator
				p += len + 1;
			}
			else
			{
				wszArgs = NULL;
			}
			UCHAR nShowCmd = AgentProtocol::ReadUint8(&p);
			printf("Finished uploading file to \"%ls\"\n"
					"Executing with arguments \"%ls\" and window: %s\n", wszUploadPath,
					wszArgs ? wszArgs : L"", nShowCmd == SW_SHOWNORMAL ? "visible" : "hidden");
			// Close the handle first to allow other applications to have access to the file
			CloseHandle(hFile);
			HINSTANCE hInst = ShellExecuteW(NULL, NULL, wszUploadPath, wszArgs, NULL, nShowCmd);
			if ((int)hInst > 32)
				puts("File was successfully executed.");
			else
				printf("Failed to execute file. Error code %u\n", (int)hInst);

			p = buf;
			AgentProtocol::WriteUint16(sizeof(uint32_t), &p);
			AgentProtocol::WriteUint8(ClientOpcode::kShellExecResult, &p);
			AgentProtocol::WriteUint32(reinterpret_cast<uint32_t>(hInst), &p);
			if (!WriteAll(agent, buf, p - buf))
				return;
			break;
		}
		case ServerOpcode::kFileDlEnd:
			printf("Finished uploading file to \"%ls\"\n", wszUploadPath);
			CloseHandle(hFile);
			break;
		}

		if (GetTimestamp() >= ullNextHeartbeat)
		{
			if (!SendHeartbeat(agent, &ullNextHeartbeat))
				return;
		}

		// Read another packet
		state = ReadState::kHeader;
		cbPacketSize = AgentProtocol::kHeaderSize;
	}
}

#ifdef _DEBUG
void DebugMain()
{
	HANDLE hPipe = ::CreateNamedPipeW(
		L"\\\\.\\pipe\\collab-vm-agent",	// pipe name 
		PIPE_ACCESS_DUPLEX |     // read/write access 
		FILE_FLAG_OVERLAPPED,    // overlapped mode 
		PIPE_TYPE_BYTE |      // message-type pipe 
		PIPE_READMODE_BYTE |  // message-read mode 
		PIPE_WAIT,               // blocking mode 
		1,               // number of instances 
		AgentProtocol::kBufferSize,   // output buffer size 
		AgentProtocol::kBufferSize,   // input buffer size 
		0,            // client time-out 
		NULL);                   // default security attributes

	if (hPipe == INVALID_HANDLE_VALUE)
	{
		printf("CreateNamedPipe failed with %d\n", GetLastError());
		return;
	}

	AGENT_ARG_DATA agent{ hPipe };

	if (!(agent.oRead.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL)) ||
		!(agent.oWrite.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL)))
	{
		printf("CreateEvent failed with error %d\n", GetLastError());
		return;
	}

	ConnectNamedPipe(hPipe, &agent.oRead);
	DWORD dwErr = GetLastError();
	if (dwErr == ERROR_IO_PENDING)
	{
		puts("Waiting for pipe client to connect...");
		DWORD read;
		if (!GetOverlappedResult(hPipe, &agent.oRead, &read, TRUE))
		{
			printf("GetOverlappedResult failed with error code %d\n", GetLastError());
			return;
		}
	}
	else if (dwErr != ERROR_PIPE_CONNECTED)
	{
		printf("ConnectNamedPipe failed with error %d\n", dwErr);
		return;
	}

	Main(&agent);

	CloseHandle(hPipe);
}
#endif