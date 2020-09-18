// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/slab.h>
#include "cam_cpas_api.h"
#include "cam_sfe_soc.h"
#include "cam_debug_util.h"

static int cam_sfe_get_dt_properties(struct cam_hw_soc_info *soc_info)
{
	int rc = 0;

	rc = cam_soc_util_get_dt_properties(soc_info);
	if (rc)
		CAM_ERR(CAM_SFE, "Error get DT properties failed rc=%d", rc);

	return rc;
}

static int cam_sfe_request_platform_resource(
	struct cam_hw_soc_info *soc_info,
	irq_handler_t irq_handler_func, void *irq_data)
{
	int rc = 0;

	rc = cam_soc_util_request_platform_resource(soc_info, irq_handler_func,
		irq_data);
	if (rc)
		CAM_ERR(CAM_SFE,
			"Error Request platform resource failed rc=%d", rc);

	return rc;
}

static int cam_sfe_release_platform_resource(struct cam_hw_soc_info *soc_info)
{
	int rc = 0;

	rc = cam_soc_util_release_platform_resource(soc_info);
	if (rc)
		CAM_ERR(CAM_SFE,
			"Error Release platform resource failed rc=%d", rc);

	return rc;
}

int cam_sfe_init_soc_resources(struct cam_hw_soc_info *soc_info,
	irq_handler_t irq_handler_func, void *irq_data)
{
	int rc = 0;

	rc = cam_sfe_get_dt_properties(soc_info);
	if (rc < 0) {
		CAM_ERR(CAM_SFE, "Error Get DT properties failed rc=%d", rc);
		goto end;
	}

	rc = cam_sfe_request_platform_resource(soc_info,
		irq_handler_func, irq_data);
	if (rc < 0) {
		CAM_ERR(CAM_SFE,
			"Error Request platform resources failed rc=%d", rc);
		goto end;
	}

end:
	return rc;
}

int cam_sfe_deinit_soc_resources(struct cam_hw_soc_info *soc_info)
{
	int rc = 0;

	if (!soc_info) {
		CAM_ERR(CAM_SFE, "Error soc_info NULL");
		return -ENODEV;
	}

	rc = cam_sfe_release_platform_resource(soc_info);
	if (rc < 0)
		CAM_ERR(CAM_SFE,
			"Error Release platform resources failed rc=%d", rc);

	return rc;
}

int cam_sfe_enable_soc_resources(struct cam_hw_soc_info *soc_info)
{
	int rc = 0;

	rc = cam_soc_util_enable_platform_resource(soc_info, true,
		CAM_TURBO_VOTE, true);
	if (rc) {
		CAM_ERR(CAM_SFE, "Error enable platform failed rc=%d", rc);
		goto end;
	}

end:
	return rc;
}

int cam_sfe_soc_enable_clk(struct cam_hw_soc_info *soc_info,
	const char *clk_name)
{
	return -EPERM;
}

int cam_sfe_soc_disable_clk(struct cam_hw_soc_info *soc_info,
	const char *clk_name)
{
	return -EPERM;
}


int cam_sfe_disable_soc_resources(struct cam_hw_soc_info *soc_info)
{
	int rc = 0;

	rc = cam_soc_util_disable_platform_resource(soc_info, true, true);
	if (rc)
		CAM_ERR(CAM_SFE, "Disable platform failed rc=%d", rc);

	return rc;
}
