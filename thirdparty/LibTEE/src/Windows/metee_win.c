/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2014-2019 Intel Corporation
 */
#include <assert.h>
#include <windows.h>
#include <initguid.h>
#include <tchar.h>
#include "helpers.h"
#include "Public.h"
#include "metee.h"

static inline struct METEE_WIN_IMPL *to_int(PTEEHANDLE _h)
{
	return _h ? (struct METEE_WIN_IMPL *)_h->handle : NULL;
}

/**********************************************************************
 **                          TEE Lib Function                         *
 **********************************************************************/
TEESTATUS TEEAPI TeeInit(IN OUT PTEEHANDLE handle, IN const UUID *uuid, IN OPTIONAL const char *device)
{
	TEESTATUS       status               = INIT_STATUS;
	TCHAR           devicePath[MAX_PATH] = {0};
	HANDLE          deviceHandle         = INVALID_HANDLE_VALUE;
	LPCGUID         currentUUID          = NULL;
	struct METEE_WIN_IMPL *impl_handle   = NULL;

	FUNC_ENTRY();

	if (NULL == uuid || NULL == handle) {
		status = TEE_INVALID_PARAMETER;
		ERRPRINT("One of the parameters was illegal");
		goto Cleanup;
	}

	impl_handle = (struct METEE_WIN_IMPL *)malloc(sizeof(*impl_handle));
	if (NULL == impl_handle) {
		status = TEE_INTERNAL_ERROR;
		ERRPRINT("Can't allocate memory for internal struct");
		goto Cleanup;
	}

	__tee_init_handle(handle);
	handle->handle = impl_handle;

	if (device != NULL) {
		currentUUID = (LPCGUID)device;
	}
	else {
		currentUUID = &GUID_DEVINTERFACE_HECI;
	}

	// get device path
	status = GetDevicePath(currentUUID, devicePath, MAX_PATH);
	if (status) {
		ERRPRINT("Error in GetDevicePath, error: %d\n", status);
		goto Cleanup;
	}

	// create file
	deviceHandle = CreateFile(devicePath,
					GENERIC_READ | GENERIC_WRITE,
					FILE_SHARE_READ | FILE_SHARE_WRITE,
					NULL,
					OPEN_EXISTING,
					FILE_FLAG_OVERLAPPED,
					NULL);

	if (deviceHandle == INVALID_HANDLE_VALUE) {
		status = TEE_DEVICE_NOT_READY;
		ERRPRINT("Error in CreateFile, error: %lu\n", GetLastError());
		goto Cleanup;
	}

	status = TEE_SUCCESS;

Cleanup:

	if (TEE_SUCCESS == status) {
		impl_handle->handle = deviceHandle;
		error_status_t result  = memcpy_s(&impl_handle->uuid, sizeof(impl_handle->uuid), uuid, sizeof(UUID));
		if (result != 0) {
			ERRPRINT("Error in in uuid copy: result %u\n", result);
			status = TEE_UNABLE_TO_COMPLETE_OPERATION;
		}
	}
	else {
		CloseHandle(deviceHandle);
		if (impl_handle)
			free(impl_handle);
		if (handle)
			handle->handle = NULL;
	}

	FUNC_EXIT(status);

	return status;
}

TEESTATUS TEEAPI TeeConnect(OUT PTEEHANDLE handle)
{
	struct METEE_WIN_IMPL *impl_handle = to_int(handle);
	TEESTATUS       status        = INIT_STATUS;
	DWORD           bytesReturned = 0;
	FW_CLIENT       fwClient      = {0};


	FUNC_ENTRY();

	if (NULL == impl_handle) {
		status = TEE_INVALID_PARAMETER;
		ERRPRINT("One of the parameters was illegal");
		goto Cleanup;
	}

	status = SendIOCTL(impl_handle->handle, (DWORD)IOCTL_TEEDRIVER_CONNECT_CLIENT,
						(LPVOID)&impl_handle->uuid, sizeof(GUID),
						&fwClient, sizeof(FW_CLIENT),
						&bytesReturned);
	if (status) {
		DWORD err = GetLastError();
		status = Win32ErrorToTee(err);
		// Connect IOCTL returns invalid handle if client is not found
		if (status == TEE_INVALID_PARAMETER)
			status = TEE_CLIENT_NOT_FOUND;
		ERRPRINT("Error in SendIOCTL, error: %lu\n", err);
		goto Cleanup;
	}

	handle->maxMsgLen  = fwClient.MaxMessageLength;
	handle->protcolVer = fwClient.ProtocolVersion;

	status = TEE_SUCCESS;

Cleanup:

	FUNC_EXIT(status);

	return status;
}

