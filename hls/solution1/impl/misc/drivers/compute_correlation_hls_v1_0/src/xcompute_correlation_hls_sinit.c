// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2023.2.1 (64-bit)
// Tool Version Limit: 2023.12
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2023 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
#ifndef __linux__

#include "xstatus.h"
#ifdef SDT
#include "xparameters.h"
#endif
#include "xcompute_correlation_hls.h"

extern XCompute_correlation_hls_Config XCompute_correlation_hls_ConfigTable[];

#ifdef SDT
XCompute_correlation_hls_Config *XCompute_correlation_hls_LookupConfig(UINTPTR BaseAddress) {
	XCompute_correlation_hls_Config *ConfigPtr = NULL;

	int Index;

	for (Index = (u32)0x0; XCompute_correlation_hls_ConfigTable[Index].Name != NULL; Index++) {
		if (!BaseAddress || XCompute_correlation_hls_ConfigTable[Index].Control_BaseAddress == BaseAddress) {
			ConfigPtr = &XCompute_correlation_hls_ConfigTable[Index];
			break;
		}
	}

	return ConfigPtr;
}

int XCompute_correlation_hls_Initialize(XCompute_correlation_hls *InstancePtr, UINTPTR BaseAddress) {
	XCompute_correlation_hls_Config *ConfigPtr;

	Xil_AssertNonvoid(InstancePtr != NULL);

	ConfigPtr = XCompute_correlation_hls_LookupConfig(BaseAddress);
	if (ConfigPtr == NULL) {
		InstancePtr->IsReady = 0;
		return (XST_DEVICE_NOT_FOUND);
	}

	return XCompute_correlation_hls_CfgInitialize(InstancePtr, ConfigPtr);
}
#else
XCompute_correlation_hls_Config *XCompute_correlation_hls_LookupConfig(u16 DeviceId) {
	XCompute_correlation_hls_Config *ConfigPtr = NULL;

	int Index;

	for (Index = 0; Index < XPAR_XCOMPUTE_CORRELATION_HLS_NUM_INSTANCES; Index++) {
		if (XCompute_correlation_hls_ConfigTable[Index].DeviceId == DeviceId) {
			ConfigPtr = &XCompute_correlation_hls_ConfigTable[Index];
			break;
		}
	}

	return ConfigPtr;
}

int XCompute_correlation_hls_Initialize(XCompute_correlation_hls *InstancePtr, u16 DeviceId) {
	XCompute_correlation_hls_Config *ConfigPtr;

	Xil_AssertNonvoid(InstancePtr != NULL);

	ConfigPtr = XCompute_correlation_hls_LookupConfig(DeviceId);
	if (ConfigPtr == NULL) {
		InstancePtr->IsReady = 0;
		return (XST_DEVICE_NOT_FOUND);
	}

	return XCompute_correlation_hls_CfgInitialize(InstancePtr, ConfigPtr);
}
#endif

#endif

