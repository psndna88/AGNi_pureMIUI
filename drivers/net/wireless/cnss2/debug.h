/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2016-2021, The Linux Foundation. All rights reserved. */

#ifndef _CNSS_DEBUG_H
#define _CNSS_DEBUG_H

#if IS_ENABLED(CONFIG_IPC_LOGGING)
#include <linux/ipc_logging.h>
#endif
#include <linux/printk.h>

#if IS_ENABLED(CONFIG_IPC_LOGGING)
#define CNSS_IPC_LOG_PAGES		32

extern void *cnss_ipc_log_context;
extern void *cnss_ipc_log_long_context;

#define cnss_ipc_log_string(_x...)
#define cnss_ipc_log_long_string(_x...)
#endif

#define cnss_pr_err(_fmt, ...)

#define cnss_pr_warn(_fmt, ...)

#define cnss_pr_info(_fmt, ...)

#define cnss_pr_dbg(_fmt, ...)

#define cnss_pr_vdbg(_fmt, ...)

#define cnss_pr_buf(_fmt, ...)

#define CNSS_ASSERT(_condition)

#define cnss_fatal_err(_fmt, ...)

int cnss_debug_init(void);
void cnss_debug_deinit(void);
int cnss_debugfs_create(struct cnss_plat_data *plat_priv);
void cnss_debugfs_destroy(struct cnss_plat_data *plat_priv);

#endif /* _CNSS_DEBUG_H */
