// =============================================================================
// AURORA TRNG - Testbench (Icarus Verilog / Vivado xsim), compilar con -DSIMULATION
// =============================================================================
// Escenarios: ID, configuracion, arranque, captura normal, fallos inyectados
// (bloqueo, sesgo, osciladores apagados), recuperacion tras clear y modo bypass.
// Genera en +outdir=<dir>:
//   tb_log.txt        CHECK <nombre> PASS|FAIL ..., IDX <fase> <rct_idx> <apt_idx>,
//                     y al final TB_RESULT PASS|FAIL
//   raw_<fase>.txt    '<indice> <bit>' por muestra bruta vista por los tests de salud
//   words_normal.txt  palabras leidas de la FIFO en funcionamiento normal (hex)
// Los ficheros los analiza tests/python/test_trng_rtl.py.
// =============================================================================
`timescale 1ps / 1ps

module tb_trng;
    localparam integer CLK_HALF = 5000;      // 100 MHz
    localparam integer N_WORDS  = 1024;      // palabras capturadas en modo normal
    localparam integer DECIM    = 16;

    reg clk = 0, resetn = 0;
    always #(CLK_HALF) clk = ~clk;

    reg  [7:0]  awaddr = 0, araddr = 0;
    reg         awvalid = 0, wvalid = 0, bready = 0, arvalid = 0, rready = 0;
    reg  [31:0] wdata = 0;
    wire        awready, wready, bvalid, arready, rvalid;
    wire [31:0] rdata;
    wire [1:0]  bresp, rresp;

    trng_axi dut (
        .clk(clk), .resetn(resetn),
        .s_axi_awaddr(awaddr), .s_axi_awvalid(awvalid), .s_axi_awready(awready),
        .s_axi_wdata(wdata), .s_axi_wstrb(4'hF), .s_axi_wvalid(wvalid), .s_axi_wready(wready),
        .s_axi_bresp(bresp), .s_axi_bvalid(bvalid), .s_axi_bready(bready),
        .s_axi_araddr(araddr), .s_axi_arvalid(arvalid), .s_axi_arready(arready),
        .s_axi_rdata(rdata), .s_axi_rresp(rresp), .s_axi_rvalid(rvalid), .s_axi_rready(rready)
    );

    // ------------------------------------------------------------- ficheros --
    reg [8*256-1:0] outdir;
    integer flog, fraw, fwords;
    integer failures = 0;

    task check(input [8*48-1:0] name, input cond, input [31:0] val);
        begin
            $fdisplay(flog, "CHECK %0s %0s 0x%08h", name, cond ? "PASS" : "FAIL", val);
            if (!cond) failures = failures + 1;
        end
    endtask

    // Monitor de los bits brutos (los que ven los tests de salud).
    // Formato: <indice de muestra desde el ultimo clear> <bit>
    always @(posedge clk)
        if (fraw != 0 && dut.raw_valid && !dut.rst) $fdisplay(fraw, "%0d %0d", dut.u_health.samples, dut.raw_bit);

    reg [8*512-1:0] fname;
    task open_raw(input [8*16-1:0] phase);
        begin
            if (fraw != 0) $fclose(fraw);
            $sformat(fname, "%0s/raw_%0s.txt", outdir, phase);
            fraw = $fopen(fname, "w");
        end
    endtask

    // ---------------------------------------------------------- AXI master --
    task axi_write(input [7:0] a, input [31:0] d);
        begin
            @(negedge clk); awaddr = a; wdata = d; awvalid = 1; wvalid = 1;
            while (!awready) @(negedge clk);
            @(posedge clk); #1; awvalid = 0; wvalid = 0; bready = 1;
            while (!bvalid) @(negedge clk);
            @(posedge clk); #1; bready = 0;
        end
    endtask

    task axi_read(input [7:0] a, output [31:0] d);
        begin
            @(negedge clk); araddr = a; arvalid = 1;
            while (!arready) @(negedge clk);
            @(posedge clk); #1; arvalid = 0; rready = 1;
            while (!rvalid) @(negedge clk);
            d = rdata;
            @(posedge clk); #1; rready = 0;
        end
    endtask

    // Espera a que STATUS & mask != 0 (o timeout en muestras).
    task wait_status(input [31:0] mask, input integer max_reads, output [31:0] st);
        integer n;
        begin
            n = 0;
            axi_read(8'h04, st);
            while ((st & mask) == 0 && n < max_reads) begin
                repeat (DECIM * 8) @(posedge clk);
                axi_read(8'h04, st);
                n = n + 1;
            end
        end
    endtask

    // IDX <fase> <rct_idx> <apt_idx> <muestras> <rct_cut> <apt_cut>
    task log_idx(input [8*16-1:0] phase);
        reg [31:0] r, a, s, rc, ac;
        begin
            axi_read(8'h24, r);
            axi_read(8'h28, a);
            axi_read(8'h18, s);
            axi_read(8'h10, rc);
            axi_read(8'h14, ac);
            $fdisplay(flog, "IDX %0s %0d %0d %0d %0d %0d", phase, r, a, s, rc, ac);
        end
    endtask

    // ------------------------------------------------------------ secuencia --
    reg [31:0] v, st;
    integer i;
    localparam [31:0] EN = 32'h1, CLR = 32'h2, BYP = 32'h100;

    initial begin
        if (!$value$plusargs("outdir=%s", outdir)) outdir = ".";
        $sformat(fname, "%0s/tb_log.txt", outdir);
        flog = $fopen(fname, "w");
        fraw = 0;

        repeat (5) @(posedge clk);
        resetn = 1;
        repeat (2) @(posedge clk);

        // 1. Identificacion y valores por defecto.
        axi_read(8'h20, v); check("id", v == 32'h54524E47, v);
        axi_read(8'h10, v); check("rct_default", v == 41, v);
        axi_read(8'h14, v); check("apt_default", v == 793, v);
        axi_write(8'h0C, DECIM);
        axi_read(8'h0C, v); check("decim_rw", v == DECIM, v);
        axi_read(8'h04, v); check("idle_fifo_empty", v[3] == 1'b1 && v[0] == 1'b0, v);

        // 2. Funcionamiento normal: arranque y captura.
        open_raw("normal");
        axi_write(8'h00, EN | CLR);
        wait_status(32'h7, 400, st);
        check("startup_done", st[0] == 1'b1, st);
        check("no_alarm_normal", st[2:1] == 2'b00, st);
        $sformat(fname, "%0s/words_normal.txt", outdir);
        fwords = $fopen(fname, "w");
        i = 0;
        while (i < N_WORDS) begin
            axi_read(8'h04, st);
            if (st[3] == 1'b0) begin
                axi_read(8'h08, v);
                $fdisplay(fwords, "%08h", v);
                i = i + 1;
            end else begin
                repeat (DECIM * 8) @(posedge clk);
            end
        end
        $fclose(fwords);
        axi_read(8'h04, st);
        check("no_alarm_after_capture", st[2:1] == 2'b00, st);
        axi_read(8'h1C, v); check("no_drops", v == 0, v);
        log_idx("normal");

        // 3. Fallo: salida bloqueada a 0 => RCT.
        open_raw("stuck");
        axi_write(8'h00, EN | CLR | (32'd1 << 4));
        wait_status(32'h6, 200, st);
        check("stuck_rct_alarm", st[1] == 1'b1, st);
        check("stuck_no_startup", st[0] == 1'b0, st);
        check("stuck_fifo_empty", st[3] == 1'b1, st);
        log_idx("stuck");

        // 4. Fallo: salida sesgada => APT.
        open_raw("biased");
        axi_write(8'h00, EN | CLR | (32'd2 << 4));
        wait_status(32'h6, 400, st);
        check("biased_alarm", st[2:1] != 2'b00, st);
        check("biased_fifo_empty", st[3] == 1'b1, st);
        log_idx("biased");

        // 4b. Sesgo con el RCT desactivado (umbral configurable) => debe saltar el APT.
        open_raw("biased_apt");
        axi_write(8'h10, 32'd60000);
        axi_write(8'h00, EN | CLR | (32'd2 << 4));
        wait_status(32'h6, 400, st);
        check("biased_apt_alarm", st[2] == 1'b1 && st[1] == 1'b0, st);
        log_idx("biased_apt");
        axi_write(8'h10, 32'd41);

        // 5. Fallo: osciladores apagados => RCT.
        open_raw("ro_off");
        axi_write(8'h00, EN | CLR | (32'd3 << 4));
        wait_status(32'h6, 200, st);
        check("ro_off_rct_alarm", st[1] == 1'b1, st);
        check("ro_off_fifo_empty", st[3] == 1'b1, st);
        log_idx("ro_off");

        // 6. Recuperacion tras clear.
        open_raw("recover");
        axi_write(8'h00, EN | CLR);
        wait_status(32'h7, 400, st);
        check("recover_startup", st[0] == 1'b1 && st[2:1] == 2'b00, st);
        log_idx("recover");

        // 7. Modo bypass (solo evaluacion): pasan bits pese a la alarma.
        open_raw("bypass");
        axi_write(8'h00, EN | CLR | BYP | (32'd1 << 4));
        wait_status(32'h8 << 0, 1, st);   // lectura inicial
        repeat (DECIM * 32 * 4) @(posedge clk);
        axi_read(8'h04, st);
        check("bypass_alarm_set", st[1] == 1'b1, st);
        check("bypass_fifo_has_data", st[3] == 1'b0, st);
        axi_read(8'h08, v);
        check("bypass_word_is_zero", v == 32'd0, v);
        axi_read(8'h00, v);
        check("ctrl_readback", v == (EN | BYP | (32'd1 << 4)), v);

        if (fraw != 0) $fclose(fraw);
        $fdisplay(flog, "TB_RESULT %0s", failures == 0 ? "PASS" : "FAIL");
        $fclose(flog);
        $display("tb_trng: %0d fallos", failures);
        $finish;
    end

    // Limite de seguridad del tiempo simulado (100 ms).
    initial begin
        #(64'd100_000_000_000);
        $display("tb_trng: TIMEOUT");
        $fdisplay(flog, "TB_RESULT TIMEOUT");
        $finish;
    end
endmodule
