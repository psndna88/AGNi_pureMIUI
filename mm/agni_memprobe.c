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
 */

#include <asm/page.h>
#include <linux/adrenokgsl_state.h>
#include <linux/charging_state.h>
#include <linux/agni_meminfo.h>

bool triggerswapping = false;
int agni_swappiness = 1;
long totalmemk,mem_avail_perc;
int trigthreshold;
bool device_fourgbdone = false;
bool fourgb;

void device_fourgb(void) {

	if (!device_fourgbdone) {
		totalmemk = totalram_pages << (PAGE_SHIFT - 10);
	
		if (totalmemk > 5000000) {
			fourgb = false; /* 6GB device */
			trigthreshold = 15;
		} else {
			fourgb = true; /* 4GB device */
			trigthreshold = 20;
		}

		device_fourgbdone = true;
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

	device_fourgb();
	availmem_prober();

	/* Decide voting */
	if (!charging_detected()) {
		if (mem_avail_perc < trigthreshold) /* low available ram when not charging */
			vote = true;
		else
			vote = false; /* stop swapping when enough available ram */
	} else {
		if (batt_swap_push && (mem_avail_perc < trigthreshold)) /* Battery > 80 % and low available ram with charging ON  */
			vote = true;
		else
			vote = false; /* Allow charging faster by keeping swapping off and thus less cpu usage */
	}

	if (adreno_load_perc > GPULOADTRIGGER) { /* High GPU usage - typically while gaming */
		if (fourgb)
			vote = true; /* big games need more ram so zram swapping is beneficial on 4gb devices */
		else
			vote = false;
	}

	if (low_batt_swap_stall) /* Battery below 25% */
		vote = false;
		
	if (vote) {
		if (fourgb) {
			agni_swappiness = 30;
		} else {
			agni_swappiness = 15;
		}
		if (fourgb && (mem_avail_perc < 10))
			mm_drop_caches(3);
	} else {
		agni_swappiness = 1;
	}

	return vote;
}

void agni_memprobe(void) {

	triggerswapping = agni_memprober();
}

