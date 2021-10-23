// SPDX-License-Identifier: GPL-2.0-or-later
/* AGNi symbols.c 
 */

// Add missing symbols required for force loading vendor modules

#include <linux/module.h>

#ifndef CONFIG_IPC_LOGGING
void *ipc_log_context_create(int max_num_pages,
	const char *modname, uint32_t feature_version) { return NULL; }
EXPORT_SYMBOL(ipc_log_context_create);
int ipc_log_context_destroy(void *ctxt) { return 0; }
EXPORT_SYMBOL(ipc_log_context_destroy);
int ipc_log_string(void *ilctxt, const char *fmt, ...) { return -EINVAL; }
EXPORT_SYMBOL(ipc_log_string);
#endif

#ifndef CONFIG_TRACING
u64 trace_clock_local(void) {
	u64 clock;

	clock = sched_clock();

	return clock;
}
EXPORT_SYMBOL_GPL(trace_clock_local);
#endif

int power_debug_print_enabled;
EXPORT_SYMBOL(power_debug_print_enabled);

int mi_power_save_battery_cave;
EXPORT_SYMBOL(mi_power_save_battery_cave);
