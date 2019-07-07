/* Copyright (c) 2002,2007-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define ANY_ID (~0)

static const struct adreno_gpu_core adreno_gpulist[] = {
	{
		.gpurev = ADRENO_REV_A512,
		.core = 5,
		.major = 1,
		.minor = 2,
		.patchid = ANY_ID,
		.features = ADRENO_PREEMPTION | ADRENO_64BIT |
			ADRENO_CONTENT_PROTECTION | ADRENO_CPZ_RETENTION,
		.pm4fw_name = "a530_pm4.fw",
		.pfpfw_name = "a530_pfp.fw",
		.zap_name = "a512_zap",
		.gpudev = &adreno_a5xx_gpudev,
		.gmem_size = (SZ_256K + SZ_16K),
		.num_protected_regs = 0x20,
		.busy_mask = 0xFFFFFFFE,
	},
	{
		.gpurev = ADRENO_REV_A509,
		.core = 5,
		.major = 0,
		.minor = 9,
		.patchid = ANY_ID,
		.features = ADRENO_PREEMPTION | ADRENO_64BIT |
			ADRENO_CONTENT_PROTECTION | ADRENO_CPZ_RETENTION,
		.pm4fw_name = "a530_pm4.fw",
		.pfpfw_name = "a530_pfp.fw",
		.zap_name = "a512_zap",
		.gpudev = &adreno_a5xx_gpudev,
		.gmem_size = (SZ_256K + SZ_16K),
   		.num_protected_regs = 0x20,
		.busy_mask = 0xFFFFFFFE,
	}
};
