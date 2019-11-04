// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2019, The Linux Foundation. All rights reserved.
 */

#include <linux/dma-mapping.h>
#include <linux/of_address.h>

#include "cam_compat.h"
#include "cam_debug_util.h"

#if KERNEL_VERSION(5, 4, 0) >= LINUX_VERSION_CODE
int cam_reserve_icp_fw(struct cam_fw_alloc_info *icp_fw, size_t fw_length)
{
	int rc = 0;
	struct device_node *of_node;
	struct device_node *mem_node;
	struct resource     res;

	of_node = (icp_fw->fw_dev)->of_node;
	mem_node = of_parse_phandle(of_node, "memory-region", 0);
	if (!mem_node) {
		rc = -ENOMEM;
		CAM_ERR(CAM_SMMU, "FW memory carveout not found");
		goto end;
	}
	rc = of_address_to_resource(mem_node, 0, &res);
	of_node_put(mem_node);
	if (rc < 0) {
		CAM_ERR(CAM_SMMU, "Unable to get start of FW mem carveout");
		goto end;
	}
	icp_fw->fw_hdl = res.start;
	icp_fw->fw_kva = ioremap_wc(icp_fw->fw_hdl, fw_length);
	if (!icp_fw->fw_kva) {
		CAM_ERR(CAM_SMMU, "Failed to map the FW.");
		rc = -ENOMEM;
		goto end;
	}
	memset_io(icp_fw->fw_kva, 0, fw_length);

end:
	return rc;
}

void cam_unreserve_icp_fw(struct cam_fw_alloc_info *icp_fw, size_t fw_length)
{
	iounmap(icp_fw->fw_kva);
}
#else
int cam_reserve_icp_fw(struct cam_fw_alloc_info *icp_fw, size_t fw_length)
{
	int rc = 0;

	icp_fw->fw_kva = dma_alloc_coherent(icp_fw->fw_dev, fw_length,
		&icp_fw->fw_hdl, GFP_KERNEL);

	if (!icp_fw->fw_kva) {
		CAM_ERR(CAM_SMMU, "FW memory alloc failed");
		rc = -ENOMEM;
	}

	return rc;
}

void cam_unreserve_icp_fw(struct cam_fw_alloc_info *icp_fw, size_t fw_length)
{
	dma_free_coherent(icp_fw->fw_dev, fw_length, icp_fw->fw_kva,
		icp_fw->fw_hdl);
}
#endif
