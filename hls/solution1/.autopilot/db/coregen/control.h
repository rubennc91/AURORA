// 0x00 : Control signals
//        bit 0  - ap_start (Read/Write/COH)
//        bit 1  - ap_done (Read/COR)
//        bit 2  - ap_idle (Read)
//        bit 3  - ap_ready (Read/COR)
//        bit 7  - auto_restart (Read/Write)
//        bit 9  - interrupt (Read)
//        others - reserved
// 0x04 : Global Interrupt Enable Register
//        bit 0  - Global Interrupt Enable (Read/Write)
//        others - reserved
// 0x08 : IP Interrupt Enable Register (Read/Write)
//        bit 0 - enable ap_done interrupt (Read/Write)
//        bit 1 - enable ap_ready interrupt (Read/Write)
//        others - reserved
// 0x0c : IP Interrupt Status Register (Read/TOW)
//        bit 0 - ap_done (Read/TOW)
//        bit 1 - ap_ready (Read/TOW)
//        others - reserved
// 0x10 : Data signal of alice_tags
//        bit 31~0 - alice_tags[31:0] (Read/Write)
// 0x14 : Data signal of alice_tags
//        bit 31~0 - alice_tags[63:32] (Read/Write)
// 0x18 : reserved
// 0x1c : Data signal of bob_tags
//        bit 31~0 - bob_tags[31:0] (Read/Write)
// 0x20 : Data signal of bob_tags
//        bit 31~0 - bob_tags[63:32] (Read/Write)
// 0x24 : reserved
// 0x28 : Data signal of size_a
//        bit 31~0 - size_a[31:0] (Read/Write)
// 0x2c : reserved
// 0x30 : Data signal of size_b
//        bit 31~0 - size_b[31:0] (Read/Write)
// 0x34 : reserved
// 0x38 : Data signal of window
//        bit 31~0 - window[31:0] (Read/Write)
// 0x3c : Data signal of window
//        bit 31~0 - window[63:32] (Read/Write)
// 0x40 : reserved
// 0x50 : Data signal of result_i
//        bit 31~0 - result_i[31:0] (Read/Write)
// 0x54 : Data signal of result_i
//        bit 31~0 - result_i[63:32] (Read/Write)
// 0x58 : Data signal of result_i
//        bit 31~0 - result_i[95:64] (Read/Write)
// 0x5c : Data signal of result_i
//        bit 31~0 - result_i[127:96] (Read/Write)
// 0x60 : reserved
// 0x64 : Data signal of result_o
//        bit 31~0 - result_o[31:0] (Read)
// 0x68 : Data signal of result_o
//        bit 31~0 - result_o[63:32] (Read)
// 0x6c : Data signal of result_o
//        bit 31~0 - result_o[95:64] (Read)
// 0x70 : Data signal of result_o
//        bit 31~0 - result_o[127:96] (Read)
// 0x74 : Control signal of result_o
//        bit 0  - result_o_ap_vld (Read/COR)
//        others - reserved
// (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

#define CONTROL_ADDR_AP_CTRL         0x00
#define CONTROL_ADDR_GIE             0x04
#define CONTROL_ADDR_IER             0x08
#define CONTROL_ADDR_ISR             0x0c
#define CONTROL_ADDR_ALICE_TAGS_DATA 0x10
#define CONTROL_BITS_ALICE_TAGS_DATA 64
#define CONTROL_ADDR_BOB_TAGS_DATA   0x1c
#define CONTROL_BITS_BOB_TAGS_DATA   64
#define CONTROL_ADDR_SIZE_A_DATA     0x28
#define CONTROL_BITS_SIZE_A_DATA     32
#define CONTROL_ADDR_SIZE_B_DATA     0x30
#define CONTROL_BITS_SIZE_B_DATA     32
#define CONTROL_ADDR_WINDOW_DATA     0x38
#define CONTROL_BITS_WINDOW_DATA     64
#define CONTROL_ADDR_RESULT_I_DATA   0x50
#define CONTROL_BITS_RESULT_I_DATA   128
#define CONTROL_ADDR_RESULT_O_DATA   0x64
#define CONTROL_BITS_RESULT_O_DATA   128
#define CONTROL_ADDR_RESULT_O_CTRL   0x74
