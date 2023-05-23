#ifndef __CPUFREQ_BOUNCING_H__
#define __CPUFREQ_BOUNCING_H__

#include <linux/cpufreq.h>

void cb_update(struct cpufreq_policy *pol, u64 time);
void cb_reset(int cpu, u64 time);
unsigned int cb_cap(struct cpufreq_policy *pol, unsigned int freq);

#endif
