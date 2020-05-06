/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 */


#ifndef _CAM_VFE_LITE680X_H_
#define _CAM_VFE_LITE680X_H_
#include "cam_vfe_camif_ver3.h"
#include "cam_vfe_top_ver3.h"
#include "cam_vfe_core.h"
#include "cam_vfe_bus_ver3.h"
#include "cam_irq_controller.h"

static struct cam_irq_register_set vfe680x_bus_irq_reg[2] = {
		{
			.mask_reg_offset   = 0x00000218,
			.clear_reg_offset  = 0x00000220,
			.status_reg_offset = 0x00000228,
		},
		{
			.mask_reg_offset   = 0x0000021C,
			.clear_reg_offset  = 0x00000224,
			.status_reg_offset = 0x0000022C,
		},
};

static struct cam_vfe_bus_ver3_hw_info vfe680x_bus_hw_info = {
	.common_reg = {
		.hw_version                       = 0x00000200,
		.cgc_ovd                          = 0x00000208,
		.if_frameheader_cfg               = {
			0x00000234,
			0x00000238,
			0x0000023C,
			0x00000240,
			0x00000244,
			0x00000248,
		},
		.pwr_iso_cfg                      = 0x0000025C,
		.overflow_status_clear            = 0x00000260,
		.ccif_violation_status            = 0x00000264,
		.overflow_status                  = 0x00000268,
		.image_size_violation_status      = 0x00000270,
		.debug_status_top_cfg             = 0x000002D4,
		.debug_status_top                 = 0x000002D8,
		.test_bus_ctrl                    = 0x000002DC,
		.irq_reg_info = {
			.num_registers            = 2,
			.irq_reg_set              = vfe680x_bus_irq_reg,
			.global_clear_offset      = 0x00000230,
			.global_clear_bitmask     = 0x00000001,
		},
	},
	.num_client = 4,
	.bus_client_reg = {
		/* BUS Client 0 RDI0 */
		{
			.cfg                      = 0x00000400,
			.image_addr               = 0x00000404,
			.frame_incr               = 0x00000408,
			.image_cfg_0              = 0x0000040C,
			.image_cfg_1              = 0x00000410,
			.image_cfg_2              = 0x00000414,
			.packer_cfg               = 0x00000418,
			.frame_header_addr        = 0x00000420,
			.frame_header_incr        = 0x00000424,
			.frame_header_cfg         = 0x00000428,
			.irq_subsample_period     = 0x00000430,
			.irq_subsample_pattern    = 0x00000434,
			.framedrop_period         = 0x00000438,
			.framedrop_pattern        = 0x0000043C,
			.mmu_prefetch_cfg         = 0x00000460,
			.mmu_prefetch_max_offset  = 0x00000464,
			.system_cache_cfg         = 0x00000468,
			.addr_status_0            = 0x00000470,
			.addr_status_1            = 0x00000474,
			.addr_status_2            = 0x00000478,
			.addr_status_3            = 0x0000047C,
			.debug_status_cfg         = 0x00000480,
			.debug_status_0           = 0x00000484,
			.debug_status_1           = 0x00000488,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_0,
			.ubwc_regs                = NULL,
		},
		/* BUS Client 1 RDI1 */
		{
			.cfg                      = 0x00000500,
			.image_addr               = 0x00000504,
			.frame_incr               = 0x00000508,
			.image_cfg_0              = 0x0000050C,
			.image_cfg_1              = 0x00000510,
			.image_cfg_2              = 0x00000514,
			.packer_cfg               = 0x00000518,
			.frame_header_addr        = 0x00000520,
			.frame_header_incr        = 0x00000524,
			.frame_header_cfg         = 0x00000528,
			.irq_subsample_period     = 0x00000530,
			.irq_subsample_pattern    = 0x00000534,
			.framedrop_period         = 0x00000538,
			.framedrop_pattern        = 0x0000053C,
			.mmu_prefetch_cfg         = 0x00000560,
			.mmu_prefetch_max_offset  = 0x00000564,
			.system_cache_cfg         = 0x00000568,
			.addr_status_0            = 0x00000570,
			.addr_status_1            = 0x00000574,
			.addr_status_2            = 0x00000578,
			.addr_status_3            = 0x0000057C,
			.debug_status_cfg         = 0x00000580,
			.debug_status_0           = 0x00000584,
			.debug_status_1           = 0x00000588,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_1,
			.ubwc_regs                = NULL,
		},
		/* BUS Client 2 RDI2 */
		{
			.cfg                      = 0x00000600,
			.image_addr               = 0x00000604,
			.frame_incr               = 0x00000608,
			.image_cfg_0              = 0x0000060C,
			.image_cfg_1              = 0x00000610,
			.image_cfg_2              = 0x00000614,
			.packer_cfg               = 0x00000618,
			.frame_header_addr        = 0x00000620,
			.frame_header_incr        = 0x00000624,
			.frame_header_cfg         = 0x00000628,
			.irq_subsample_period     = 0x00000630,
			.irq_subsample_pattern    = 0x00000634,
			.framedrop_period         = 0x00000638,
			.framedrop_pattern        = 0x0000063C,
			.mmu_prefetch_cfg         = 0x00000660,
			.mmu_prefetch_max_offset  = 0x00000664,
			.system_cache_cfg         = 0x00000668,
			.addr_status_0            = 0x00000670,
			.addr_status_1            = 0x00000674,
			.addr_status_2            = 0x00000678,
			.addr_status_3            = 0x0000067C,
			.debug_status_cfg         = 0x00000680,
			.debug_status_0           = 0x00000684,
			.debug_status_1           = 0x00000688,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_2,
			.ubwc_regs                = NULL,
		},
		/* BUS Client 3 RDI3 */
		{
			.cfg                      = 0x00000700,
			.image_addr               = 0x00000704,
			.frame_incr               = 0x00000708,
			.image_cfg_0              = 0x0000070C,
			.image_cfg_1              = 0x00000710,
			.image_cfg_2              = 0x00000714,
			.packer_cfg               = 0x00000718,
			.frame_header_addr        = 0x00000720,
			.frame_header_incr        = 0x00000724,
			.frame_header_cfg         = 0x00000728,
			.irq_subsample_period     = 0x00000730,
			.irq_subsample_pattern    = 0x00000734,
			.framedrop_period         = 0x00000738,
			.framedrop_pattern        = 0x0000073C,
			.mmu_prefetch_cfg         = 0x00000760,
			.mmu_prefetch_max_offset  = 0x00000764,
			.system_cache_cfg         = 0x00000768,
			.addr_status_0            = 0x00000770,
			.addr_status_1            = 0x00000774,
			.addr_status_2            = 0x00000778,
			.addr_status_3            = 0x0000077C,
			.debug_status_cfg         = 0x00000780,
			.debug_status_0           = 0x00000784,
			.debug_status_1           = 0x00000788,
			.comp_group               = CAM_VFE_BUS_VER3_COMP_GRP_3,
			.ubwc_regs                = NULL,
		},
	},
	.num_out = 4,
	.vfe_out_hw_info = {
		{
			.vfe_out_type  = CAM_VFE_BUS_VER3_VFE_OUT_RDI0,
			.max_width     = -1,
			.max_height    = -1,
			.source_group  = CAM_VFE_BUS_VER3_SRC_GRP_0,
			.num_wm        = 1,
			.wm_idx        = {
				0,
			},
		},
		{
			.vfe_out_type  = CAM_VFE_BUS_VER3_VFE_OUT_RDI1,
			.max_width     = -1,
			.max_height    = -1,
			.source_group  = CAM_VFE_BUS_VER3_SRC_GRP_1,
			.num_wm        = 1,
			.wm_idx        = {
				1,
			},
		},
		{
			.vfe_out_type  = CAM_VFE_BUS_VER3_VFE_OUT_RDI2,
			.max_width     = -1,
			.max_height    = -1,
			.source_group  = CAM_VFE_BUS_VER3_SRC_GRP_2,
			.num_wm        = 1,
			.wm_idx        = {
				2,
			},
		},
		{
			.vfe_out_type  = CAM_VFE_BUS_VER3_VFE_OUT_RDI3,
			.max_width     = -1,
			.max_height    = -1,
			.source_group  = CAM_VFE_BUS_VER3_SRC_GRP_3,
			.num_wm        = 1,
			.wm_idx        = {
				3,
			},
		},
	},
	.comp_done_shift = 6,
	.top_irq_shift   = 1,
	.max_out_res = CAM_ISP_IFE_OUT_RES_BASE + 33,
};

static struct cam_vfe_hw_info cam_vfe_lite68x_hw_info = {
	.irq_reg_info                  =,

	.bus_version                   = CAM_VFE_BUS_VER_3_0,
	.bus_hw_info                   = &vfe680x_bus_hw_info,

	.bus_rd_version                = NULL,
	.bus_rd_hw_info                = NULL,

	.top_version                   =,
	.top_hw_info                   =,
};

#endif /* _CAM_VFE_LITE680X_H_ */
