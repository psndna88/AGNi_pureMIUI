/*
 * Copyright (C) 2021 XiaoMi, Inc.
 *               2022 The LineageOS Project
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef __HWID_H__
#define __HWID_H__

#include <linux/types.h>

#define HARDWARE_PROJECT_UNKNOWN    0
#define HARDWARE_PROJECT_J18        1
#define HARDWARE_PROJECT_K1         2
#define HARDWARE_PROJECT_K2         3
#define HARDWARE_PROJECT_K11        4
#define HARDWARE_PROJECT_K1A        5
#define HARDWARE_PROJECT_K9         6
#define HARDWARE_PROJECT_K8         7
#define HARDWARE_PROJECT_K3S        8
#define HARDWARE_PROJECT_J18S       9
#define HARDWARE_PROJECT_K9D        10
#define HARDWARE_PROJECT_K9B        11
#define HARDWARE_PROJECT_K9E        12
#define HARDWARE_PROJECT_L9         13
#define HARDWARE_PROJECT_M20        14

typedef enum {
	CountryCN = 0x00,
	CountryGlobal = 0x01,
	CountryIndia = 0x02,
	CountryJapan = 0x03,
	INVALID = 0x04,
	CountryIDMax = 0x7FFFFFFF
} CountryType;

uint32_t get_hw_version_platform(void);
uint32_t get_hw_id_value(void);
uint32_t get_hw_country_version(void);
uint32_t get_hw_version_major(void);
uint32_t get_hw_version_minor(void);
uint32_t get_hw_version_build(void);

#endif /* __HWID_H__ */
