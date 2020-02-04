/*
 * AGNi Memory Prober 03-02-2020
 * (c) Parvinder Singh (psndna88@gmail.com)
 * Derived from fs/proc/meminfo.c
 * Calculate % of used ram
 * v1.1: trigger for available-ram based dynamic swappiness
 *       Do not swap when enough ram available
 * v1.2: gpu workload awareness from msm_adreno_tz governor
 * 	 Do not swap on high gpu usage like gaming
 * v1.3: feed actual zram usage of ram as addition for available ram
 * v1.4: use charging & battery % detection to decide swap behaviour by voting. Rewrite.
 */

#include <asm/page.h>
#include <linux/kernel.h>
#include <linux/adrenokgsl_state.h>
#include <linux/charging_state.h>
#include <linux/agni_meminfo.h>

bool triggerswapping = false;

#define GPULOADTRIGGER 75 /* gpu load % threshold */

bool agni_memprober(void) {
	int ramtrigger;
	bool vote = false;
	long availpages, totalmemk, availablememk, mem_used_perc;
	unsigned long gpu_load_perc;

	/* Ram pages */
	availpages = si_mem_available();
	availablememk = availpages << (PAGE_SHIFT - 10);
	totalmemk = totalram_pages << (PAGE_SHIFT - 10);
	mem_used_perc = DIV_ROUND_CLOSEST(((availablememk + zram_ram_usage) * 100),totalmemk);
	if (totalmemk > 4000000) {
		ramtrigger = 15; /* % of available ram - 6GB device*/
	} else {
		ramtrigger = 25; /* % of available ram - 4GB device*/
	}
	/* GPU load */
	gpu_load_perc = adreno_load();

	/* Decide voting */
	if (!charging_detected()) {
		if (mem_used_perc < ramtrigger) /* low available ram when not charging */
			vote = true;
		else
			vote = false; /* stop swapping when enough available ram */
	} else {
		if (batt_swap_push && (mem_used_perc < ramtrigger)) /* Battery > 80 % and low available ram with charging ON  */
			vote = true;
		else
			vote = false; /* Allow charging faster by keeping swapping off and thus less cpu usage */
	}
	if ((gpu_load_perc > GPULOADTRIGGER) || low_batt_swap_stall) /* High GPU usage - typically while gaming OR Battery below 25% */
		vote = false;

	return vote;
}

void agni_memprobe(void) {

	triggerswapping = agni_memprober();
}

