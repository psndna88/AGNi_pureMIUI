/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (C) 2020 XiaoMi, Inc.
 */

#include "mi_dsi_display.h"

int mi_get_disp_id(struct dsi_display *display)
{
	if (!strncmp(display->display_type, "primary", 7))
		return DSI_PRIMARY;
	else
		return DSI_SECONDARY;
}
