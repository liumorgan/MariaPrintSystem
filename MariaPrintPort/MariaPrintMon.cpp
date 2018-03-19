/*
 * Copyright (c) @marbocub <marbocub@gmail.com>
 * All rights reserved.
 *
 * MariaPrintMon : A part of Maria Print System.
 *
 */

#include "stdafx.h"
#include <winspool.h>
#include <winsplp.h>
#include <stdio.h>
#include <stdlib.h>

WCHAR szPortName[] = L"MARIAPRINT:";
WCHAR szMonitorName[] = L"Maria Print Port";
WCHAR szDescription[] = L"Maria Redirecter";

// handle
typedef struct _MARIAMONINI MARIAMONINI, *PMARIAMONINI;
typedef struct _PORTINI PORTINI, *PPORTINI;
struct _PORTINI {
	DWORD        signature;
	PMARIAMONINI pMariaMonIni;
	HANDLE       hFile;
	HANDLE       hPrinter;
	DWORD        JobId;
	TCHAR        filePath[MAX_PATH + 1] = { 0 };
};
struct _MARIAMONINI {
	DWORD        signature;
	PMONITORINIT pMonitorInit;
	PPORTINI     pPortIni;
};

PMARIAMONINI g_pMariaMonIni = NULL;

VOID EnterCritical(VOID);
VOID LeaveCritical(VOID);

BOOL WINAPI MariaEnumPorts(
	_In_     HANDLE  hMonitor,
	_In_opt_ LPWSTR  pName,
	_In_     DWORD   Level,
	_Out_    LPBYTE  pPorts,
	_In_     DWORD   cbBuf,
	_Out_    LPDWORD pcbNeeded,
	_Out_    LPDWORD pcReturned)
{
	size_t needed;
	size_t cbuf;
	DWORD count;
	PORT_INFO_1* pi1;
	PORT_INFO_2* pi2;
	LPTSTR pstr;

	UNREFERENCED_PARAMETER(pName);

	if (!pcbNeeded || !pcReturned || (!pPorts && (cbBuf > 0)))
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	else if ((1 != Level) && (2 != Level))
	{
		SetLastError(ERROR_INVALID_LEVEL);
		return FALSE;
	}

	needed = 0;
	if (1 == Level)
	{
		needed += sizeof(PORT_INFO_1);
		needed += (wcslen(szPortName) + 1) * sizeof(WCHAR);
	}
	else if (2 == Level)
	{
		needed += sizeof(PORT_INFO_2);
		needed += (wcslen(szPortName) + 1) * sizeof(WCHAR);
		needed += (wcslen(szMonitorName) + 1) * sizeof(WCHAR);
		needed += (wcslen(szDescription) + 1) * sizeof(WCHAR);
	}
	if (!(pPorts && (needed <= cbBuf)))
	{
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return FALSE;
	}

	count = 0;
	pstr = (LPTSTR)(pPorts + cbBuf);
	pi1 = (PORT_INFO_1*)pPorts;
	pi2 = (PORT_INFO_2*)pPorts;
	if (1 == Level)
	{
		cbuf = (wcslen(szPortName) + 1);
		pstr -= cbuf;
		wcscpy_s(pstr, cbuf, szPortName);
		pi1->pName = pstr;

		count++;
		pi1++;
	}
	else if (2 == Level)
	{
		cbuf = (wcslen(szPortName) + 1);
		pstr -= cbuf;
		wcscpy_s(pstr, cbuf, szPortName);
		pi2->pPortName = pstr;

		cbuf = (wcslen(szMonitorName) + 1);
		pstr -= cbuf;
		wcscpy_s(pstr, cbuf, szMonitorName);
		pi2->pMonitorName = pstr;

		cbuf = (wcslen(szDescription) + 1);
		pstr -= cbuf;
		wcscpy_s(pstr, cbuf, szDescription);
		pi2->pDescription = pstr;

		pi2->fPortType = PORT_TYPE_READ | PORT_TYPE_WRITE;
		pi2->Reserved = 0;

		count++;
		pi2++;
	}

	*pcbNeeded = (DWORD)needed;
	*pcReturned = count;

	return TRUE;
}

BOOL WINAPI MariaOpenPort(
	_In_     HANDLE hMonitor,
	_In_opt_ LPWSTR pName,
	_Out_    PHANDLE pHandle)
{
	PMARIAMONINI pMariaMonIni = (PMARIAMONINI)hMonitor;

	UNREFERENCED_PARAMETER(pName);

	if (!pName || !pHandle)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}

	*pHandle = NULL;

	EnterCritical();

	PPORTINI pPortIni = NULL;
	if (pMariaMonIni->pPortIni)
	{
		pPortIni = pMariaMonIni->pPortIni;
	}
	else
	{
		pPortIni = (PPORTINI)GlobalAlloc(GMEM_FIXED, sizeof(PORTINI));
		if (pPortIni) {
			ZeroMemory(pPortIni, sizeof(PORTINI));
		}
		pPortIni->hFile = INVALID_HANDLE_VALUE;
		pPortIni->hPrinter = INVALID_HANDLE_VALUE;
		pPortIni->pMariaMonIni = pMariaMonIni;
		pMariaMonIni->pPortIni = pPortIni;
	}

	LeaveCritical();

	*pHandle = (HANDLE)pPortIni;

	return TRUE;
}

