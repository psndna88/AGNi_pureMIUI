/*
 * AGNi Memory Prober v1.2 03-02-2020
 * (c) Parvinder Singh (psndna88@gmail.com)
 * Derived from fs/proc/meminfo.c
 * Calculate % of used ram
 * v1.1: trigger for available-ram based dynamic swappiness
 *       Do not swap when enough ram available
 * v1.2: gpu workload awareness from msm_adreno_tz governor
 * 	 Do not swap on high gpu usage like gaming
 * v1.3: feed actual zram usage of ram as addition for available ram
 */

#include <asm/page.h>
#include <linux/kernel.h>
#include <linux/adrenokgsl_state.h>
#include <linux/agni_meminfo.h>

bool triggerswapping = false;

#define SWAPTRIGGER 20 /* % of free ram */
#define GPULOADTRIGGER 75 /* gpu load % */

bool agni_memprober(void) {
	bool breachlowtf = false;
	long availpages, totalmemk, availablememk, mem_used_perc;
	unsigned long gpu_load_perc;

	availpages = si_mem_available();

#define K(x) ((x) << (PAGE_SHIFT - 10))
	availablememk = K(availpages);
	totalmemk = K(totalram_pages);
#undef K

	mem_used_perc = DIV_ROUND_CLOSEST(((availablememk + zram_ram_usage) * 100),totalmemk);
	
	gpu_load_perc = adreno_load();

	if ((mem_used_perc < SWAPTRIGGER) && (gpu_load_perc <= GPULOADTRIGGER))
		return breachlowtf = true; /* Go for swapping */
	else
		return breachlowtf = false;
}

void agni_memprobe(void) {

	triggerswapping = agni_memprober();
}

