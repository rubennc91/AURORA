// =============================================================================
// AURORA TRNG - Tests de salud continuos NIST SP 800-90B (seccion 4.4)
// =============================================================================
// * Repetition Count Test (RCT): alarma si un mismo valor se repite
//   rct_cutoff veces seguidas.
// * Adaptive Proportion Test (APT): en ventanas de 1024 muestras, alarma si el
//   primer valor de la ventana aparece apt_cutoff veces o mas.
// * Test de arranque: startup_done se activa tras 1024 muestras sin alarmas.
// Las alarmas son persistentes hasta 'clear'. Se registra el indice de la
// muestra que disparo cada alarma (para contrastar con el modelo software).
// =============================================================================
`timescale 1ps / 1ps

module trng_health (
    input  wire        clk,
    input  wire        rst,           // reset sincrono (incluye 'clear')
    input  wire        bit_in,
    input  wire        valid,
    input  wire [15:0] rct_cutoff,
    input  wire [15:0] apt_cutoff,
    output reg         rct_alarm,
    output reg         apt_alarm,
    output reg         startup_done,
    output reg  [31:0] samples,
    output reg  [31:0] rct_fail_idx,
    output reg  [31:0] apt_fail_idx
);
    localparam integer APT_W = 1024;

    reg        rct_a;
    reg [15:0] rct_b;
    reg        apt_a;
    reg [10:0] apt_b;
    reg [9:0]  apt_i;     // posicion dentro de la ventana

    always @(posedge clk) begin
        if (rst) begin
            rct_alarm <= 1'b0; apt_alarm <= 1'b0; startup_done <= 1'b0;
            samples <= 32'd0; rct_fail_idx <= 32'hFFFFFFFF; apt_fail_idx <= 32'hFFFFFFFF;
            rct_a <= 1'b0; rct_b <= 16'd0;
            apt_a <= 1'b0; apt_b <= 11'd0; apt_i <= 10'd0;
        end else if (valid) begin
            samples <= samples + 32'd1;

            // --- RCT ---
            if (rct_b != 16'd0 && bit_in == rct_a) begin
                if (rct_b + 16'd1 >= rct_cutoff && !rct_alarm) begin
                    rct_alarm <= 1'b1;
                    rct_fail_idx <= samples;
                end
                if (rct_b != 16'hFFFF) rct_b <= rct_b + 16'd1;
            end else begin
                rct_a <= bit_in;
                rct_b <= 16'd1;
            end

            // --- APT ---
            if (apt_i == 10'd0) begin
                apt_a <= bit_in;
                apt_b <= 11'd1;
            end else if (bit_in == apt_a) begin
                if ({5'd0, apt_b} + 16'd1 >= apt_cutoff && !apt_alarm) begin
                    apt_alarm <= 1'b1;
                    apt_fail_idx <= samples;
                end
                apt_b <= apt_b + 11'd1;
            end
            apt_i <= apt_i + 10'd1;   // da la vuelta cada 1024

            // --- Arranque ---
            if (samples + 32'd1 >= APT_W && !rct_alarm && !apt_alarm) startup_done <= 1'b1;
        end
    end
endmodule
