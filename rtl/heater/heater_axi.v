// =============================================================================
// AURORA - Calefactor de la PL para las pruebas de temperatura del TRNG
// =============================================================================
// N_BANKS bancos de RO_PER_BANK osciladores en anillo de 3 etapas (trng_ro).
// Cada oscilador conmuta a cientos de MHz y disipa potencia: encendiendo
// bancos se sube la temperatura del chip sin tocar la placa (acceso remoto).
//
// Seguridad:
//   * Watchdog en hardware: si el software no escribe CTRL en WDOG ciclos
//     (por defecto 100e6 = 1 s a 100 MHz), se apagan todos los bancos y se
//     marca STATUS[0]. Un cuelgue de la aplicacion apaga el calefactor.
//   * El software (sw/baremetal/main.cpp, modo "heat") enciende los bancos de
//     uno en uno, regula a la temperatura objetivo y corta por encima del
//     limite. Ademas, el chip tiene su propia proteccion de sobretemperatura.
//   * Solo se incluye en el bitstream si se pide (make bitstream VIVADO_ARGS=heater).
//
// Registros (offsets en bytes):
//   0x00 CTRL      [N_BANKS-1:0] bancos encendidos. Cada escritura reinicia el watchdog.
//   0x04 STATUS    [0] watchdog disparado (se borra al escribir CTRL)
//   0x08 WDOG      ciclos sin escribir CTRL antes de apagar (0 = 100e6)
//   0x0C ID        0x48454154 ("HEAT")
//   0x10 CONFIG    {RO_PER_BANK[15:0], N_BANKS[7:0], 8'd3}
//   0x14 ACTIVITY  XOR de las salidas de cada banco, muestreado: cambia si oscila
// =============================================================================
`timescale 1ps / 1ps

module heater_axi #(
    parameter integer N_BANKS = 32,
    parameter integer RO_PER_BANK = 512
) (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 clk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_axi, ASSOCIATED_RESET resetn" *)
    input  wire        clk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 resetn RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire        resetn,
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

    reg [N_BANKS-1:0] mask;
    reg        tripped;
    reg [31:0] wdog_cycles, wdog_cnt;
    wire [31:0] wdog_limit = (wdog_cycles == 32'd0) ? 32'd100_000_000 : wdog_cycles;

    wire [31:0] mask32 = mask;       // extension a 32 bits (N_BANKS <= 32)
    wire [31:0] act32;

    // -------------------------------------------------------- osciladores ---
    wire [N_BANKS-1:0] bank_xor;
    genvar b, i;
    generate
        for (b = 0; b < N_BANKS; b = b + 1) begin : g_bank
            wire [RO_PER_BANK-1:0] osc;
            for (i = 0; i < RO_PER_BANK; i = i + 1) begin : g_ro
                trng_ro #(
                    .STAGES(3),
                    .SIM_HALF_PS(900 + 37 * (i % 11)),
                    .SIM_JITTER_PS(20),
                    .SIM_SEED(1000 + 97 * b + i)
                ) u_ro (
                    .en(mask[b]),
                    .osc(osc[i])
                );
            end
            assign bank_xor[b] = ^osc;
        end
    endgenerate

    // Muestreo de la actividad (sincronizador de 2 FF; no es un camino de timing).
    (* ASYNC_REG = "TRUE" *) reg [N_BANKS-1:0] hs1, hs2;
    always @(posedge clk) begin
        hs1 <= bank_xor;
        hs2 <= hs1;
    end
    assign act32 = hs2;

    // ------------------------------------------------------------ watchdog --
    wire wr_hs = s_axi_awvalid && s_axi_awready && s_axi_wvalid && s_axi_wready;
    wire ctrl_write = wr_hs && (s_axi_awaddr[7:2] == 6'h00);

    always @(posedge clk) begin
        if (!resetn) begin
            mask <= {N_BANKS{1'b0}};
            tripped <= 1'b0;
            wdog_cnt <= 32'd0;
        end else if (ctrl_write) begin
            mask <= s_axi_wdata[N_BANKS-1:0];
            tripped <= 1'b0;
            wdog_cnt <= 32'd0;
        end else if (mask != {N_BANKS{1'b0}}) begin
            if (wdog_cnt + 32'd1 >= wdog_limit) begin
                mask <= {N_BANKS{1'b0}};
                tripped <= 1'b1;
                wdog_cnt <= 32'd0;
            end else begin
                wdog_cnt <= wdog_cnt + 32'd1;
            end
        end else begin
            wdog_cnt <= 32'd0;
        end
    end

    // ----------------------------------------------------------- AXI write ---
    always @(posedge clk) begin
        if (!resetn) begin
            s_axi_awready <= 1'b0; s_axi_wready <= 1'b0; s_axi_bvalid <= 1'b0;
            wdog_cycles <= 32'd0;
        end else begin
            s_axi_awready <= 1'b0;
            s_axi_wready <= 1'b0;
            if (!s_axi_awready && s_axi_awvalid && s_axi_wvalid && !s_axi_bvalid) begin
                s_axi_awready <= 1'b1;
                s_axi_wready <= 1'b1;
            end
            if (wr_hs) begin
                s_axi_bvalid <= 1'b1;
                if (s_axi_awaddr[7:2] == 6'h02) wdog_cycles <= s_axi_wdata;
            end else if (s_axi_bvalid && s_axi_bready) begin
                s_axi_bvalid <= 1'b0;
            end
        end
    end

    // ------------------------------------------------------------ AXI read ---
    wire rd_hs = s_axi_arvalid && s_axi_arready;
    always @(posedge clk) begin
        if (!resetn) begin
            s_axi_arready <= 1'b0; s_axi_rvalid <= 1'b0; s_axi_rdata <= 32'd0;
        end else begin
            s_axi_arready <= 1'b0;
            if (!s_axi_arready && s_axi_arvalid && !s_axi_rvalid) s_axi_arready <= 1'b1;
            if (rd_hs) begin
                s_axi_rvalid <= 1'b1;
                case (s_axi_araddr[7:2])
                    6'h00: s_axi_rdata <= mask32;
                    6'h01: s_axi_rdata <= {31'd0, tripped};
                    6'h02: s_axi_rdata <= wdog_limit;
                    6'h03: s_axi_rdata <= 32'h48454154;
                    6'h04: s_axi_rdata <= {RO_PER_BANK[15:0], N_BANKS[7:0], 8'd3};
                    6'h05: s_axi_rdata <= act32;
                    default: s_axi_rdata <= 32'd0;
                endcase
            end else if (s_axi_rvalid && s_axi_rready) begin
                s_axi_rvalid <= 1'b0;
            end
        end
    end

    wire _unused = &{1'b0, s_axi_wstrb};
endmodule
