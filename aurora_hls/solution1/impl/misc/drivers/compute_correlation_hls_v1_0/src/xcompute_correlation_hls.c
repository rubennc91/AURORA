// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2023.2.1 (64-bit)
// Tool Version Limit: 2023.12
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2023 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
/***************************** Include Files *********************************/
#include "xcompute_correlation_hls.h"

/************************** Function Implementation *************************/
#ifndef __linux__
int XCompute_correlation_hls_CfgInitialize(XCompute_correlation_hls *InstancePtr, XCompute_correlation_hls_Config *ConfigPtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(ConfigPtr != NULL);

    InstancePtr->Control_BaseAddress = ConfigPtr->Control_BaseAddress;
    InstancePtr->IsReady = XIL_COMPONENT_IS_READY;

    return XST_SUCCESS;
}
#endif

void XCompute_correlation_hls_Start(XCompute_correlation_hls *InstancePtr) {
    u32 Data;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_AP_CTRL) & 0x80;
    XCompute_correlation_hls_WriteReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_AP_CTRL, Data | 0x01);
}

u32 XCompute_correlation_hls_IsDone(XCompute_correlation_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_AP_CTRL);
    return (Data >> 1) & 0x1;
}

u32 XCompute_correlation_hls_IsIdle(XCompute_correlation_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_AP_CTRL);
    return (Data >> 2) & 0x1;
}

u32 XCompute_correlation_hls_IsReady(XCompute_correlation_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_AP_CTRL);
    // check ap_start to see if the pcore is ready for next input
    return !(Data & 0x1);
}

void XCompute_correlation_hls_EnableAutoRestart(XCompute_correlation_hls *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XCompute_correlation_hls_WriteReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_AP_CTRL, 0x80);
}

void XCompute_correlation_hls_DisableAutoRestart(XCompute_correlation_hls *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XCompute_correlation_hls_WriteReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_AP_CTRL, 0);
}

void XCompute_correlation_hls_Set_alice_tags(XCompute_correlation_hls *InstancePtr, u64 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XCompute_correlation_hls_WriteReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_ALICE_TAGS_DATA, (u32)(Data));
    XCompute_correlation_hls_WriteReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_ALICE_TAGS_DATA + 4, (u32)(Data >> 32));
}

u64 XCompute_correlation_hls_Get_alice_tags(XCompute_correlation_hls *InstancePtr) {
    u64 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_ALICE_TAGS_DATA);
    Data += (u64)XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_ALICE_TAGS_DATA + 4) << 32;
    return Data;
}

void XCompute_correlation_hls_Set_bob_tags(XCompute_correlation_hls *InstancePtr, u64 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XCompute_correlation_hls_WriteReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_BOB_TAGS_DATA, (u32)(Data));
    XCompute_correlation_hls_WriteReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_BOB_TAGS_DATA + 4, (u32)(Data >> 32));
}

u64 XCompute_correlation_hls_Get_bob_tags(XCompute_correlation_hls *InstancePtr) {
    u64 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_BOB_TAGS_DATA);
    Data += (u64)XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_BOB_TAGS_DATA + 4) << 32;
    return Data;
}

void XCompute_correlation_hls_Set_size_a(XCompute_correlation_hls *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XCompute_correlation_hls_WriteReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_SIZE_A_DATA, Data);
}

u32 XCompute_correlation_hls_Get_size_a(XCompute_correlation_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_SIZE_A_DATA);
    return Data;
}

void XCompute_correlation_hls_Set_size_b(XCompute_correlation_hls *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XCompute_correlation_hls_WriteReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_SIZE_B_DATA, Data);
}

u32 XCompute_correlation_hls_Get_size_b(XCompute_correlation_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_SIZE_B_DATA);
    return Data;
}

void XCompute_correlation_hls_Set_window(XCompute_correlation_hls *InstancePtr, u64 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XCompute_correlation_hls_WriteReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_WINDOW_DATA, (u32)(Data));
    XCompute_correlation_hls_WriteReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_WINDOW_DATA + 4, (u32)(Data >> 32));
}

u64 XCompute_correlation_hls_Get_window(XCompute_correlation_hls *InstancePtr) {
    u64 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_WINDOW_DATA);
    Data += (u64)XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_WINDOW_DATA + 4) << 32;
    return Data;
}

void XCompute_correlation_hls_Set_result_i(XCompute_correlation_hls *InstancePtr, XCompute_correlation_hls_Result_i Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XCompute_correlation_hls_WriteReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_RESULT_I_DATA + 0, Data.word_0);
    XCompute_correlation_hls_WriteReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_RESULT_I_DATA + 4, Data.word_1);
    XCompute_correlation_hls_WriteReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_RESULT_I_DATA + 8, Data.word_2);
    XCompute_correlation_hls_WriteReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_RESULT_I_DATA + 12, Data.word_3);
}

XCompute_correlation_hls_Result_i XCompute_correlation_hls_Get_result_i(XCompute_correlation_hls *InstancePtr) {
    XCompute_correlation_hls_Result_i Data;

    Data.word_0 = XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_RESULT_I_DATA + 0);
    Data.word_1 = XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_RESULT_I_DATA + 4);
    Data.word_2 = XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_RESULT_I_DATA + 8);
    Data.word_3 = XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_RESULT_I_DATA + 12);
    return Data;
}

XCompute_correlation_hls_Result_o XCompute_correlation_hls_Get_result_o(XCompute_correlation_hls *InstancePtr) {
    XCompute_correlation_hls_Result_o Data;

    Data.word_0 = XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_RESULT_O_DATA + 0);
    Data.word_1 = XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_RESULT_O_DATA + 4);
    Data.word_2 = XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_RESULT_O_DATA + 8);
    Data.word_3 = XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_RESULT_O_DATA + 12);
    return Data;
}

u32 XCompute_correlation_hls_Get_result_o_vld(XCompute_correlation_hls *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_RESULT_O_CTRL);
    return Data & 0x1;
}

void XCompute_correlation_hls_InterruptGlobalEnable(XCompute_correlation_hls *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XCompute_correlation_hls_WriteReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_GIE, 1);
}

void XCompute_correlation_hls_InterruptGlobalDisable(XCompute_correlation_hls *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XCompute_correlation_hls_WriteReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_GIE, 0);
}

void XCompute_correlation_hls_InterruptEnable(XCompute_correlation_hls *InstancePtr, u32 Mask) {
    u32 Register;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Register =  XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_IER);
    XCompute_correlation_hls_WriteReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_IER, Register | Mask);
}

void XCompute_correlation_hls_InterruptDisable(XCompute_correlation_hls *InstancePtr, u32 Mask) {
    u32 Register;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Register =  XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_IER);
    XCompute_correlation_hls_WriteReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_IER, Register & (~Mask));
}

void XCompute_correlation_hls_InterruptClear(XCompute_correlation_hls *InstancePtr, u32 Mask) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XCompute_correlation_hls_WriteReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_ISR, Mask);
}

u32 XCompute_correlation_hls_InterruptGetEnabled(XCompute_correlation_hls *InstancePtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    return XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_IER);
}

u32 XCompute_correlation_hls_InterruptGetStatus(XCompute_correlation_hls *InstancePtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    return XCompute_correlation_hls_ReadReg(InstancePtr->Control_BaseAddress, XCOMPUTE_CORRELATION_HLS_CONTROL_ADDR_ISR);
}

