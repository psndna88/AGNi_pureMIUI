/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * DOC: wlan_cm_roam_ucfg_api.h
 *
 * Implementation for roaming public ucfg API interfaces.
 */

#ifndef _WLAN_CM_ROAM_UCFG_API_H_
#define _WLAN_CM_ROAM_UCFG_API_H_

#include "wlan_cm_roam_api.h"

/**
 * ucfg_user_space_enable_disable_rso() - Enable/Disable Roam Scan offload
 * to firmware.
 * @pdev: Pointer to pdev
 * @vdev_id: vdev id
 * @is_fast_roam_enabled: Value provided by userspace.
 * is_fast_roam_enabled - true: enable RSO if FastRoamEnabled ini is enabled
 *                        false: disable RSO
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
ucfg_user_space_enable_disable_rso(struct wlan_objmgr_pdev *pdev,
				   uint8_t vdev_id,
				   const bool is_fast_roam_enabled);

/**
 * ucfg_is_roaming_enabled() - Check if roaming enabled
 * to firmware.
 * @psoc: psoc context
 * @vdev_id: vdev id
 *
 * Return: True if Roam state machine is in
 *	   WLAN_ROAM_RSO_ENABLED/WLAN_ROAMING_IN_PROG/WLAN_ROAM_SYNCH_IN_PROG
 */
bool ucfg_is_roaming_enabled(struct wlan_objmgr_pdev *pdev, uint8_t vdev_id);

/*
 * ucfg_cm_abort_roam_scan() -abort current roam scan cycle by roam scan
 * offload module.
 * @pdev: Pointer to pdev
 * vdev_id - vdev Identifier
 *
 * Return QDF_STATUS
 */
QDF_STATUS ucfg_cm_abort_roam_scan(struct wlan_objmgr_pdev *pdev,
				   uint8_t vdev_id);

/**
 * ucfg_cm_rso_set_roam_trigger() - Send roam trigger bitmap firmware
 * @pdev: Pointer to pdev
 * @vdev_id: vdev id
 * @trigger: Carries pointer of the object containing vdev id and
 *  roam_trigger_bitmap.
 *
 * Return: QDF_STATUS
 */
static inline QDF_STATUS
ucfg_cm_rso_set_roam_trigger(struct wlan_objmgr_pdev *pdev, uint8_t vdev_id,
			     struct wlan_roam_triggers *trigger)
{
	return wlan_cm_rso_set_roam_trigger(pdev, vdev_id, trigger);
}

/**
 * ucfg_cm_disable_rso() - Disable roam scan offload to firmware
 * @pdev: Pointer to pdev
 * @vdev_id: vdev id
 * @requestor: RSO disable requestor
 * @reason: Reason for RSO disable
 *
 * Return:  QDF_STATUS
 */
static inline
QDF_STATUS ucfg_cm_disable_rso(struct wlan_objmgr_pdev *pdev, uint8_t vdev_id,
			       enum wlan_cm_rso_control_requestor requestor,
			       uint8_t reason)
{
	return wlan_cm_disable_rso(pdev, vdev_id, requestor, reason);
}

/**
 * ucfg_cm_enable_rso() - Enable roam scan offload to firmware
 * @pdev: Pointer to pdev
 * @vdev_id: vdev id
 * @requestor: RSO disable requestor
 * @reason: Reason for RSO disable
 *
 * Return:  QDF_STATUS
 */
static inline
QDF_STATUS ucfg_cm_enable_rso(struct wlan_objmgr_pdev *pdev, uint8_t vdev_id,
			      enum wlan_cm_rso_control_requestor requestor,
			      uint8_t reason)
{
	return wlan_cm_enable_rso(pdev, vdev_id, requestor, reason);
}

/**
 * ucfg_cm_abort_rso() - Enable roam scan offload to firmware
 * @pdev: Pointer to pdev
 * @vdev_id: vdev id
 *
 * Returns:
 * QDF_STATUS_E_BUSY if roam_synch is in progress and upper layer has to wait
 *                   before RSO stop cmd can be issued;
 * QDF_STATUS_SUCCESS if roam_synch is not outstanding. RSO stop cmd will be
 *                    issued with the global SME lock held in this case, and
 *                    uppler layer doesn't have to do any wait.
 */
static inline
QDF_STATUS ucfg_cm_abort_rso(struct wlan_objmgr_pdev *pdev, uint8_t vdev_id)
{
	return wlan_cm_abort_rso(pdev, vdev_id);
}

/**
 * ucfg_cm_roaming_in_progress() - check if roaming is in progress
 * @pdev: Pointer to pdev
 * @vdev_id: vdev id
 *
 * Return: true or false
 */
static inline bool
ucfg_cm_roaming_in_progress(struct wlan_objmgr_pdev *pdev, uint8_t vdev_id)
{
	return wlan_cm_roaming_in_progress(pdev, vdev_id);
}

#endif

#ifdef WLAN_FEATURE_ROAM_OFFLOAD
static inline QDF_STATUS
ucfg_cm_update_roam_scan_scheme_bitmap(struct wlan_objmgr_psoc *psoc,
				       uint8_t vdev_id,
				       uint32_t roam_scan_scheme_bitmap)
{
	return wlan_cm_update_roam_scan_scheme_bitmap(psoc, vdev_id,
						      roam_scan_scheme_bitmap);
}

