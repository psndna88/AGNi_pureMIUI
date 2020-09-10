/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef _CAM_SFE_DEV_H_
#define _CAM_SFE_DEV_H_

#include <linux/platform_device.h>

/*
 * cam_sfe_probe()
 *
 * @brief:                   Driver probe function called on Boot
 *
 * @pdev:                    Platform Device pointer
 *
 * @Return:                  0: Success
 *                           Non-zero: Failure
 */
int cam_sfe_probe(struct platform_device *pdev);

/*
 * cam_sfe_remove()
 *
 * @brief:                   Driver remove function
 *
 * @pdev:                    Platform Device pointer
 *
 * @Return:                  0: Success
 *                           Non-zero: Failure
 */
int cam_sfe_remove(struct platform_device *pdev);

#endif /* _CAM_SFE_DEV_H_ */
