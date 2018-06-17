/* Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "sched.h"

/*
 * Scheduler boost is a mechanism to temporarily place tasks on CPUs
 * with higher capacity than those where a task would have normally
 * ended up with their load characteristics. Any entity enabling
 * boost is responsible for disabling it as well.
 */

unsigned int sysctl_sched_boost;

static bool verify_boost_params(int old_val, int new_val)
{
	/*
	 * Boost can only be turned on or off. There is no possiblity of
	 * switching from one boost type to another or to set the same
	 * kind of boost several times.
	 */
	return !(!!old_val == !!new_val);
}

int sched_boost_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos)
{
	int ret;
	unsigned int *data = (unsigned int *)table->data;
	unsigned int old_val;

	// Backup current sysctl_sched_boost value
	old_val = *data;

	// Set new sysctl_sched_boost value
	ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);

	if (ret || !write)
		goto done;

#ifdef CONFIG_DYNAMIC_STUNE_BOOST
	if (verify_boost_params(old_val, *data)) {
		if (*data > 0)
			stune_boost("top-app");
		else
			reset_stune_boost("top-app");
	} else {
		*data = old_val;
		ret = -EINVAL;
	}
#endif // CONFIG_DYNAMIC_STUNE_BOOST

done:
	return ret;
}