static inline QDF_STATUS
ucfg_cm_update_roam_rt_stats(struct wlan_objmgr_psoc *psoc,
			     uint8_t value, enum roam_rt_stats_params stats)
{
	return wlan_cm_update_roam_rt_stats(psoc, value, stats);
}

static inline uint8_t
ucfg_cm_get_roam_rt_stats(struct wlan_objmgr_psoc *psoc,
			  enum roam_rt_stats_params stats)
{
	return wlan_cm_get_roam_rt_stats(psoc, stats);
}

/**
 * ucfg_cm_roam_send_rt_stats_config() - Enable/Disable Roam event stats from FW
 * @pdev: Pointer to pdev
 * @vdev_id: vdev id
 * @param_value: Value set based on the userspace attributes.
 * param_value - 0: if configure attribute is 0
 *               1: if configure is 1 and suspend_state is not set
 *               3: if configure is 1 and suspend_state is set
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
ucfg_cm_roam_send_rt_stats_config(struct wlan_objmgr_pdev *pdev,
				  uint8_t vdev_id, uint8_t param_value);

/**
 * ucfg_cm_roam_send_ho_delay_config() - Send the HO delay value to Firmware
 * @pdev: Pointer to pdev
 * @vdev_id: vdev id
 * @param_value: Value will be from range 20 to 1000 in msec.
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
ucfg_cm_roam_send_ho_delay_config(struct wlan_objmgr_pdev *pdev,
				  uint8_t vdev_id, uint16_t param_value);

/**
 * ucfg_cm_exclude_rm_partial_scan_freq() - Exclude the channels in roam full
 * scan that are already scanned as part of partial scan.
 * @pdev: Pointer to pdev
 * @vdev_id: vdev id
 * @param_value: Include/exclude the partial scan channel in roam full scan
 * 1 - Exclude
 * 0 - Include
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
ucfg_cm_exclude_rm_partial_scan_freq(struct wlan_objmgr_pdev *pdev,
				     uint8_t vdev_id, uint8_t param_value);

/**
 * ucfg_cm_roam_full_scan_6ghz_on_disc() - Include the 6 GHz channels in roam
 * full scan only on prior discovery of any 6 GHz support in the environment.
 * @pdev: Pointer to pdev
 * @vdev_id: vdev id
 * @param_value: Include the 6 GHz channels in roam full scan:
 * 1 - Include only on prior discovery of any 6 GHz support in the environment
 * 0 - Include all the supported 6 GHz channels by default
 *
 * Return: QDF_STATUS
 */
QDF_STATUS ucfg_cm_roam_full_scan_6ghz_on_disc(struct wlan_objmgr_pdev *pdev,
					       uint8_t vdev_id,
					       uint8_t param_value);

/**
 * ucfg_cm_set_roam_scan_high_rssi_offset() - Set the delta change in high RSSI
 * at which roam scan is triggered in 2.4/5 GHz.
 * @psoc: Pointer to psoc object
 * @vdev_id: vdev id
 * @param_value: Set the High RSSI delta for roam scan trigger
 * 0    - Disable
 * 1-16 - Set an offset value in this range
 *
 * Return: QDF_STATUS
 */
static inline QDF_STATUS
ucfg_cm_set_roam_scan_high_rssi_offset(struct wlan_objmgr_psoc *psoc,
				       uint8_t vdev_id, uint8_t param_value)
{
	return cm_set_roam_scan_high_rssi_offset(psoc, vdev_id, param_value);
}
#else
static inline QDF_STATUS
ucfg_cm_update_roam_scan_scheme_bitmap(struct wlan_objmgr_psoc *psoc,
				       uint8_t vdev_id,
				       uint32_t roam_scan_scheme_bitmap)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
ucfg_cm_update_roam_rt_stats(struct wlan_objmgr_psoc *psoc,
			     uint8_t value, enum roam_rt_stats_params stats)
{
	return QDF_STATUS_SUCCESS;
}

static inline uint8_t
ucfg_cm_get_roam_rt_stats(struct wlan_objmgr_psoc *psoc,
			  enum roam_rt_stats_params stats)
{
	return 0;
}

static inline QDF_STATUS
ucfg_cm_roam_send_rt_stats_config(struct wlan_objmgr_pdev *pdev,
				  uint8_t vdev_id, uint8_t param_value)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
ucfg_cm_roam_send_ho_delay_config(struct wlan_objmgr_pdev *pdev,
				  uint8_t vdev_id, uint16_t param_value)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
ucfg_cm_exclude_rm_partial_scan_freq(struct wlan_objmgr_pdev *pdev,
				     uint8_t vdev_id, uint8_t param_value)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
ucfg_cm_roam_full_scan_6ghz_on_disc(struct wlan_objmgr_pdev *pdev,
				    uint8_t vdev_id, uint8_t param_value)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
ucfg_cm_set_roam_scan_high_rssi_offset(struct wlan_objmgr_psoc *psoc,
				       uint8_t vdev_id, uint8_t param_value)
{
	return QDF_STATUS_SUCCESS;
}
#endif
