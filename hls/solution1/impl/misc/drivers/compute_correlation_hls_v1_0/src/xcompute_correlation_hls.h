// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2023.2.1 (64-bit)
// Tool Version Limit: 2023.12
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2023 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
#ifndef XCOMPUTE_CORRELATION_HLS_H
#define XCOMPUTE_CORRELATION_HLS_H

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/
#ifndef __linux__
#include "xil_types.h"
#include "xil_assert.h"
#include "xstatus.h"
#include "xil_io.h"
#else
#include <stdint.h>
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stddef.h>
#endif
#include "xcompute_correlation_hls_hw.h"

/**************************** Type Definitions ******************************/
#ifdef __linux__
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#else
typedef struct {
#ifdef SDT
    char *Name;
#else
    u16 DeviceId;
#endif
    u64 Control_BaseAddress;
} XCompute_correlation_hls_Config;
#endif

typedef struct {
    u64 Control_BaseAddress;
    u32 IsReady;
} XCompute_correlation_hls;

typedef u32 word_type;

typedef struct {
    u32 word_0;
    u32 word_1;
    u32 word_2;
    u32 word_3;
} XCompute_correlation_hls_Result_i;

typedef struct {
    u32 word_0;
    u32 word_1;
    u32 word_2;
    u32 word_3;
} XCompute_correlation_hls_Result_o;

/***************** Macros (Inline Functions) Definitions *********************/
#ifndef __linux__
#define XCompute_correlation_hls_WriteReg(BaseAddress, RegOffset, Data) \
    Xil_Out32((BaseAddress) + (RegOffset), (u32)(Data))
#define XCompute_correlation_hls_ReadReg(BaseAddress, RegOffset) \
    Xil_In32((BaseAddress) + (RegOffset))
#else
#define XCompute_correlation_hls_WriteReg(BaseAddress, RegOffset, Data) \
    *(volatile u32*)((BaseAddress) + (RegOffset)) = (u32)(Data)
#define XCompute_correlation_hls_ReadReg(BaseAddress, RegOffset) \
    *(volatile u32*)((BaseAddress) + (RegOffset))

#define Xil_AssertVoid(expr)    assert(expr)
#define Xil_AssertNonvoid(expr) assert(expr)

#define XST_SUCCESS             0
#define XST_DEVICE_NOT_FOUND    2
#define XST_OPEN_DEVICE_FAILED  3
#define XIL_COMPONENT_IS_READY  1
#endif

/************************** Function Prototypes *****************************/
#ifndef __linux__
#ifdef SDT
int XCompute_correlation_hls_Initialize(XCompute_correlation_hls *InstancePtr, UINTPTR BaseAddress);
XCompute_correlation_hls_Config* XCompute_correlation_hls_LookupConfig(UINTPTR BaseAddress);
#else
int XCompute_correlation_hls_Initialize(XCompute_correlation_hls *InstancePtr, u16 DeviceId);
XCompute_correlation_hls_Config* XCompute_correlation_hls_LookupConfig(u16 DeviceId);
#endif
int XCompute_correlation_hls_CfgInitialize(XCompute_correlation_hls *InstancePtr, XCompute_correlation_hls_Config *ConfigPtr);
#else
int XCompute_correlation_hls_Initialize(XCompute_correlation_hls *InstancePtr, const char* InstanceName);
int XCompute_correlation_hls_Release(XCompute_correlation_hls *InstancePtr);
#endif

void XCompute_correlation_hls_Start(XCompute_correlation_hls *InstancePtr);
u32 XCompute_correlation_hls_IsDone(XCompute_correlation_hls *InstancePtr);
u32 XCompute_correlation_hls_IsIdle(XCompute_correlation_hls *InstancePtr);
u32 XCompute_correlation_hls_IsReady(XCompute_correlation_hls *InstancePtr);
void XCompute_correlation_hls_EnableAutoRestart(XCompute_correlation_hls *InstancePtr);
void XCompute_correlation_hls_DisableAutoRestart(XCompute_correlation_hls *InstancePtr);

void XCompute_correlation_hls_Set_alice_tags(XCompute_correlation_hls *InstancePtr, u64 Data);
u64 XCompute_correlation_hls_Get_alice_tags(XCompute_correlation_hls *InstancePtr);
void XCompute_correlation_hls_Set_bob_tags(XCompute_correlation_hls *InstancePtr, u64 Data);
u64 XCompute_correlation_hls_Get_bob_tags(XCompute_correlation_hls *InstancePtr);
void XCompute_correlation_hls_Set_size_a(XCompute_correlation_hls *InstancePtr, u32 Data);
u32 XCompute_correlation_hls_Get_size_a(XCompute_correlation_hls *InstancePtr);
void XCompute_correlation_hls_Set_size_b(XCompute_correlation_hls *InstancePtr, u32 Data);
u32 XCompute_correlation_hls_Get_size_b(XCompute_correlation_hls *InstancePtr);
void XCompute_correlation_hls_Set_window(XCompute_correlation_hls *InstancePtr, u64 Data);
u64 XCompute_correlation_hls_Get_window(XCompute_correlation_hls *InstancePtr);
void XCompute_correlation_hls_Set_result_i(XCompute_correlation_hls *InstancePtr, XCompute_correlation_hls_Result_i Data);
XCompute_correlation_hls_Result_i XCompute_correlation_hls_Get_result_i(XCompute_correlation_hls *InstancePtr);
XCompute_correlation_hls_Result_o XCompute_correlation_hls_Get_result_o(XCompute_correlation_hls *InstancePtr);
u32 XCompute_correlation_hls_Get_result_o_vld(XCompute_correlation_hls *InstancePtr);

void XCompute_correlation_hls_InterruptGlobalEnable(XCompute_correlation_hls *InstancePtr);
void XCompute_correlation_hls_InterruptGlobalDisable(XCompute_correlation_hls *InstancePtr);
void XCompute_correlation_hls_InterruptEnable(XCompute_correlation_hls *InstancePtr, u32 Mask);
void XCompute_correlation_hls_InterruptDisable(XCompute_correlation_hls *InstancePtr, u32 Mask);
void XCompute_correlation_hls_InterruptClear(XCompute_correlation_hls *InstancePtr, u32 Mask);
u32 XCompute_correlation_hls_InterruptGetEnabled(XCompute_correlation_hls *InstancePtr);
u32 XCompute_correlation_hls_InterruptGetStatus(XCompute_correlation_hls *InstancePtr);

#ifdef __cplusplus
}
#endif

#endif
