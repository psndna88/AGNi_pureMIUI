/*
 * AGNi Memory Prober
 * (c) Parvinder Singh (psndna88@gmail.com)
 */

#ifndef _LINUX_AGNI_MEMINFO_H
#define _LINUX_AGNI_MEMINFO_H

extern unsigned long totalram_pages;
extern long si_mem_available(void);
extern bool triggerswapping;
extern bool agni_memprober(void);
extern void agni_memprobe(void);
extern unsigned long zram_ram_usage;
extern bool batt_swap_push;
extern bool low_batt_swap_stall;
#define GPULOADTRIGGER 75 /* gpu load % threshold */
extern int agni_swappiness;
extern void mm_drop_caches(int val);
extern unsigned long adreno_load_perc;

#endif /* _LINUX_AGNI_MEMINFO_H */
