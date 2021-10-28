/* Copyright (c) 2010-2018, 2021, The Linux Foundation. All rights reserved.
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

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/clk.h>
#include <linux/of_platform.h>
#include <linux/workqueue.h>
#include <linux/mdss_io_util.h>
#include <media/cec.h>

#include "mdss_hdmi_util.h"
#include "mdss_hdmi_cec.h"

#define TRACE		DEV_DBG

#define DRV_NAME	"msm-cec"

#define RETRANSMIT_MAX_NUM	5

#define CEC_STATUS_WR_ERROR		BIT(0)
#define CEC_STATUS_WR_DONE		BIT(1)
#define CEC_INTR			(BIT(1) | BIT(3) | BIT(7))

#define CEC_SUPPORTED_HW_VERSION	0x30000001


#define HDMI_CEC_CTRL_ENABLE		BIT(0)
#define HDMI_CEC_CTRL_SEND		BIT(1)
#define HDMI_CEC_CTRL_LINE_OE		BIT(9)

#define HDMI_CEC_STATUS_INUSE		BIT(0)
#define HDMI_CEC_STATUS_FRAME_DONE	BIT(3)
#define HDMI_CEC_STATUS_NO_ACK		BIT(4)
#define HDMI_CEC_STATUS_RD_INVALID	BIT(5)
#define HDMI_CEC_STATUS_ARB_FAIL	BIT(6)

#define HDMI_CEC_INT_WR_DONE		BIT(0)
#define HDMI_CEC_INT_WR_DONE_EN		BIT(1)
#define HDMI_CEC_INT_ERR		BIT(2)
#define HDMI_CEC_INT_ERR_EN		BIT(3)
#define HDMI_CEC_INT_MONITOR		BIT(4)
#define HDMI_CEC_INT_MONITOR_EN		BIT(5)
#define HDMI_CEC_INT_RD_DONE		BIT(6)
#define HDMI_CEC_INT_RD_DONE_EN		BIT(7)

struct hdmi_cec_ctrl {
	struct cec_adapter  *adap;
	struct device       *parent;
	struct dss_io_data  *io;
	bool                enabled;
	bool                addressed;
	bool                cec_wakeup_en;
	struct workqueue_struct *workqueue;
	struct work_struct  cec_read_work;
	struct work_struct  cec_write_work;
	struct cec_msg      rx_msg;
	struct cec_msg      tx_msg;
};

static void hdmi_cec_receive_msg_work(struct work_struct *work)
{
	struct hdmi_cec_ctrl *cec_ctrl;
	int i = 0;
	u8 frame_size;
	u32 data;

	cec_ctrl = container_of(work, struct hdmi_cec_ctrl, cec_read_work);
	if (!cec_ctrl->enabled) {
		DEV_ERR("%s: CEC HW is not enabled\n", __func__);
		return;
	}

	data = DSS_REG_R_ND(cec_ctrl->io, HDMI_CEC_RD_DATA);

	frame_size = (data & 0x1F00) >> 8;
	if (frame_size < 0 || frame_size > CEC_MAX_MSG_SIZE) {
		DEV_ERR("%s: invalid message (frame length = %d)\n",
			__func__, frame_size);
		return;
	}

	cec_ctrl->rx_msg.len = frame_size;
	cec_ctrl->rx_msg.msg[i++] = data & 0xFF;

	for (; i < frame_size; ++i)
		cec_ctrl->rx_msg.msg[i] =
		DSS_REG_R_ND(cec_ctrl->io, HDMI_CEC_RD_DATA) & 0xFF;

	cec_received_msg(cec_ctrl->adap, &cec_ctrl->rx_msg);
}

static void hdmi_cec_send_msg_work(struct work_struct *work)
{
	int i, line_check_retry = 10;
	struct cec_msg *msg;
	bool frame_type;
	struct hdmi_cec_ctrl *cec_ctrl;
	u8 dest;

	cec_ctrl = container_of(work, struct hdmi_cec_ctrl, cec_write_work);
	if (!cec_ctrl->enabled) {
		DEV_ERR("%s: CEC HW is not enabled\n", __func__);
		return;
	}

	msg = &cec_ctrl->tx_msg;
	dest = msg->msg[0] & 0xF;
	frame_type = (dest == 15 ? 1 : 0);

	/* toggle CEC in order to flush out HW state */
	DSS_REG_W(cec_ctrl->io, HDMI_CEC_CTRL, 0);
	DSS_REG_W(cec_ctrl->io, HDMI_CEC_CTRL, HDMI_CEC_CTRL_ENABLE);

	for (i = 0; i < msg->len; ++i)
		DSS_REG_W_ND(cec_ctrl->io, HDMI_CEC_WR_DATA,
			(msg->msg[i] << 8) | frame_type);

	while ((DSS_REG_R(cec_ctrl->io, HDMI_CEC_STATUS) &
		HDMI_CEC_STATUS_INUSE) && line_check_retry) {
		--line_check_retry;
		DEV_DBG("%s: CEC line is busy(%d)\n",
			__func__, line_check_retry);
		schedule();
	}

	if (!line_check_retry) {
		DEV_DBG("%s: Failed to send message - CEC line is busy\n",
			__func__, line_check_retry);
		cec_transmit_done(cec_ctrl->adap, CEC_TX_STATUS_MAX_RETRIES,
				  0, 0, 0, 1);
		return;
	}

	/* start transmission */
	DSS_REG_W(cec_ctrl->io, HDMI_CEC_CTRL, (cec_ctrl->tx_msg.len << 4) |
		  HDMI_CEC_CTRL_ENABLE | HDMI_CEC_CTRL_SEND |
		  HDMI_CEC_CTRL_LINE_OE);
}