TEESTATUS TEEAPI TeeRead(IN PTEEHANDLE handle, IN OUT void* buffer, IN size_t bufferSize,
			 OUT OPTIONAL size_t* pNumOfBytesRead, IN OPTIONAL uint32_t timeout)
{
	struct METEE_WIN_IMPL *impl_handle = to_int(handle);
	TEESTATUS       status = INIT_STATUS;
	EVENTHANDLE     evt    = NULL;

	FUNC_ENTRY();

	if (NULL == impl_handle || NULL == buffer || 0 == bufferSize) {
		status = TEE_INVALID_PARAMETER;
		ERRPRINT("One of the parameters was illegal");
		goto Cleanup;
	}

	status = BeginReadInternal(impl_handle->handle, buffer, (ULONG)bufferSize, &evt);
	if (status) {
		ERRPRINT("Error in BeginReadInternal, error: %d\n", status);
		goto Cleanup;
	}

	impl_handle->evt = evt;

	if (timeout == 0)
		timeout = INFINITE;

	status = EndReadInternal(impl_handle->handle, evt, timeout, (LPDWORD)pNumOfBytesRead);
	if (status) {
		ERRPRINT("Error in EndReadInternal, error: %d\n", status);
		goto Cleanup;
	}

	status = TEE_SUCCESS;

Cleanup:
	if (impl_handle)
		impl_handle->evt = NULL;

	FUNC_EXIT(status);

	return status;
}

TEESTATUS TEEAPI TeeWrite(IN PTEEHANDLE handle, IN const void* buffer, IN size_t bufferSize,
			  OUT OPTIONAL size_t* numberOfBytesWritten, IN OPTIONAL uint32_t timeout)
{
	struct METEE_WIN_IMPL *impl_handle = to_int(handle);
	TEESTATUS       status = INIT_STATUS;
	EVENTHANDLE     evt    = NULL;

	FUNC_ENTRY();

	if (NULL == impl_handle || NULL == buffer || 0 == bufferSize) {
		status = TEE_INVALID_PARAMETER;
		ERRPRINT("One of the parameters was illegal");
		goto Cleanup;
	}

	status = BeginWriteInternal(impl_handle->handle, (PVOID)buffer, (ULONG)bufferSize, &evt);
	if (status) {
		ERRPRINT("Error in BeginWrite, error: %d\n", status);
		goto Cleanup;
	}

	impl_handle->evt = evt;

	if (timeout == 0)
		timeout = INFINITE;

	status = EndWriteInternal(impl_handle->handle, evt, timeout, (LPDWORD)numberOfBytesWritten);
	if (status) {
		ERRPRINT("Error in EndWrite, error: %d\n", status);
		goto Cleanup;
	}

	status = TEE_SUCCESS;

Cleanup:
	if (impl_handle)
		impl_handle->evt = NULL;
	FUNC_EXIT(status);

	return status;
}

VOID TEEAPI TeeDisconnect(IN PTEEHANDLE handle)
{
	struct METEE_WIN_IMPL *impl_handle = to_int(handle);
	DWORD ret;

	FUNC_ENTRY();
	if (NULL == impl_handle) {
		goto Cleanup;
	}

	if (CancelIo(impl_handle->handle)) {
		ret = WaitForSingleObject(impl_handle->evt, CANCEL_TIMEOUT);
		if (ret != WAIT_OBJECT_0) {
			ERRPRINT("Error in WaitForSingleObject, return: %lu, error: %lu\n", ret, GetLastError());
		}
	}

	CloseHandle(impl_handle->handle);
	free(impl_handle);
	handle->handle = NULL;

Cleanup:
	FUNC_EXIT(0);
}

TEE_DEVICE_HANDLE TEEAPI TeeGetDeviceHandle(IN PTEEHANDLE handle)
{
	struct METEE_WIN_IMPL *impl_handle = to_int(handle);

	FUNC_ENTRY();
	if (NULL == impl_handle) {
		FUNC_EXIT(TEE_INVALID_PARAMETER);
		return TEE_INVALID_DEVICE_HANDLE;
	}
	FUNC_EXIT(TEE_SUCCESS);
	return impl_handle->handle;
}

#pragma pack(1)
//HECI_VERSION_V3
struct HECI_VERSION
{
	uint16_t major;
	uint16_t minor;
	uint16_t hotfix;
	uint16_t build;
};
#pragma pack()

TEESTATUS TEEAPI GetDriverVersion(IN PTEEHANDLE handle, IN OUT teeDriverVersion_t *driverVersion)
{
	struct METEE_WIN_IMPL *impl_handle = to_int(handle);
	TEESTATUS status = INIT_STATUS;
	DWORD bytesReturned = 0;
	struct HECI_VERSION ver;

	FUNC_ENTRY();

	if (NULL == impl_handle || NULL == driverVersion) {
		status = TEE_INVALID_PARAMETER;
		ERRPRINT("One of the parameters was illegal");
		goto Cleanup;
	}

	status = SendIOCTL(impl_handle->handle, (DWORD)IOCTL_HECI_GET_VERSION,
						NULL, 0,
						&ver, sizeof(ver),
						&bytesReturned);
	if (status) {
		DWORD err = GetLastError();
		status = Win32ErrorToTee(err);
		ERRPRINT("Error in SendIOCTL, error: %lu\n", err);
		goto Cleanup;
	}

	driverVersion->major = ver.major;
	driverVersion->minor = ver.minor;
	driverVersion->hotfix = ver.hotfix;
	driverVersion->build = ver.build;

	status = TEE_SUCCESS;

Cleanup:
	FUNC_EXIT(status);
	return status;
}
