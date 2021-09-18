#
# Makefile for the RMNET module
#

rmnet-y		 := rmnet_config.o
rmnet-y		 += rmnet_vnd.o
rmnet-y		 += rmnet_handlers.o
rmnet-y		 += rmnet_map_data.o
rmnet-y		 += rmnet_map_command.o
rmnet-y		 += rmnet_descriptor.o
obj-$(CONFIG_RMNET) += rmnet.o

obj-$(CONFIG_RMNET_PERF) += rmnet_perf.o
rmnet_perf-y := rmnet_perf_config.o rmnet_perf_core.o rmnet_perf_opt.o rmnet_perf_tcp_opt.o rmnet_perf_udp_opt.o

obj-$(CONFIG_RMNET_SHS) += rmnet_shs.o
rmnet_shs-y := rmnet_shs_config.o rmnet_shs_main.o rmnet_shs_wq.o rmnet_shs_freq.o rmnet_shs_wq_mem.o rmnet_shs_wq_genl.o
