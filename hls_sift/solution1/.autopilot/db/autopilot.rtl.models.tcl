set SynModuleInfo {
  {SRCNAME compute_correlation_hls_Pipeline_SEARCH_ALICE_SIFT MODELNAME compute_correlation_hls_Pipeline_SEARCH_ALICE_SIFT RTLNAME compute_correlation_hls_compute_correlation_hls_Pipeline_SEARCH_ALICE_SIFT}
  {SRCNAME compute_correlation_hls_Pipeline_SEARCH_ALICE_SYNC MODELNAME compute_correlation_hls_Pipeline_SEARCH_ALICE_SYNC RTLNAME compute_correlation_hls_compute_correlation_hls_Pipeline_SEARCH_ALICE_SYNC
    SUBMODULES {
      {MODELNAME compute_correlation_hls_flow_control_loop_pipe_sequential_init RTLNAME compute_correlation_hls_flow_control_loop_pipe_sequential_init BINDTYPE interface TYPE internal_upc_flow_control INSTNAME compute_correlation_hls_flow_control_loop_pipe_sequential_init_U}
    }
  }
  {SRCNAME compute_correlation_hls MODELNAME compute_correlation_hls RTLNAME compute_correlation_hls IS_TOP 1
    SUBMODULES {
      {MODELNAME compute_correlation_hls_gmem0_m_axi RTLNAME compute_correlation_hls_gmem0_m_axi BINDTYPE interface TYPE adapter IMPL m_axi}
      {MODELNAME compute_correlation_hls_gmem1_m_axi RTLNAME compute_correlation_hls_gmem1_m_axi BINDTYPE interface TYPE adapter IMPL m_axi}
      {MODELNAME compute_correlation_hls_gmem2_m_axi RTLNAME compute_correlation_hls_gmem2_m_axi BINDTYPE interface TYPE adapter IMPL m_axi}
      {MODELNAME compute_correlation_hls_control_s_axi RTLNAME compute_correlation_hls_control_s_axi BINDTYPE interface TYPE interface_s_axilite}
    }
  }
}
