/* Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
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

#ifndef _CNSS_REG_H
#define _CNSS_REG_H

#define QCA6490_DEBUG_PBL_LOG_SRAM_START 0x1403D58
#define QCA6490_DEBUG_PBL_LOG_SRAM_MAX_SIZE 40
#define QCA6490_V1_SBL_DATA_START 0x143b000
#define QCA6490_V1_SBL_DATA_END (0x143b000 + 0x00011000)
#define QCA6490_V2_SBL_DATA_START 0x1435000
#define QCA6490_V2_SBL_DATA_END (0x1435000 + 0x00011000)
#define QCA6490_DEBUG_SBL_LOG_SRAM_MAX_SIZE 48
#define QCA6490_TCSR_PBL_LOGGING_REG 0x01B000F8
#define QCA6490_PCIE_BHI_ERRDBG2_REG 0x01E0E238
#define QCA6490_PCIE_BHI_ERRDBG3_REG 0x01E0E23C
#define QCA6490_PBL_WLAN_BOOT_CFG 0x01E22B34
#define QCA6490_PBL_BOOTSTRAP_STATUS 0x01910008

#define QCA6390_DEBUG_PBL_LOG_SRAM_START 0x01403D58
#define QCA6390_DEBUG_PBL_LOG_SRAM_MAX_SIZE 80
#define QCA6390_V2_SBL_DATA_START 0x016c8580
#define QCA6390_V2_SBL_DATA_END (0x016c8580 + 0x00011000)
#define QCA6390_DEBUG_SBL_LOG_SRAM_MAX_SIZE 44
#define QCA6390_TCSR_PBL_LOGGING_REG 0x01B000F8
#define QCA6390_PCIE_BHI_ERRDBG2_REG 0x01E0E238
#define QCA6390_PCIE_BHI_ERRDBG3_REG 0x01E0E23C
#define QCA6390_PBL_WLAN_BOOT_CFG    0x01E22B34
#define QCA6390_PBL_BOOTSTRAP_STATUS 0x01910008
#endif
