// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/slab.h>
#include <linux/list.h>
#include "cam_tasklet_util.h"
#include "cam_sfe_hw_intf.h"
#include "cam_sfe_soc.h"
#include "cam_sfe_core.h"
#include "cam_debug_util.h"

static const char drv_name[] = "sfe";
#define SFE_CORE_BASE_IDX      0

int cam_sfe_get_hw_caps(void *hw_priv, void *get_hw_cap_args, uint32_t arg_size)
{
	CAM_DBG(CAM_SFE, "Enter");
	return 0;
}

int cam_sfe_init_hw(void *hw_priv, void *init_hw_args, uint32_t arg_size)
{
	struct cam_hw_info                *sfe_hw = hw_priv;
	struct cam_hw_soc_info            *soc_info = NULL;
	struct cam_sfe_hw_core_info       *core_info = NULL;
	struct cam_isp_resource_node      *isp_res = NULL;
	int rc = 0;

	CAM_DBG(CAM_SFE, "Enter");
	if (!hw_priv) {
		CAM_ERR(CAM_SFE, "Invalid arguments");
		return -EINVAL;
	}

	mutex_lock(&sfe_hw->hw_mutex);
	sfe_hw->open_count++;
	if (sfe_hw->open_count > 1) {
		mutex_unlock(&sfe_hw->hw_mutex);
		CAM_DBG(CAM_SFE, "SFE has already been initialized cnt %d",
			sfe_hw->open_count);
		return 0;
	}
	mutex_unlock(&sfe_hw->hw_mutex);

	soc_info = &sfe_hw->soc_info;
	core_info = (struct cam_sfe_hw_core_info *)sfe_hw->core_info;

	/* Turn ON Regulators, Clocks and other SOC resources */
	rc = cam_sfe_enable_soc_resources(soc_info);
	if (rc) {
		CAM_ERR(CAM_SFE, "Enable SOC failed");
		rc = -EFAULT;
		goto decrement_open_cnt;
	}

	isp_res   = (struct cam_isp_resource_node *)init_hw_args;
	if (isp_res && isp_res->init) {
		rc = isp_res->init(isp_res, NULL, 0);
		if (rc) {
			CAM_ERR(CAM_SFE, "init Failed rc=%d", rc);
			goto disable_soc;
		}
	}

	CAM_DBG(CAM_SFE, "Enable soc done");

	/* Do HW Reset */
	rc = cam_sfe_reset(hw_priv, NULL, 0);
	if (rc) {
		CAM_ERR(CAM_SFE, "Reset Failed rc=%d", rc);
		goto deinit_sfe_res;
	}

	sfe_hw->hw_state = CAM_HW_STATE_POWER_UP;
	return rc;

deinit_sfe_res:
	if (isp_res && isp_res->deinit)
		isp_res->deinit(isp_res, NULL, 0);
disable_soc:
	cam_sfe_disable_soc_resources(soc_info);
decrement_open_cnt:
	mutex_lock(&sfe_hw->hw_mutex);
	sfe_hw->open_count--;
	mutex_unlock(&sfe_hw->hw_mutex);
	return rc;
}

int cam_sfe_deinit_hw(void *hw_priv, void *deinit_hw_args, uint32_t arg_size)
{
	struct cam_hw_info                *sfe_hw = hw_priv;
	struct cam_hw_soc_info            *soc_info = NULL;
	struct cam_sfe_hw_core_info       *core_info = NULL;
	struct cam_isp_resource_node      *isp_res = NULL;
	int rc = 0;

	CAM_DBG(CAM_SFE, "Enter");
	if (!hw_priv) {
		CAM_ERR(CAM_SFE, "Invalid arguments");
		return -EINVAL;
	}

	mutex_lock(&sfe_hw->hw_mutex);
	if (!sfe_hw->open_count) {
		mutex_unlock(&sfe_hw->hw_mutex);
		CAM_ERR(CAM_SFE, "Error! Unbalanced deinit");
		return -EFAULT;
	}
	sfe_hw->open_count--;
	if (sfe_hw->open_count) {
		mutex_unlock(&sfe_hw->hw_mutex);
		CAM_DBG(CAM_SFE, "open_cnt non-zero =%d", sfe_hw->open_count);
		return 0;
	}
	mutex_unlock(&sfe_hw->hw_mutex);

	soc_info = &sfe_hw->soc_info;
	core_info = (struct cam_sfe_hw_core_info *)sfe_hw->core_info;

	isp_res   = (struct cam_isp_resource_node *)deinit_hw_args;
	if (isp_res && isp_res->deinit) {
		rc = isp_res->deinit(isp_res, NULL, 0);
		if (rc)
			CAM_ERR(CAM_SFE, "deinit failed");
	}

	/* Turn OFF Regulators, Clocks and other SOC resources */
	CAM_DBG(CAM_SFE, "Disable SOC resource");
	rc = cam_sfe_disable_soc_resources(soc_info);
	if (rc)
		CAM_ERR(CAM_SFE, "Disable SOC failed");

	sfe_hw->hw_state = CAM_HW_STATE_POWER_DOWN;

	CAM_DBG(CAM_SFE, "Exit");
	return rc;
}

