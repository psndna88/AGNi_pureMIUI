// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (C) 2020 XiaoMi, Inc.
 */

#ifndef _MI_DISP_PRINT_H_
#define _MI_DISP_PRINT_H_

#include <linux/compiler.h>
#include <linux/printk.h>

#include "mi_disp_config.h"
#include "mi_disp_debugfs.h"

#define DISP_NAME    "mi_disp"

__printf(2, 3)
void mi_disp_printk(const char *level, const char *format, ...);
__printf(1, 2)
void mi_disp_dbg(const char *format, ...);
__printf(2, 3)
void mi_disp_printk_utc(const char *level, const char *format, ...);
__printf(1, 2)
void mi_disp_dbg_utc(const char *format, ...);

#define DISP_WARN(fmt, ...)
#define DISP_INFO(fmt, ...)
#define DISP_ERROR(fmt, ...)
#define DISP_DEBUG(fmt, ...)
#define DISP_UTC_WARN(fmt, ...)
#define DISP_UTC_INFO(fmt, ...)
#define DISP_UTC_ERROR(fmt, ...)
#define DISP_UTC_DEBUG(fmt, ...)

#endif /* _MI_DISP_PRINT_H_ */
