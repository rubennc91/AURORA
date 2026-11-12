// =============================================================================
// AURORA TRNG - Oscilador en anillo
// =============================================================================
// Sintesis: una NAND (habilitacion) + (STAGES-1) inversores en LUTs, en bucle.
//   STAGES debe ser impar. Las LUTs se protegen con DONT_TOUCH y el bucle
//   combinacional se permite en el XDC (ver trng.xdc).
// Simulacion (`define SIMULATION): modelo de comportamiento con semiperiodo
//   SIM_HALF_PS y jitter gaussiano de desviacion SIM_JITTER_PS por flanco.
//   El modelo solo sirve para verificar la logica digital: la entropia REAL
//   solo puede medirse con capturas de la placa.
// =============================================================================
`timescale 1ps / 1ps

module trng_ro #(
    parameter integer STAGES        = 3,
    parameter integer SIM_HALF_PS   = 1300,
    parameter integer SIM_JITTER_PS = 40,
    parameter integer SIM_SEED      = 1
) (
    input  wire en,
    output wire osc
);
`ifdef SIMULATION
    reg     r = 1'b0;
    integer seed = SIM_SEED;
    integer d;
    assign osc = r;
    always begin
        if (en) begin
            d = SIM_HALF_PS + $dist_normal(seed, 0, SIM_JITTER_PS);
            if (d < 1) d = 1;
            #(d) r = ~r;
        end else begin
            r = 1'b0;
            @(en);
        end
    end
`else
    (* DONT_TOUCH = "true", ALLOW_COMBINATORIAL_LOOPS = "true" *) wire [STAGES-1:0] chain;

    // Primera etapa: NAND con la habilitacion (O = ~(I0 & I1)).
    (* DONT_TOUCH = "true" *)
    LUT2 #(.INIT(4'b0111)) u_nand (.O(chain[0]), .I0(chain[STAGES-1]), .I1(en));

    genvar i;
    generate
        for (i = 1; i < STAGES; i = i + 1) begin : g_inv
            (* DONT_TOUCH = "true" *)
            LUT1 #(.INIT(2'b01)) u_inv (.O(chain[i]), .I0(chain[i-1]));
        end
    endgenerate

    assign osc = chain[STAGES-1];
`endif
endmodule
