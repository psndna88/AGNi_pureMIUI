/* Copyright (c) 2015, Varun Chitre <varun.chitre15@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * A simple hotplugging driver.
 * Compatible from dual core CPUs to Octa Core CPUs
 */

#define ENABLED 1
#define DISABLED 0

#define POWER_SAVER 0
#define BALANCED 1
#define TURBO 2

#define HOTPLUG_PERCORE 1
#define HOTPLUG_SCHED 2

extern int sched_set_boost(int enable);
