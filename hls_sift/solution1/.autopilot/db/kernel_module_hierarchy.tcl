set ModuleHierarchy {[{
"Name" : "compute_correlation_hls","ID" : "0","Type" : "sequential",
"SubLoops" : [
	{"Name" : "LAG_LOOP","ID" : "1","Type" : "no",
	"SubLoops" : [
	{"Name" : "BOB_LOOP_SYNC","ID" : "2","Type" : "no",
		"SubInsts" : [
		{"Name" : "grp_compute_correlation_hls_Pipeline_SEARCH_ALICE_SYNC_fu_414","ID" : "3","Type" : "sequential",
				"SubLoops" : [
				{"Name" : "SEARCH_ALICE_SYNC","ID" : "4","Type" : "pipeline"},]},]},]},
	{"Name" : "BOB_LOOP_SIFT","ID" : "5","Type" : "no",
	"SubInsts" : [
	{"Name" : "grp_compute_correlation_hls_Pipeline_SEARCH_ALICE_SIFT_fu_401","ID" : "6","Type" : "sequential",
			"SubLoops" : [
			{"Name" : "SEARCH_ALICE_SIFT","ID" : "7","Type" : "pipeline"},]},]},]
}]}