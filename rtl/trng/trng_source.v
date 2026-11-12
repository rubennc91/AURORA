// =============================================================================
// AURORA TRNG - Fuente de entropia: N osciladores en anillo XOR + muestreo
// =============================================================================
// Las salidas de los osciladores se combinan con XOR y se muestrean con el
// reloj del sistema a traves de un sincronizador de 2 flip-flops. Se entrega
// un bit cada 'decim' ciclos para que se acumule jitter entre muestras.
//
// fault_sel (inyeccion de fallos para autodiagnostico de los tests de salud):
//   0: normal
//   1: salida bloqueada a 0
//   2: salida sesgada (AND de tres muestras consecutivas, P(1) ~ 0.125)
//   3: osciladores apagados (fallo fisico realista)
// =============================================================================
`timescale 1ps / 1ps

module trng_source #(
    parameter integer N_RO = 4
) (
    input  wire        clk,
    input  wire        rst,
    input  wire        en,
    input  wire [15:0] decim,
    input  wire [1:0]  fault_sel,
    output reg         bit_out,
    output reg         valid
);
    wire ro_en = en && (fault_sel != 2'd3);
    wire [N_RO-1:0] osc;

    genvar i;
    generate
        for (i = 0; i < N_RO; i = i + 1) begin : g_ro
            // Longitudes distintas (3, 5, 7, ...) => frecuencias no conmensurables.
            trng_ro #(
                .STAGES(3 + 2 * i),
                .SIM_HALF_PS(1300 + 237 * i),
                .SIM_JITTER_PS(40 + 5 * i),
                .SIM_SEED(17 + 31 * i)
            ) u_ro (
                .en(ro_en),
                .osc(osc[i])
            );
        end
    endgenerate

    wire mix = ^osc;

    (* ASYNC_REG = "TRUE" *) reg s1, s2;
    reg [15:0] cnt;
    reg        prev, prev2;

    always @(posedge clk) begin
        if (rst) begin
            s1 <= 1'b0; s2 <= 1'b0;
            cnt <= 16'd0; valid <= 1'b0; bit_out <= 1'b0; prev <= 1'b0; prev2 <= 1'b0;
        end else begin
            s1 <= mix;
            s2 <= s1;
            valid <= 1'b0;
            if (en) begin
                if (cnt + 16'd1 >= ((decim == 16'd0) ? 16'd1 : decim)) begin
                    cnt   <= 16'd0;
                    valid <= 1'b1;
                    prev  <= s2;
                    prev2 <= prev;
                    case (fault_sel)
                        2'd1:    bit_out <= 1'b0;
                        2'd2:    bit_out <= s2 & prev & prev2;
                        default: bit_out <= s2;
                    endcase
                end else begin
                    cnt <= cnt + 16'd1;
                end
            end else begin
                cnt <= 16'd0;
            end
        end
    end
endmodule
