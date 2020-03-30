/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef _CAM_SFE_HW_INTF_H_
#define _CAM_SFE_HW_INTF_H_

#include "cam_isp_hw.h"

#define CAM_SFE_HW_NUM_MAX            2
#define SFE_CORE_BASE_IDX             0

enum cam_isp_hw_sfe_in {
	CAM_ISP_HW_SFE_IN_PIX,
	CAM_ISP_HW_SFE_IN_RD0,
	CAM_ISP_HW_SFE_IN_RD1,
	CAM_ISP_HW_SFE_IN_RD2,
	CAM_ISP_HW_SFE_IN_RDI0,
	CAM_ISP_HW_SFE_IN_RDI1,
	CAM_ISP_HW_SFE_IN_RDI2,
	CAM_ISP_HW_SFE_IN_RDI3,
	CAM_ISP_HW_SFE_IN_RDI4,
	CAM_ISP_HW_SFE_IN_MAX,
};

enum cam_sfe_hw_irq_status {
	CAM_SFE_IRQ_STATUS_SUCCESS,
	CAM_SFE_IRQ_STATUS_ERR,
	CAM_SFE_IRQ_STATUS_OVERFLOW,
	CAM_SFE_IRQ_STATUS_VIOLATION,
	CAM_SFE_IRQ_STATUS_MAX,
};

/*
 * struct cam_sfe_hw_get_hw_cap:
 *
 * @reserved_1: reserved
 * @reserved_2: reserved
 * @reserved_3: reserved
 * @reserved_4: reserved
 */
struct cam_sfe_hw_get_hw_cap {
	uint32_t reserved_1;
	uint32_t reserved_2;
	uint32_t reserved_3;
	uint32_t reserved_4;
};

/*
 * struct cam_sfe_hw_vfe_bus_rd_acquire_args:
 *
 * @rsrc_node:               Pointer to Resource Node object, filled if acquire
 *                           is successful
 * @res_id:                  Unique Identity of port to associate with this
 *                           resource.
 * @is_dual:                 Flag to indicate dual SFE usecase
 * @cdm_ops:                 CDM operations
 * @unpacket_fmt:            Unpacker format for read engine
 * @is_offline:              Flag to indicate offline usecase
 */
struct cam_sfe_hw_sfe_bus_rd_acquire_args {
	struct cam_isp_resource_node         *rsrc_node;
	uint32_t                              res_id;
	uint32_t                              is_dual;
	struct cam_cdm_utils_ops             *cdm_ops;
	uint32_t                              unpacker_fmt;
	bool                                  is_offline;
};

/*
 * struct cam_sfe_hw_sfe_bus_in_acquire_args:
 *
 * @rsrc_node:               Pointer to Resource Node object, filled if acquire
 *                           is successful
 * @res_id:                  Unique Identity of port to associate with this
 *                           resource.
 * @cdm_ops:                 CDM operations
 * @is_dual:                 Dual mode usecase
 * @sync_mode:               If in dual mode, indicates master/slave
 * @in_port:                 in port info
 * @is_fe_enabled:           Flag to indicate if FE is enabled
 * @is_offline:              Flag to indicate Offline IFE
 */
struct cam_sfe_hw_sfe_in_acquire_args {
	struct cam_isp_resource_node         *rsrc_node;
	uint32_t                              res_id;
	struct cam_cdm_utils_ops             *cdm_ops;
	uint32_t                              is_dual;
	enum cam_isp_hw_sync_mode             sync_mode;
	struct cam_isp_in_port_generic_info  *in_port;
	bool                                  is_fe_enabled;
	bool                                  is_offline;
};

/*
 * struct cam_sfe_hw_sfe_out_acquire_args:
 *
 * @rsrc_node:               Pointer to Resource Node object, filled if acquire
 *                           is successful
 * @out_port_info:           Output Port details to acquire
 * @unique_id:               Unique Identity of Context to associate with this
 *                           resource. Used for composite grouping of multiple
 *                           resources in the same context
 * @is_dual:                 Dual SFE or not
 * @split_id:                In case of Dual SFE, this is Left or Right.
 * @is_master:               In case of Dual SFE, this is Master or Slave.
 * @cdm_ops:                 CDM operations
 */
struct cam_sfe_hw_sfe_out_acquire_args {
	struct cam_isp_resource_node         *rsrc_node;
	struct cam_isp_out_port_generic_info *out_port_info;
	uint32_t                              unique_id;
	uint32_t                              is_dual;
	enum cam_isp_hw_split_id              split_id;
	uint32_t                              is_master;
	struct cam_cdm_utils_ops             *cdm_ops;
};

/*
 * struct cam_sfe_acquire_args:
 *
 * @rsrc_type:               Type of Resource (OUT/IN) to acquire
 * @tasklet:                 Tasklet to associate with this resource. This is
 *                           used to schedule bottom of IRQ events associated
 *                           with this resource.
 * @priv:                    Context data
 * @event_cb:                Callback function to hw mgr in case of hw events
 * @sfe_out:                 Acquire args for SFE_OUT
 * @sfe_bus_rd               Acquire args for SFE_BUS_READ
 * @sfe_in:                  Acquire args for SFE_IN
 */
struct cam_sfe_acquire_args {
	enum cam_isp_resource_type           rsrc_type;
	void                                *tasklet;
	void                                *priv;
	cam_hw_mgr_event_cb_func             event_cb;
	union {
		struct cam_sfe_hw_sfe_out_acquire_args     sfe_out;
		struct cam_sfe_hw_sfe_in_acquire_args      sfe_in;
		struct cam_sfe_hw_sfe_bus_rd_acquire_args  sfe_rd;
	};
};

/*
 * cam_sfe_hw_init()
 *
 * @Brief:                  Initialize SFE HW device
 *
 * @sfe_hw:                 sfe_hw interface to fill in and return on
 *                          successful initialization
 * @hw_idx:                 Index of SFE HW
 */
int cam_sfe_hw_init(struct cam_hw_intf **sfe_hw, uint32_t hw_idx);

#endif /* _CAM_SFE_HW_INTF_H_ */

