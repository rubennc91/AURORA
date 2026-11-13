// =============================================================================
// AURORA TRNG - Top con interfaz AXI4-Lite (para el bloque PS del Zynq)
// =============================================================================
// Mapa de registros (offsets en bytes):
//   0x00 CTRL     [0] enable  [1] clear (autoborrado)  [5:4] fault_sel
//                 [8] gate_bypass: deja pasar bits aunque haya alarma o no haya
//                     terminado el arranque (SOLO para capturas de evaluacion de
//                     entropia; nunca en produccion)
//   0x04 STATUS   [0] startup_done [1] rct_alarm [2] apt_alarm [3] fifo_empty
//                 [4] fifo_full [31:16] palabras en la FIFO
//   0x08 DATA     lectura: saca una palabra de 32 bits de la FIFO (0 si vacia)
//   0x0C DECIM    ciclos de reloj entre muestras (por defecto 64)
//   0x10 RCT_CUT  umbral del Repetition Count Test   (por defecto 41)
//   0x14 APT_CUT  umbral del Adaptive Proportion Test (por defecto 793)
//   0x18 SAMPLES  muestras brutas generadas desde el ultimo clear
//   0x1C DROPPED  palabras perdidas por FIFO llena
//   0x20 ID       0x54524E47 ("TRNG")
//   0x24 RCT_IDX  indice de la muestra que disparo el RCT (0xFFFFFFFF si ninguna)
//   0x28 APT_IDX  indice de la muestra que disparo el APT
//
// Los umbrales por defecto corresponden a H_min = 0.5 bits/muestra y una
// probabilidad de falsa alarma alpha = 2^-20 (ver sw/aurora_trng.h).
// =============================================================================
`timescale 1ps / 1ps

module trng_axi #(
    parameter integer N_RO = 4,
    parameter integer FIFO_DEPTH_LOG2 = 8
) (
    // Anotaciones para que Vivado asocie reloj/reset al bus s_axi (block design).
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 clk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_axi, ASSOCIATED_RESET resetn" *)
    input  wire        clk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 resetn RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire        resetn,
    // AXI4-Lite esclavo
    input  wire [7:0]  s_axi_awaddr,
    input  wire        s_axi_awvalid,
    output reg         s_axi_awready,
    input  wire [31:0] s_axi_wdata,
    input  wire [3:0]  s_axi_wstrb,
    input  wire        s_axi_wvalid,
    output reg         s_axi_wready,
    output wire [1:0]  s_axi_bresp,
    output reg         s_axi_bvalid,
    input  wire        s_axi_bready,
    input  wire [7:0]  s_axi_araddr,
    input  wire        s_axi_arvalid,
    output reg         s_axi_arready,
    output reg  [31:0] s_axi_rdata,
    output wire [1:0]  s_axi_rresp,
    output reg         s_axi_rvalid,
    input  wire        s_axi_rready
);
    assign s_axi_bresp = 2'b00;
    assign s_axi_rresp = 2'b00;

    // ------------------------------------------------------------ registros --
    reg        enable, gate_bypass, clear_pulse;
    reg [1:0]  fault_sel;
    reg [15:0] decim, rct_cut, apt_cut;
    reg [31:0] dropped;

    wire rst = !resetn || clear_pulse;

    // ------------------------------------------------------------- fuente ----
    wire raw_bit, raw_valid;
    trng_source #(.N_RO(N_RO)) u_src (
        .clk(clk), .rst(rst), .en(enable), .decim(decim), .fault_sel(fault_sel),
        .bit_out(raw_bit), .valid(raw_valid)
    );

    wire rct_alarm, apt_alarm, startup_done;
    wire [31:0] samples, rct_idx, apt_idx;
    trng_health u_health (
        .clk(clk), .rst(rst), .bit_in(raw_bit), .valid(raw_valid),
        .rct_cutoff(rct_cut), .apt_cutoff(apt_cut),
        .rct_alarm(rct_alarm), .apt_alarm(apt_alarm), .startup_done(startup_done),
        .samples(samples), .rct_fail_idx(rct_idx), .apt_fail_idx(apt_idx)
    );

    // Los bits solo pasan con el arranque superado y sin alarmas (salvo bypass).
    // Se evalua con el estado de salud ANTERIOR a esta muestra, y la muestra que
    // dispara una alarma tampoco pasa.
    wire healthy = startup_done && !rct_alarm && !apt_alarm;
    wire accept  = raw_valid && (healthy || gate_bypass);

    // ------------------------------------------------- empaquetado + FIFO ----
    localparam integer DEPTH = 1 << FIFO_DEPTH_LOG2;
    reg [31:0] fifo_mem [0:DEPTH-1];
    reg [FIFO_DEPTH_LOG2:0] wr_ptr, rd_ptr;
    wire [FIFO_DEPTH_LOG2:0] level = wr_ptr - rd_ptr;
    wire fifo_empty = (level == 0);
    wire fifo_full  = (level == DEPTH);

    reg [31:0] shreg;
    reg [4:0]  nbits;
    reg        pop;

    always @(posedge clk) begin
        if (rst) begin
            shreg <= 32'd0; nbits <= 5'd0; wr_ptr <= 0; dropped <= 32'd0;
        end else if (accept) begin
            shreg <= {shreg[30:0], raw_bit};
            nbits <= nbits + 5'd1;
            if (nbits == 5'd31) begin
                if (!fifo_full) begin
                    fifo_mem[wr_ptr[FIFO_DEPTH_LOG2-1:0]] <= {shreg[30:0], raw_bit};
                    wr_ptr <= wr_ptr + 1'b1;
                end else begin
                    dropped <= dropped + 32'd1;
                end
            end
        end
    end

    always @(posedge clk) begin
        if (rst) rd_ptr <= 0;
        else if (pop && !fifo_empty) rd_ptr <= rd_ptr + 1'b1;
    end

    // ---------------------------------------------------------- AXI write ----
    wire wr_hs = s_axi_awvalid && s_axi_awready && s_axi_wvalid && s_axi_wready;

    always @(posedge clk) begin
        if (!resetn) begin
            s_axi_awready <= 1'b0; s_axi_wready <= 1'b0; s_axi_bvalid <= 1'b0;
            enable <= 1'b0; gate_bypass <= 1'b0; fault_sel <= 2'd0; clear_pulse <= 1'b0;
            decim <= 16'd64; rct_cut <= 16'd41; apt_cut <= 16'd793;
        end else begin
            clear_pulse <= 1'b0;
            s_axi_awready <= 1'b0;
            s_axi_wready <= 1'b0;
            if (!s_axi_awready && s_axi_awvalid && s_axi_wvalid && !s_axi_bvalid) begin
                s_axi_awready <= 1'b1;
                s_axi_wready <= 1'b1;
            end
            if (wr_hs) begin
                s_axi_bvalid <= 1'b1;
                case (s_axi_awaddr[7:2])
                    6'h00: begin
                        enable      <= s_axi_wdata[0];
                        clear_pulse <= s_axi_wdata[1];
                        fault_sel   <= s_axi_wdata[5:4];
                        gate_bypass <= s_axi_wdata[8];
                    end
                    6'h03: decim   <= s_axi_wdata[15:0];
                    6'h04: rct_cut <= s_axi_wdata[15:0];
                    6'h05: apt_cut <= s_axi_wdata[15:0];
                    default: ;
                endcase
            end else if (s_axi_bvalid && s_axi_bready) begin
                s_axi_bvalid <= 1'b0;
            end
        end
    end

    // ----------------------------------------------------------- AXI read ----
    wire rd_hs = s_axi_arvalid && s_axi_arready;

    always @(posedge clk) begin
        if (!resetn) begin
            s_axi_arready <= 1'b0; s_axi_rvalid <= 1'b0; s_axi_rdata <= 32'd0; pop <= 1'b0;
        end else begin
            pop <= 1'b0;
            s_axi_arready <= 1'b0;
            if (!s_axi_arready && s_axi_arvalid && !s_axi_rvalid) s_axi_arready <= 1'b1;
            if (rd_hs) begin
                s_axi_rvalid <= 1'b1;
                case (s_axi_araddr[7:2])
                    6'h00: s_axi_rdata <= {23'd0, gate_bypass, 2'd0, fault_sel, 3'd0, enable};
                    6'h01: s_axi_rdata <= {{(15 - FIFO_DEPTH_LOG2){1'b0}}, level, 11'd0,
                                           fifo_full, fifo_empty, apt_alarm, rct_alarm, startup_done};
                    6'h02: begin
                        s_axi_rdata <= fifo_empty ? 32'd0 : fifo_mem[rd_ptr[FIFO_DEPTH_LOG2-1:0]];
                        pop <= 1'b1;
                    end
                    6'h03: s_axi_rdata <= {16'd0, decim};
                    6'h04: s_axi_rdata <= {16'd0, rct_cut};
                    6'h05: s_axi_rdata <= {16'd0, apt_cut};
                    6'h06: s_axi_rdata <= samples;
                    6'h07: s_axi_rdata <= dropped;
                    6'h08: s_axi_rdata <= 32'h54524E47;
                    6'h09: s_axi_rdata <= rct_idx;
                    6'h0A: s_axi_rdata <= apt_idx;
                    default: s_axi_rdata <= 32'd0;
                endcase
            end else if (s_axi_rvalid && s_axi_rready) begin
                s_axi_rvalid <= 1'b0;
            end
        end
    end

    // wstrb no se usa: todos los registros se escriben como palabra completa.
    wire _unused = &{1'b0, s_axi_wstrb};
endmodule
