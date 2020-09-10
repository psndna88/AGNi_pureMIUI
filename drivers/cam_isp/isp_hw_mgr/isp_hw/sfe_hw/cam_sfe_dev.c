// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/slab.h>
#include <linux/mod_devicetable.h>
#include <linux/of_device.h>
#include "cam_sfe_dev.h"
#include "cam_sfe_core.h"
#include "cam_sfe_soc.h"
#include "cam_sfe_hw_intf.h"
#include "cam_debug_util.h"

static struct cam_hw_intf *sfe_instance;

static char sfe_dev_name[8];

int cam_sfe_probe(struct platform_device *pdev)
{
	struct cam_hw_info                *sfe_info = NULL;
	struct cam_hw_intf                *sfe_intf = NULL;
	const struct of_device_id         *match_dev = NULL;
	struct cam_sfe_hw_core_info       *core_info = NULL;
	struct cam_sfe_hw_info            *hw_info = NULL;
	int                                rc = 0;

	sfe_intf = kzalloc(sizeof(struct cam_hw_intf), GFP_KERNEL);
	if (!sfe_intf) {
		rc = -ENOMEM;
		goto end;
	}

	of_property_read_u32(pdev->dev.of_node,
		"cell-index", &sfe_intf->hw_idx);

	sfe_info = kzalloc(sizeof(struct cam_hw_info), GFP_KERNEL);
	if (!sfe_info) {
		rc = -ENOMEM;
		goto free_sfe_intf;
	}

	memset(sfe_dev_name, 0, sizeof(sfe_dev_name));
	snprintf(sfe_dev_name, sizeof(sfe_dev_name),
		"sfe%1u", sfe_intf->hw_idx);

	sfe_info->soc_info.pdev = pdev;
	sfe_info->soc_info.dev = &pdev->dev;
	sfe_info->soc_info.dev_name = sfe_dev_name;
	sfe_intf->hw_priv = sfe_info;
	sfe_intf->hw_ops.get_hw_caps = cam_sfe_get_hw_caps;
	sfe_intf->hw_ops.init = cam_sfe_init_hw;
	sfe_intf->hw_ops.deinit = cam_sfe_deinit_hw;
	sfe_intf->hw_ops.reset = cam_sfe_reset;
	sfe_intf->hw_ops.reserve = cam_sfe_reserve;
	sfe_intf->hw_ops.release = cam_sfe_release;
	sfe_intf->hw_ops.start = cam_sfe_start;
	sfe_intf->hw_ops.stop = cam_sfe_stop;
	sfe_intf->hw_ops.read = cam_sfe_read;
	sfe_intf->hw_ops.write = cam_sfe_write;
	sfe_intf->hw_ops.process_cmd = cam_sfe_process_cmd;
	sfe_intf->hw_type = CAM_ISP_HW_TYPE_SFE;

	CAM_DBG(CAM_SFE, "type %d index %d",
		sfe_intf->hw_type, sfe_intf->hw_idx);

	platform_set_drvdata(pdev, sfe_intf);

	sfe_info->core_info = kzalloc(sizeof(struct cam_sfe_hw_core_info),
		GFP_KERNEL);
	if (!sfe_info->core_info) {
		CAM_DBG(CAM_SFE, "Failed to alloc for core");
		rc = -ENOMEM;
		goto free_sfe_hw;
	}
	core_info = (struct cam_sfe_hw_core_info *)sfe_info->core_info;

	match_dev = of_match_device(pdev->dev.driver->of_match_table,
		&pdev->dev);
	if (!match_dev) {
		CAM_ERR(CAM_SFE, "Of_match Failed");
		rc = -EINVAL;
		goto free_core_info;
	}
	hw_info = (struct cam_sfe_hw_info *)match_dev->data;
	core_info->sfe_hw_info = hw_info;

	rc = cam_sfe_init_soc_resources(&sfe_info->soc_info, cam_sfe_irq,
		sfe_info);
	if (rc < 0) {
		CAM_ERR(CAM_SFE, "Failed to init soc rc=%d", rc);
		goto free_core_info;
	}

	rc = cam_sfe_core_init(core_info, &sfe_info->soc_info,
		sfe_intf, hw_info);
	if (rc < 0) {
		CAM_ERR(CAM_SFE, "Failed to init core rc=%d", rc);
		goto deinit_soc;
	}

	sfe_info->hw_state = CAM_HW_STATE_POWER_DOWN;
	mutex_init(&sfe_info->hw_mutex);
	spin_lock_init(&sfe_info->hw_lock);
	init_completion(&sfe_info->hw_complete);

	sfe_instance = sfe_intf;

	CAM_DBG(CAM_SFE, "SFE%d probe successful", sfe_intf->hw_idx);

	return rc;

deinit_soc:
	if (cam_sfe_deinit_soc_resources(&sfe_info->soc_info))
		CAM_ERR(CAM_SFE, "Failed to deinit soc");
free_core_info:
	kfree(sfe_info->core_info);
free_sfe_hw:
	kfree(sfe_info);
free_sfe_intf:
	kfree(sfe_intf);
end:
	return rc;
}

int cam_sfe_remove(struct platform_device *pdev)
{
	struct cam_hw_info                *sfe_info = NULL;
	struct cam_hw_intf                *sfe_intf = NULL;
	struct cam_sfe_hw_core_info       *core_info = NULL;
	int                                rc = 0;

	sfe_intf = platform_get_drvdata(pdev);
	if (!sfe_intf) {
		CAM_ERR(CAM_SFE, "Error! No data in pdev");
		return -EINVAL;
	}

	CAM_DBG(CAM_SFE, "type %d index %d",
		sfe_intf->hw_type, sfe_intf->hw_idx);

	sfe_instance = NULL;

	sfe_info = sfe_intf->hw_priv;
	if (!sfe_info) {
		CAM_ERR(CAM_SFE, "Error! HW data is NULL");
		rc = -ENODEV;
		goto free_sfe_intf;
	}

	core_info = (struct cam_sfe_hw_core_info *)sfe_info->core_info;
	if (!core_info) {
		CAM_ERR(CAM_SFE, "Error! core data NULL");
		rc = -EINVAL;
		goto deinit_soc;
	}

	rc = cam_sfe_core_deinit(core_info, core_info->sfe_hw_info);
	if (rc < 0)
		CAM_ERR(CAM_SFE, "Failed to deinit core rc=%d", rc);

	kfree(sfe_info->core_info);

deinit_soc:
	rc = cam_sfe_deinit_soc_resources(&sfe_info->soc_info);
	if (rc < 0)
		CAM_ERR(CAM_SFE, "Failed to deinit soc rc=%d", rc);

	mutex_destroy(&sfe_info->hw_mutex);
	kfree(sfe_info);

	CAM_DBG(CAM_SFE, "SFE%d remove successful", sfe_intf->hw_idx);

free_sfe_intf:
	kfree(sfe_intf);

	return rc;
}

int cam_sfe_hw_init(struct cam_hw_intf **sfe_intf, uint32_t hw_idx)
{
	int rc = 0;

	if (sfe_instance) {
		*sfe_intf = sfe_instance;
		rc = 0;
	} else {
		*sfe_intf = NULL;
		rc = -ENODEV;
	}

	return rc;
}