int cam_sfe_reset(void *hw_priv, void *reset_core_args, uint32_t arg_size)
{
	CAM_DBG(CAM_SFE, "Enter");
	return 0;
}

int cam_sfe_reserve(void *hw_priv, void *reserve_args, uint32_t arg_size)
{
	CAM_DBG(CAM_SFE, "Enter");
	return 0;
}

int cam_sfe_release(void *hw_priv, void *release_args, uint32_t arg_size)
{
	CAM_DBG(CAM_SFE, "Enter");
	return 0;
}

int cam_sfe_start(void *hw_priv, void *start_args, uint32_t arg_size)
{
	CAM_DBG(CAM_SFE, "Enter");
	return 0;
}

int cam_sfe_stop(void *hw_priv, void *stop_args, uint32_t arg_size)
{
	CAM_DBG(CAM_SFE, "Enter");
	return 0;
}

int cam_sfe_read(void *hw_priv, void *read_args, uint32_t arg_size)
{
	return -EPERM;
}

int cam_sfe_write(void *hw_priv, void *write_args, uint32_t arg_size)
{
	return -EPERM;
}

int cam_sfe_process_cmd(void *hw_priv, uint32_t cmd_type,
	void *cmd_args, uint32_t arg_size)
{
	struct cam_hw_info                *sfe_hw = hw_priv;
	struct cam_hw_soc_info            *soc_info = NULL;
	struct cam_sfe_hw_core_info       *core_info = NULL;
	struct cam_sfe_hw_info            *hw_info = NULL;
	int rc;

	if (!hw_priv) {
		CAM_ERR(CAM_SFE, "Invalid arguments");
		return -EINVAL;
	}

	soc_info = &sfe_hw->soc_info;
	core_info = (struct cam_sfe_hw_core_info *)sfe_hw->core_info;
	hw_info = core_info->sfe_hw_info;

	switch (cmd_type) {
	case CAM_ISP_HW_CMD_GET_CHANGE_BASE:
	case CAM_ISP_HW_CMD_GET_REG_UPDATE:
		rc = 0;
		break;

	default:
		CAM_ERR(CAM_SFE, "Invalid cmd type:%d", cmd_type);
		rc = -EINVAL;
		break;
	}

	return rc;
}

irqreturn_t cam_sfe_irq(int irq_num, void *data)
{
	struct cam_hw_info            *sfe_hw;
	struct cam_sfe_hw_core_info   *core_info;

	if (!data)
		return IRQ_NONE;

	sfe_hw = (struct cam_hw_info *)data;
	core_info = (struct cam_sfe_hw_core_info *)sfe_hw->core_info;

	return cam_irq_controller_handle_irq(irq_num,
		core_info->sfe_irq_controller);
}

int cam_sfe_core_init(
	struct cam_sfe_hw_core_info                *core_info,
	struct cam_hw_soc_info                     *soc_info,
	struct cam_hw_intf                         *hw_intf,
	struct cam_sfe_hw_info                     *sfe_hw_info)
{

	CAM_DBG(CAM_SFE, "Enter");

	INIT_LIST_HEAD(&core_info->free_payload_list);
	spin_lock_init(&core_info->spin_lock);

	return 0;
}

int cam_sfe_core_deinit(
	struct cam_sfe_hw_core_info  *core_info,
	struct cam_sfe_hw_info       *sfe_hw_info)
{
	int                rc = -EINVAL;
	unsigned long      flags;

	spin_lock_irqsave(&core_info->spin_lock, flags);

	INIT_LIST_HEAD(&core_info->free_payload_list);

	rc = cam_irq_controller_deinit(&core_info->sfe_irq_controller);
	if (rc)
		CAM_ERR(CAM_SFE,
			"Error cam_irq_controller_deinit failed rc=%d", rc);

	spin_unlock_irqrestore(&core_info->spin_lock, flags);

	return rc;
}
