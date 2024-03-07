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

/**
 *  DOC: wlan_cm_tgt_if_tx_api.h
 *
 *  This file contains connection manager tx ops related functions
 */

#ifndef CM_TGT_IF_TX_API_H__
#define CM_TGT_IF_TX_API_H__

#include "wlan_cm_roam_public_struct.h"

#ifdef WLAN_FEATURE_ROAM_OFFLOAD
/**
 * wlan_cm_roam_send_set_vdev_pcl()  - Send vdev set pcl command to firmware
 * @psoc:     PSOC pointer
 * @pcl_req:  Set pcl request structure pointer
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_cm_roam_send_set_vdev_pcl(struct wlan_objmgr_psoc *psoc,
			       struct set_pcl_req *pcl_req);

/**
 * wlan_cm_tgt_send_roam_rt_stats_config() - Send roam event stats config
 * command to FW
 * @psoc: psoc pointer
 * @req: roam stats config parameter
 *
 * Return: QDF_STATUS
 */
QDF_STATUS wlan_cm_tgt_send_roam_rt_stats_config(struct wlan_objmgr_psoc *psoc,
						 struct roam_disable_cfg *req);

/**
 * wlan_cm_tgt_send_roam_ho_delay_config() - Send roam HO delay config command
 * to FW
 * @psoc: psoc pointer
 * @vdev_id: vdev id
 * @roam_ho_delay: roam hand-off delay value
 *
 * Return: QDF_STATUS
 */
QDF_STATUS wlan_cm_tgt_send_roam_ho_delay_config(struct wlan_objmgr_psoc *psoc,
						 uint8_t vdev_id,
						 uint16_t roam_ho_delay);

/**
 * wlan_cm_tgt_exclude_rm_partial_scan_freq() - Exclude the channels in roam
 * full scan that are already scanned as part of partial scan.
 * @psoc: psoc pointer
 * @vdev_id: vdev id
 * @exclude_rm_partial_scan_freq: Exclude the channels in roam full scan that
 * are already scanned as part of partial scan.
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_cm_tgt_exclude_rm_partial_scan_freq(struct wlan_objmgr_psoc *psoc,
					 uint8_t vdev_id,
					 uint8_t exclude_rm_partial_scan_freq);

/**
 * wlan_cm_tgt_send_roam_full_scan_6ghz_on_disc() - Include the 6 GHz channels
 * in roam full scan only on prior discovery of any 6 GHz support in the
 * environment.
 * @psoc: PSOC pointer
 * @roam_inc_6ghz_if_disc: Include the 6 GHz channels in roam full scan:
 * 1 - Include only on prior discovery of any 6 GHz support in the environment
 * 0 - Include all the supported 6 GHz channels by default
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_cm_tgt_send_roam_full_scan_6ghz_on_disc(
					struct wlan_objmgr_psoc *psoc,
					uint8_t vdev_id,
					uint8_t roam_full_scan_6ghz_on_disc);

/**
 * wlan_cm_tgt_send_roam_scan_offload_rssi_params() - Set the RSSI parameters
 * for roam offload scan
 * @vdev: Pointer to vdev object
 * @roam_rssi_params: structure containing parameters for roam offload scan
 * based on RSSI
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
wlan_cm_tgt_send_roam_scan_offload_rssi_params(
		struct wlan_objmgr_vdev *vdev,
		struct wlan_roam_offload_scan_rssi_params *roam_rssi_params);
#else
static inline QDF_STATUS
wlan_cm_roam_send_set_vdev_pcl(struct wlan_objmgr_psoc *psoc,
			       struct set_pcl_req *pcl_req)
{
	return QDF_STATUS_E_FAILURE;
}

static inline QDF_STATUS
wlan_cm_tgt_send_roam_rt_stats_config(struct wlan_objmgr_psoc *psoc,
				      struct roam_disable_cfg *req)
{
	return QDF_STATUS_E_FAILURE;
}

static inline QDF_STATUS
wlan_cm_tgt_send_roam_ho_delay_config(struct wlan_objmgr_psoc *psoc,
				      uint8_t vdev_id, uint16_t roam_ho_delay)
{
	return QDF_STATUS_E_FAILURE;
}

static inline QDF_STATUS
wlan_cm_tgt_exclude_rm_partial_scan_freq(struct wlan_objmgr_psoc *psoc,
					 uint8_t vdev_id,
					 uint8_t exclude_rm_partial_scan_freq)
{
	return QDF_STATUS_E_FAILURE;
}

static inline QDF_STATUS
wlan_cm_tgt_send_roam_full_scan_6ghz_on_disc(
					struct wlan_objmgr_psoc *psoc,
					uint8_t vdev_id,
					uint8_t roam_full_scan_6ghz_on_disc)
{
	return QDF_STATUS_E_FAILURE;
}
#endif /* WLAN_FEATURE_ROAM_OFFLOAD */