static void hdmi_cec_handle_tx_done(struct hdmi_cec_ctrl *cec_ctrl,
				    u32 intr, u32 status)
{
	if ((intr & HDMI_CEC_INT_WR_DONE) && (intr & HDMI_CEC_INT_WR_DONE_EN)) {
		DEV_DBG("%s: CEC_IRQ_FRAME_WR_DONE\n", __func__);
		DSS_REG_W(cec_ctrl->io, HDMI_CEC_INT,
			  intr | HDMI_CEC_INT_WR_DONE);

		cec_transmit_done(cec_ctrl->adap, CEC_TX_STATUS_OK, 0, 0, 0, 0);

	} else if ((intr & HDMI_CEC_INT_ERR) && (intr & HDMI_CEC_INT_ERR_EN)) {
		DEV_DBG("%s: CEC_IRQ_FRAME_ERROR\n", __func__);
		DSS_REG_W(cec_ctrl->io, HDMI_CEC_INT, intr | BIT(2));

		/* refine error code for tx error */
		DEV_DBG("%s: CEC_STATUS: %02x\n", __func__, status);

		if (status & HDMI_CEC_STATUS_NO_ACK) {
			cec_transmit_done(cec_ctrl->adap,
					  CEC_TX_STATUS_MAX_RETRIES |
					  CEC_TX_STATUS_NACK,
					  0, 1, 0, 0);
		} else {
			cec_transmit_done(cec_ctrl->adap, CEC_TX_STATUS_ERROR,
					  0, 0, 0, 1);
		}
	}
}

static void hdmi_cec_handle_rx_done(struct hdmi_cec_ctrl *cec_ctrl,
				    u32 intr, u32 status)
{
	if ((intr & HDMI_CEC_INT_RD_DONE) && (intr & HDMI_CEC_INT_RD_DONE_EN)) {
		DEV_DBG("%s: CEC_IRQ_FRAME_RD_DONE\n", __func__);

		DSS_REG_W(cec_ctrl->io, HDMI_CEC_INT,
			  intr | HDMI_CEC_INT_RD_DONE);

		queue_work(cec_ctrl->workqueue, &cec_ctrl->cec_read_work);
	} /* handle rx error code */
}

int hdmi_cec_isr(void *data)
{
	u32 cec_intr, cec_status;
	struct hdmi_cec_ctrl *cec_ctrl = data;

	if (!cec_ctrl) {
		DEV_ERR("%s: invalid cec ctrl\n", __func__);
		return -EPERM;
	}

	if (!cec_ctrl->enabled) {
		DEV_DBG("%s: CEC hardware is not enabled\n", __func__);
		return 0;
	}

	cec_intr = DSS_REG_R(cec_ctrl->io, HDMI_CEC_INT);
	cec_status = DSS_REG_R(cec_ctrl->io, HDMI_CEC_STATUS);

	hdmi_cec_handle_tx_done(cec_ctrl, cec_intr, cec_status);
	hdmi_cec_handle_rx_done(cec_ctrl, cec_intr, cec_status);

	return 0;
}

