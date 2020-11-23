/*
 * ring buffer based function tracer -DUMMY
 *
 * Copyright (C) 2007-2012 Steven Rostedt <srostedt@redhat.com>
 * Copyright (C) 2008 Ingo Molnar <mingo@redhat.com>
 * Copyright (C) 2008 Parvinder Singh <psndna88@gmail.com>
 */

#include <linux/kallsyms.h>

void *ipc_log_context_create(int max_num_pages,
			     const char *mod_name, uint16_t user_version)
{
	return 0;
}
EXPORT_SYMBOL(ipc_log_context_create);

int ipc_log_string(void *ilctxt, const char *fmt, ...)
{
	return 0;
}
EXPORT_SYMBOL(ipc_log_string);

int __trace_bprintk(unsigned long ip, const char *fmt, ...)
{
	return 0;
}
EXPORT_SYMBOL_GPL(__trace_bprintk);

void __ref kmemleak_ignore(const void *ptr) { }
EXPORT_SYMBOL(kmemleak_ignore);
