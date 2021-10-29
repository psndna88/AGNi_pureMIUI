// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, Google LLC. All rights reserved.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/amba/bus.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>

static int coresight_clk_disable_amba_probe(struct amba_device *adev,
					    const struct amba_id *id)
{
	pm_runtime_put(&adev->dev);
	return 0;
}

#define ETM4x_AMBA_ID(pid)                                                     \
	{                                                                      \
		.id = pid, .mask = 0x000fffff,                                 \
	}

#define TMC_ETR_AXI_ARCACHE (0x1U << 1)
#define TMC_ETR_SAVE_RESTORE (0x1U << 2)
#define CORESIGHT_SOC_600_ETR_CAPS (TMC_ETR_SAVE_RESTORE | TMC_ETR_AXI_ARCACHE)

static const struct amba_id coresight_ids[] = {
	/* ETM4 IDs */
	ETM4x_AMBA_ID(0x0003b908),
	ETM4x_AMBA_ID(0x0003b909),
	ETM4x_AMBA_ID(0x0003b961),
	ETM4x_AMBA_ID(0x0003b962),
	ETM4x_AMBA_ID(0x0003b966),
	ETM4x_AMBA_ID(0x0003b968),
	ETM4x_AMBA_ID(0x0003b969),
	ETM4x_AMBA_ID(0x0003b999),
	ETM4x_AMBA_ID(0x000bb95d),
	/* dynamic-replicator IDs */
	{
		.id = 0x000bb909,
		.mask = 0x000fffff,
	},
	{
		/* Coresight SoC-600 */
		.id = 0x000bb9ec,
		.mask = 0x000fffff,
	},
	/* dynamic-funnel IDs */
	{
		.id = 0x000bb908,
		.mask = 0x000fffff,
	},
	{
		/* Coresight SoC-600 */
		.id = 0x000bb9eb,
		.mask = 0x000fffff,
	},
	/* coresight-tmc IDs */
	{
		.id = 0x000bb961,
		.mask = 0x000fffff,
	},
	{
		/* Coresight SoC 600 TMC-ETR/ETS */
		.id = 0x000bb9e8,
		.mask = 0x000fffff,
		.data = (void *)(unsigned long)CORESIGHT_SOC_600_ETR_CAPS,
	},
	{
		/* Coresight SoC 600 TMC-ETB */
		.id = 0x000bb9e9,
		.mask = 0x000fffff,
	},
	{
		/* Coresight SoC 600 TMC-ETF */
		.id = 0x000bb9ea,
		.mask = 0x000fffff,
	},
	{ 0, 0 },
};

static struct amba_driver coresight_clk_disable_amba_driver = {
	.drv = {
		.name	= "coresight-clk-disable-amba",
		.suppress_bind_attrs = true,
	},
	.probe		= coresight_clk_disable_amba_probe,
	.id_table	= coresight_ids,
};

module_amba_driver(coresight_clk_disable_amba_driver);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("CoreSight DEBUGv8 and ETMv4 clock disable AMBA driver stub");
MODULE_AUTHOR("J. Avila <elavila@google.com>");