void hdmi_cec_device_suspend(void *input, bool suspend)
{
	struct hdmi_cec_ctrl *cec_ctrl = (struct hdmi_cec_ctrl *)input;

	if (!cec_ctrl) {
		DEV_WARN("%s: HDMI CEC HW module not initialized.\n", __func__);
		return;
	}
}

bool hdmi_cec_is_wakeup_en(void *input)
{
	struct hdmi_cec_ctrl *cec_ctrl = (struct hdmi_cec_ctrl *)input;

	if (!cec_ctrl) {
		DEV_WARN("%s: HDMI CEC HW module not initialized.\n", __func__);
		return false;
	}

	return cec_ctrl->cec_wakeup_en;
}


static int hdmi_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	int ret = 0;
	struct hdmi_cec_ctrl *cec_ctrl = adap->priv;
	u32 hdmi_hw_version, reg_val;

	if (enable) {
		/* 19.2Mhz * 0.00005 us = 950 = 0x3B6 */
		DSS_REG_W(cec_ctrl->io, HDMI_CEC_REFTIMER,
			  (0x3B6 & 0xFFF) | BIT(16));

		hdmi_hw_version = DSS_REG_R(cec_ctrl->io, HDMI_VERSION);
		if (hdmi_hw_version >= CEC_SUPPORTED_HW_VERSION) {
			DSS_REG_W(cec_ctrl->io, HDMI_CEC_RD_RANGE, 0x30AB9888);
			DSS_REG_W(cec_ctrl->io, HDMI_CEC_WR_RANGE, 0x888AA888);

			DSS_REG_W(cec_ctrl->io, HDMI_CEC_RD_START_RANGE,
				  0x88888888);
			DSS_REG_W(cec_ctrl->io, HDMI_CEC_RD_TOTAL_RANGE, 0x99);
			DSS_REG_W(cec_ctrl->io, HDMI_CEC_COMPL_CTL, 0xF);
			DSS_REG_W(cec_ctrl->io, HDMI_CEC_WR_CHECK_CONFIG, 0x4);
		} else {
			DEV_DBG("%s: CEC version %d is not supported.\n",
				__func__, hdmi_hw_version);
			ret = -EPERM;
			goto end;
		}

		DSS_REG_W(cec_ctrl->io, HDMI_CEC_RD_FILTER,
			  BIT(0) | (0x7FF << 4));
		DSS_REG_W(cec_ctrl->io, HDMI_CEC_TIME,
			  BIT(0) | ((7 * 0x30) << 7));

		/* Enable CEC interrupts */
		DSS_REG_W(cec_ctrl->io, HDMI_CEC_INT,
			  HDMI_CEC_INT_WR_DONE_EN |
			  HDMI_CEC_INT_ERR_EN |
			  HDMI_CEC_INT_RD_DONE_EN);

		/* Enable Engine */
		DSS_REG_W(cec_ctrl->io, HDMI_CEC_CTRL, HDMI_CEC_CTRL_ENABLE);
	} else {
		/* Disable Engine */
		DSS_REG_W(cec_ctrl->io, HDMI_CEC_CTRL, 0);

		/* Disable CEC interrupts */
		reg_val = DSS_REG_R(cec_ctrl->io, HDMI_CEC_INT);
		DSS_REG_W(cec_ctrl->io, HDMI_CEC_INT, reg_val & ~CEC_INTR);
	}

	cec_ctrl->enabled = enable;

end:
	return ret;
}

static int hdmi_cec_adap_log_addr(struct cec_adapter *adap, u8 logical_addr)
{
	struct hdmi_cec_ctrl *cec_ctrl = adap->priv;

	if (logical_addr == CEC_LOG_ADDR_INVALID)
		return 0;

	/* need to check if gating with enabled flag is really necessary */
	if (cec_ctrl->enabled)
		DSS_REG_W(cec_ctrl->io, HDMI_CEC_ADDR, logical_addr & 0xF);

	return 0;
}

static int hdmi_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				  u32 signal_free_time, struct cec_msg *msg)
{
	struct hdmi_cec_ctrl *cec_ctrl = adap->priv;

	cec_ctrl->tx_msg = *msg;

	queue_work(cec_ctrl->workqueue, &cec_ctrl->cec_write_work);

	return 0;
}

static const struct cec_adap_ops mdss_hdmi_cec_adap_ops = {
	.adap_enable = hdmi_cec_adap_enable,
	.adap_log_addr = hdmi_cec_adap_log_addr,
	.adap_transmit = hdmi_cec_adap_transmit,
};