#if defined(WLAN_FEATURE_HOST_ROAM) || defined(WLAN_FEATURE_ROAM_OFFLOAD)

#define CFG_DISABLE_4WAY_HS_OFFLOAD_DEFAULT BIT(0)

/**
 * wlan_cm_tgt_send_roam_offload_init()  - Send WMI_VDEV_PARAM_ROAM_FW_OFFLOAD
 * to init/deinit roaming module at firmware
 * @psoc: PSOC pointer
 * @vdev_id: vdev id
 * @is_init: true if roam module is to be initialized else false for deinit
 *
 * Return: QDF_STATUS
 */
QDF_STATUS wlan_cm_tgt_send_roam_offload_init(struct wlan_objmgr_psoc *psoc,
					      uint8_t vdev_id, bool is_init);

/**
 * wlan_cm_tgt_send_roam_start_req()  - Send roam start command to firmware
 * @psoc:    psoc pointer
 * @vdev_id: vdev id
 * @req: roam start config parameter
 *
 * Return: QDF_STATUS
 */
QDF_STATUS wlan_cm_tgt_send_roam_start_req(struct wlan_objmgr_psoc *psoc,
					   uint8_t vdev_id,
					   struct wlan_roam_start_config *req);

/**
 * wlan_cm_tgt_send_roam_stop_req()  - Send roam stop command to firmware
 * @psoc:    psoc pointer
 * @vdev_id: vdev id
 * @req: roam stop config parameter
 *
 * Return: QDF_STATUS
 */
QDF_STATUS wlan_cm_tgt_send_roam_stop_req(struct wlan_objmgr_psoc *psoc,
					 uint8_t vdev_id,
					 struct wlan_roam_stop_config *req);

/**
 * wlan_cm_tgt_send_roam_start_req()  - Send roam update command to firmware
 * @psoc:    psoc pointer
 * @vdev_id: vdev id
 * @req: roam update config parameter
 *
 * Return: QDF_STATUS
 */
QDF_STATUS wlan_cm_tgt_send_roam_update_req(struct wlan_objmgr_psoc *psoc,
					   uint8_t vdev_id,
					   struct wlan_roam_update_config *req);

/**
 * wlan_cm_tgt_send_roam_abort_req()  - Send roam abort command to firmware
 * @psoc:    psoc pointer
 * @vdev_id: vdev id
 *
 * Return: QDF_STATUS
 */
QDF_STATUS wlan_cm_tgt_send_roam_abort_req(struct wlan_objmgr_psoc *psoc,
					   uint8_t vdev_id);

/**
 * wlan_cm_tgt_send_roam_per_config()  - Send roam per config command to FW
 * @psoc:    psoc pointer
 * @vdev_id: vdev id
 * @req: per roam config parameter
 *
 * Return: QDF_STATUS
 */
QDF_STATUS wlan_cm_tgt_send_roam_per_config(struct wlan_objmgr_psoc *psoc,
					  uint8_t vdev_id,
					  struct wlan_per_roam_config_req *req);

/**
 * wlan_cm_tgt_send_roam_triggers()  - Send roam trigger command to FW
 * @psoc: psoc pointer
 * @vdev_id: vdev id
 * @req: roam trigger parameter
 *
 * Return: QDF_STATUS
 */
QDF_STATUS wlan_cm_tgt_send_roam_triggers(struct wlan_objmgr_psoc *psoc,
					  uint8_t vdev_id,
					  struct wlan_roam_triggers *req);
#endif

/**
 * wlan_cm_tgt_send_roam_disable_config()  - Send roam disable config command
 * to FW
 * @psoc:    psoc pointer
 * @vdev_id: vdev id
 * @req: roam disable config parameter
 *
 * Return: QDF_STATUS
 */
QDF_STATUS wlan_cm_tgt_send_roam_disable_config(struct wlan_objmgr_psoc *psoc,
						uint8_t vdev_id,
						struct roam_disable_cfg *req);
#endif /* CM_TGT_IF_TX_API_H__ */
