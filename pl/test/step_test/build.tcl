import_device dr1_90.db -package DR1M90GEG400 -speed 2
read_verilog step_test.v
read_adc step_test.adc
read_sdc step_test.sdc
optimize_rtl
optimize_gate
legalize_phy_inst
place
route
report_area -io_info -file step_test_phy.area
update_timing -mode final
report_timing_summary -file step_test_pr.timing
bitgen -bit step_test.bit
exit