static ssize_t hdmi_cec_rda_wakeup_enable(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	struct cec_adapter  *adap = dev_get_drvdata(dev);
	struct hdmi_cec_ctrl *ctl = adap->priv;

	if (!ctl) {
		pr_err("invalid cec ctl\n");
		return -EINVAL;
	}

	if (ctl->cec_wakeup_en) {
		pr_debug("cec_wakeup is enabled\n");
		ret = snprintf(buf, PAGE_SIZE, "%d\n", 1);
	} else {
		pr_debug("cec_wakeup is disabled\n");
		ret = snprintf(buf, PAGE_SIZE, "%d\n", 0);
	}

	return ret;
}

static ssize_t hdmi_cec_wta_wakeup_enable(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int val;
	size_t ret;
	struct cec_adapter  *adap = dev_get_drvdata(dev);
	struct hdmi_cec_ctrl *ctl = adap->priv;

	if (!ctl) {
		pr_err("invalid cec ctl\n");
		return -EINVAL;
	}

	ret = kstrtoint(buf, 10, &val);
	if (ret) {
		pr_err("kstrtoint failed.\n");
		return -EINVAL;
	}

	if (val > 0) {
		ctl->cec_wakeup_en = true;
	} else if (val == 0) {
		ctl->cec_wakeup_en = false;
	} else {
		pr_err("invalid parameter\n");
		return -EINVAL;
	}

	return count;
}

static DEVICE_ATTR(wakeup_enable, 0644, hdmi_cec_rda_wakeup_enable,
	hdmi_cec_wta_wakeup_enable);

void *hdmi_cec_init(struct hdmi_cec_init_data *init_data)
{
	struct hdmi_cec_ctrl *cec_ctrl;
	int ret = 0;

	if (!init_data->io) {
		DEV_ERR("%s: invalid cec ctrl\n", __func__);
		ret = -EINVAL;
		goto err_alloc;
	}

	cec_ctrl = kzalloc(sizeof(struct hdmi_cec_ctrl), GFP_KERNEL);
	if (!cec_ctrl) {
		DEV_ERR("%s: FAILED: out of memory\n", __func__);
		ret = -EINVAL;
		goto err_alloc;
	}

	cec_ctrl->io = init_data->io;
	cec_ctrl->workqueue = init_data->workq;

	cec_ctrl->adap = cec_allocate_adapter(&mdss_hdmi_cec_adap_ops, cec_ctrl,
					      DRV_NAME, CEC_CAP_DEFAULTS |
					      CEC_CAP_PHYS_ADDR,
					      CEC_MAX_LOG_ADDRS);
	ret = PTR_ERR_OR_ZERO(cec_ctrl->adap);
	if (ret < 0) {
		DEV_ERR("%s: Failed to allocate CEC adapter\n",
			__func__);
		goto err_allocate_adapter;
	}

	cec_ctrl->parent = init_data->dev;

	INIT_WORK(&cec_ctrl->cec_read_work, hdmi_cec_receive_msg_work);
	INIT_WORK(&cec_ctrl->cec_write_work, hdmi_cec_send_msg_work);

	ret = cec_register_adapter(cec_ctrl->adap, cec_ctrl->parent);
	if (ret < 0) {
		DEV_ERR("%s: Failed to register CEC adapter\n",
			__func__);
		goto err_register_adapter;
	}

	ret = sysfs_add_file_to_group(&cec_ctrl->adap->devnode.dev.kobj,
				      &dev_attr_wakeup_enable.attr, NULL);
	if (ret < 0) {
		DEV_ERR("%s: Fail to add sysfs for CEC adapter\n", __func__);
		goto err_sysfs_file;
	}

	return cec_ctrl;

err_sysfs_file:
	cec_unregister_adapter(cec_ctrl->adap);
	goto err_allocate_adapter;	// skip cec_delete_adapter
err_register_adapter:
	cec_delete_adapter(cec_ctrl->adap);
err_allocate_adapter:
	kfree(cec_ctrl);
err_alloc:
	return ERR_PTR(ret);
}

void hdmi_cec_deinit(void *data)
{
	struct hdmi_cec_ctrl *cec_ctrl = data;

	sysfs_remove_file_from_group(&cec_ctrl->adap->devnode.dev.kobj,
				     &dev_attr_wakeup_enable.attr, NULL);
	cec_unregister_adapter(cec_ctrl->adap);
	kfree(cec_ctrl);
}
