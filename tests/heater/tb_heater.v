// Testbench del calefactor (Icarus, -DSIMULATION). Escribe un informe de texto
// que comprueba tests/heater/test_heater_rtl.py.
`timescale 1ps / 1ps
module tb_heater;
    reg clk = 0, resetn = 0;
    always #5000 clk = ~clk;   // 100 MHz
    reg [7:0] awaddr = 0, araddr = 0;
    reg awvalid = 0, wvalid = 0, bready = 1, arvalid = 0, rready = 1;
    reg [31:0] wdata = 0;
    wire awready, wready, bvalid, arready, rvalid;
    wire [1:0] bresp, rresp;
    wire [31:0] rdata;
    heater_axi #(.N_BANKS(3), .RO_PER_BANK(3)) dut (
        .clk(clk), .resetn(resetn),
        .s_axi_awaddr(awaddr), .s_axi_awvalid(awvalid), .s_axi_awready(awready),
        .s_axi_wdata(wdata), .s_axi_wstrb(4'hF), .s_axi_wvalid(wvalid), .s_axi_wready(wready),
        .s_axi_bresp(bresp), .s_axi_bvalid(bvalid), .s_axi_bready(bready),
        .s_axi_araddr(araddr), .s_axi_arvalid(arvalid), .s_axi_arready(arready),
        .s_axi_rdata(rdata), .s_axi_rresp(rresp), .s_axi_rvalid(rvalid), .s_axi_rready(rready));

    task wr(input [7:0] a, input [31:0] d);
        begin
            @(posedge clk); awaddr <= a; wdata <= d; awvalid <= 1; wvalid <= 1;
            @(posedge clk); while (!(awready && wready)) @(posedge clk);
            awvalid <= 0; wvalid <= 0;
            @(posedge clk); while (!bvalid) @(posedge clk);
        end
    endtask
    reg [31:0] rd_val;
    task rd(input [7:0] a);
        begin
            @(posedge clk); araddr <= a; arvalid <= 1;
            @(posedge clk); while (!arready) @(posedge clk);
            arvalid <= 0;
            @(posedge clk); while (!rvalid) @(posedge clk);
            rd_val = rdata;
        end
    endtask

    integer k, changes;
    reg [31:0] prev;
    initial begin
        repeat (5) @(posedge clk);
        resetn <= 1;
        rd(8'h0C); $display("ID %08x", rd_val);
        rd(8'h10); $display("CONFIG %08x", rd_val);
        rd(8'h00); $display("MASK_RESET %08x", rd_val);
        // Sin bancos encendidos la actividad no cambia.
        changes = 0; rd(8'h14); prev = rd_val;
        for (k = 0; k < 40; k = k + 1) begin rd(8'h14); if (rd_val != prev) changes = changes + 1; prev = rd_val; end
        $display("ACT_OFF_CHANGES %0d", changes);
        // Banco 1 encendido: su bit de actividad cambia; los demas no.
        wr(8'h08, 32'd5000);
        wr(8'h00, 32'h2);
        rd(8'h00); $display("MASK_SET %08x", rd_val);
        changes = 0; rd(8'h14); prev = rd_val;
        for (k = 0; k < 40; k = k + 1) begin rd(8'h14); if ((rd_val ^ prev) & 32'h2) changes = changes + 1;
            if ((rd_val ^ prev) & 32'h5) $display("OTHER_BANK_ACTIVE"); prev = rd_val; end
        $display("ACT_ON_CHANGES %0d", changes);
        // Refrescos periodicos (cada < 5000 ciclos): el watchdog no salta.
        for (k = 0; k < 4; k = k + 1) begin repeat (3000) @(posedge clk); wr(8'h00, 32'h2); end
        rd(8'h00); $display("MASK_KICKED %08x", rd_val);
        rd(8'h04); $display("TRIPPED_KICKED %0d", rd_val);
        // Sin refresco: se apaga solo.
        repeat (6000) @(posedge clk);
        rd(8'h00); $display("MASK_WDOG %08x", rd_val);
        rd(8'h04); $display("TRIPPED_WDOG %0d", rd_val);
        rd(8'h08); $display("WDOG %0d", rd_val);
        // Escribir CTRL borra la marca.
        wr(8'h00, 32'h0);
        rd(8'h04); $display("TRIPPED_CLEARED %0d", rd_val);
        $display("DONE");
        $finish;
    end
endmodule