BOOL WINAPI MariaStartDocPort(
	_In_ HANDLE hPort,
	_In_ LPWSTR pPrinterName,
	_In_ DWORD  JobId,
	_In_ DWORD  Level,
	_In_ LPBYTE pDocInfo)
{
	PPORTINI pPortIni = (PPORTINI)hPort;
	PDOC_INFO_1 pDocInfo1 = (PDOC_INFO_1)pDocInfo;

	if (1 != Level)
	{
		SetLastError(ERROR_INVALID_LEVEL);
		return FALSE;
	}

	if (pPortIni->hFile != INVALID_HANDLE_VALUE)
	{
		return TRUE;
	}


	WCHAR tmpPath[MAX_PATH + 1] = { 0 };
	if (0 == GetTempPath(_countof(tmpPath), tmpPath))
	{
		return FALSE;
	}
	//wcscpy_s(tmpPath, _countof(tmpPath), L"C:\\debug");
	WCHAR filePath[MAX_PATH + 1] = { 0 };
	if (0 == GetTempFileName(tmpPath, L"MR_", 0, filePath))
	{
		return FALSE;
	}

	EnterCritical();
	if (OpenPrinter(pPrinterName, &(pPortIni->hPrinter), NULL))
	{
		wcscpy_s(pPortIni->filePath, MAX_PATH + 1, filePath);
		pPortIni->hFile = CreateFile(pPortIni->filePath,
			GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL,
			OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
			NULL);
		pPortIni->JobId = JobId;
	}
	LeaveCritical();

	if (INVALID_HANDLE_VALUE == pPortIni->hFile)
	{
		return FALSE;
	}

	return TRUE;
}

BOOL WINAPI MariaWritePort(
	_In_  HANDLE  hPort,
	_In_  LPBYTE  pBuffer,
	      DWORD   cbBuf,
	_Out_ LPDWORD pcbWritten)
{
	PPORTINI pPortIni = (PPORTINI)hPort;
	BOOL result;

	if (!pcbWritten)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}

	*pcbWritten = 0;
	result = WriteFile(pPortIni->hFile, pBuffer, cbBuf, pcbWritten, NULL);

	if (result && *pcbWritten == 0)
	{
		SetLastError(ERROR_TIMEOUT);
		result = FALSE;
	}

	return result;
}

BOOL WINAPI MariaReadPort(
	_In_  HANDLE  hPort,
	_Out_ LPBYTE  pBuffer,
	      DWORD   cbBuffer,
	_Out_ LPDWORD pcbRead)
{
	*pcbRead = 0;

	return TRUE;
}

BOOL WINAPI MariaEndDocPort(
	_In_ HANDLE hPort)
{
	PPORTINI pPortIni = (PPORTINI)hPort;

	if (pPortIni->hFile != INVALID_HANDLE_VALUE)
	{
		FlushFileBuffers(pPortIni->hFile);

		EnterCritical();
		CloseHandle(pPortIni->hFile);
		pPortIni->hFile = INVALID_HANDLE_VALUE;
		LeaveCritical();
	}

	if (pPortIni->hPrinter != INVALID_HANDLE_VALUE)
	{
		SetJob(pPortIni->hPrinter, pPortIni->JobId, 0, NULL, JOB_CONTROL_SENT_TO_PRINTER);

		EnterCritical();
		ClosePrinter(pPortIni->hPrinter);
		pPortIni->hPrinter = INVALID_HANDLE_VALUE;
		LeaveCritical();
	}

	return TRUE;
}

BOOL WINAPI MariaClosePort(
	_In_ HANDLE hPort)
{
	PPORTINI pPortIni = (PPORTINI)hPort;

	EnterCritical();

	PMARIAMONINI pMariaMonIni = pPortIni->pMariaMonIni;
	if (pPortIni->hFile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(pPortIni->hFile);
		pPortIni->hFile = INVALID_HANDLE_VALUE;
	}
	if (pPortIni->hPrinter != INVALID_HANDLE_VALUE)
	{
		ClosePrinter(pPortIni->hPrinter);
		pPortIni->hPrinter = INVALID_HANDLE_VALUE;
	}
	GlobalFree(pPortIni);
	pMariaMonIni->pPortIni = NULL;

	LeaveCritical();

	return TRUE;
}

