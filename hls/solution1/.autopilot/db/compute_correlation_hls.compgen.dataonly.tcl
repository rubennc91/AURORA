# This script segment is generated automatically by AutoPilot

set axilite_register_dict [dict create]
set port_control {
alice_tags { 
	dir I
	width 64
	depth 1
	mode ap_none
	offset 16
	offset_end 27
}
bob_tags { 
	dir I
	width 64
	depth 1
	mode ap_none
	offset 28
	offset_end 39
}
size_a { 
	dir I
	width 32
	depth 1
	mode ap_none
	offset 40
	offset_end 47
}
size_b { 
	dir I
	width 32
	depth 1
	mode ap_none
	offset 48
	offset_end 55
}
window { 
	dir I
	width 64
	depth 1
	mode ap_none
	offset 56
	offset_end 67
}
result_i { 
	dir I
	width 128
	depth 1
	mode ap_none
	offset 80
	offset_end 99
}
result_o { 
	dir O
	width 128
	depth 1
	mode ap_vld
	offset 100
	offset_end 119
}
ap_start { }
ap_done { }
ap_ready { }
ap_idle { }
interrupt {
}
}
dict set axilite_register_dict control $port_control


