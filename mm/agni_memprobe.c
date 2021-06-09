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
 * v1.5: Rewrite Logic cleanly, optimise. Drop caches aswell as needed.
 * v1.6: Allow zram swapping on gaming and increase swappiness for 4 gb ram device variants
 * v1.7: Increased swappiness values with increased zram disksize
 * v1.8: Turn off the swappiness control routine when zram disksize set to zero
 *       Also enable dynamic fsync when zram enabled
 * v1.9: Account for 3GB ram devices and tune zram disksize accordingly
 * v2.0: Handle 3GB ram devices more aggressively
 * v2.1: Do not drop caches, change behaviour for miui roms, control swappiness for > 4GB ram devices
 * v2.2: Full swappiness for 3gb devices, changes for others
 * v2.3: Reduce swappiness only when battery < 15% & not charging
 */

#include <asm/page.h>
#include <linux/adrenokgsl_state.h>
#include <linux/charging_state.h>
#include <linux/agni_meminfo.h>
#include <linux/mm.h>

bool triggerswapping = false;
int agni_swappiness = 60;
long totalmemk,mem_avail_perc;
int trigthreshold;
bool ramchecked = false;
int ramgb;

void device_totalram(void) {
	unsigned long int totalrampages;

	if (!ramchecked) {
		totalrampages = totalram_pages();
		totalmemk = totalrampages << (PAGE_SHIFT - 10);
	
		if (totalmemk > 5505024) {
			ramgb = 8; /* 8GB device */
			trigthreshold = 15;
		} else if ((totalmemk > 5000000) && (totalmemk < 5505024)) {
			ramgb = 6; /* 6GB device */
			trigthreshold = 15;
		} else if ((totalmemk > 3000000) && (totalmemk < 5000000)) {
			ramgb = 4; /* 4GB device */
			trigthreshold = 20;
		} else {
			ramgb = 3; /* 3GB device */
			trigthreshold = 40;
		}

		ramchecked = true;
	}
}

void availmem_prober(void) {
	long availpages, availablememk;

	/* Ram pages */
	availpages = si_mem_available();
	availablememk = availpages << (PAGE_SHIFT - 10);
	mem_avail_perc = ((availablememk + zram_ram_usage) * 100) / totalmemk;
}

bool agni_memprober(void) {
	bool vote = false;

	availmem_prober();

	/* Decide voting */
//	if (!charging_detected()) {
//		if (mem_avail_perc < trigthreshold) /* low available ram when not charging */
//			vote = true;
//		else
//			vote = false; /* stop swapping when enough available ram */
//	} else {
//		if (batt_swap_push && (mem_avail_perc < trigthreshold)) /* Battery > 80 % and low available ram with charging ON  */
//			vote = true;
//		else
//			vote = false; /* Allow charging faster by keeping swapping off and thus less cpu usage */
//	}

//	if (adreno_load_perc > GPULOADTRIGGER) { /* High GPU usage - typically while gaming */
//		if (ramgb <= 4)
//			vote = true; /* big games need more ram so zram swapping is beneficial on 4gb devices */
//		else
//			vote = false;
//	}

//	if ((low_batt_swap_stall) && (!charging_detected())) { /* Battery below 25% & not charging*/
//		vote = false;
//	} else {
		vote = true;
//	}

	return vote;
}

void agni_memprobe(void) {

	device_totalram();

	triggerswapping = agni_memprober();
}