/*
BOOL WINAPI MariaAddPort(
	HANDLE hMonitor,
	LPWSTR pName,
	HWND hWnd,
	LPWSTR pMonitorName)
{
	return TRUE;
}

BOOL WINAPI MariaAddPortEx(
	HANDLE hMonitor,
	LPWSTR pName,
	DWORD Level,
	LPBYTE lpBuffer,
	LPWSTR pMonitorName)
{
	return TRUE;
}

BOOL WINAPI MariaConfigurePort(
	HANDLE hMonitor,
	LPWSTR pName,
	HWND hWnd,
	LPWSTR pPortName)
{
	return TRUE;
}

BOOL WINAPI MariaDeletePort(
	HANDLE hMonitor,
	LPWSTR pName,
	HWND hWnd,
	LPWSTR pPortName)
{
	return TRUE;
}

BOOL WINAPI MariaGetPrinterDataFromPort(
	HANDLE hPort,
	DWORD ControlID,
	LPWSTR pValueName,
	LPWSTR lpInBuffer,
	DWORD cbInBuffer,
	LPWSTR lpOutBuffer,
	DWORD cbOutBuffer,
	LPDWORD lpcbReturned)
{
	return TRUE;
}

BOOL WINAPI MariaSetPortTimeOuts(
	HANDLE hPort,
	LPCOMMTIMEOUTS lpCTO,
	DWORD reserved)
{
	return TRUE;
}

BOOL WINAPI MariaXcvOpenPort(
	HANDLE hMonitor,
	LPCWSTR pszObject,
	ACCESS_MASK GrantedAccess,
	PHANDLE phXcv)
{
	return TRUE;
}

DWORD WINAPI MariaXcvDataPort(
	HANDLE hXcv,
	LPCWSTR pszDataName,
	PBYTE pInputData,
	DWORD cbInputData,
	PBYTE pOutputData,
	DWORD cbOutputData,
	PDWORD pcbOutputNeeded)
{
	return 0;
}

BOOL WINAPI MariaXcvClosePort(
	HANDLE hXcv)
{
	return TRUE;
}
*/

VOID WINAPI MariaShutdown(
	_In_ HANDLE hMonitor)
{
	PMARIAMONINI pMariaMonIni = (PMARIAMONINI)hMonitor;

	if (pMariaMonIni->pPortIni)
	{
		if (INVALID_HANDLE_VALUE != pMariaMonIni->pPortIni->hFile)
		{
			CloseHandle(pMariaMonIni->pPortIni->hFile);
			pMariaMonIni->pPortIni->hFile = INVALID_HANDLE_VALUE;
		}
		if (pMariaMonIni->pPortIni->hPrinter != INVALID_HANDLE_VALUE)
		{
			ClosePrinter(pMariaMonIni->pPortIni->hPrinter);
			pMariaMonIni->pPortIni->hPrinter = INVALID_HANDLE_VALUE;
		}
		GlobalFree(pMariaMonIni->pPortIni);
		pMariaMonIni->pPortIni = NULL;
	}
	GlobalFree(pMariaMonIni);
}

/*
DWORD WINAPI MariaSendRecvBidiDataFromPort(
	HANDLE hPort,
	DWORD dwAccessBit,
	LPCWSTR pAction,
	PBIDI_REQUEST_CONTAINER pReqData,
	PBIDI_RESPONSE_CONTAINER* ppResData)
{
//	DebugLog(__FUNCTION__ L"() is called.");
	return 0;
}
*/


MONITOR2 mon2 = {
	sizeof(MONITOR2),
	MariaEnumPorts,		//EnumPorts
	MariaOpenPort,		//OpenPort
	NULL,				//OpenPortEx
	MariaStartDocPort,	//StartDocPort
	MariaWritePort,		//WritePort
	MariaReadPort,		//ReadPort
	MariaEndDocPort,	//EndDocPort
	MariaClosePort,		//ClosePort
	NULL,				//AddPort
	NULL,				//AddPortEx
	NULL,				//ConfigurePort
	NULL,				//DeletePort
	NULL,				//GetPrinterDataFromPort
	NULL,				//SetPortTimeOuts
	NULL,				//XcvOpenPort
	NULL,				//XcvDataPort
	NULL,				//XcvClosePort
	MariaShutdown,		//Shutdown
	NULL				//SendRecvBidiDataFromPort
};

LPMONITOR2 InitializePrintMonitor2(_In_ PMONITORINIT pMonitorInit, _Out_ PHANDLE phMonitor)
{
	PMARIAMONINI pMariaMonIni = (PMARIAMONINI)GlobalAlloc(GMEM_FIXED, sizeof(MARIAMONINI));
	if (pMariaMonIni) {
		ZeroMemory(pMariaMonIni, sizeof(MARIAMONINI));
	}

	pMariaMonIni->pMonitorInit = pMonitorInit;
	pMariaMonIni->pPortIni = NULL;

	g_pMariaMonIni = pMariaMonIni;
	*phMonitor = (HANDLE)pMariaMonIni;

	return &mon2;
}