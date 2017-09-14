/*
 * Sigma Control API DUT - Miracast interface
 * Copyright (c) 2017, Qualcomm Atheros, Inc.
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
*/
#ifndef SIGMA_MIRACAST_H
#define SIGMA_MIRACAST_H

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef ANDROID
#include "properties.h"
#endif /* ANDROID */


struct sigma_dut;
struct sigma_conn;
struct sigma_cmd;

void miracast_init(struct sigma_dut *sigma_dut);

void miracast_deinit(struct sigma_dut *sigma_dut);

void miracast_start_autonomous_go(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd, char *ifname);

int miracast_dev_send_frame(struct sigma_dut *dut, struct sigma_conn *conn,
			    struct sigma_cmd *cmd);

int miracast_dev_exec_action(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd);

int miracast_preset_testparameters(struct sigma_dut *dut,
				   struct sigma_conn *conn,
				   struct sigma_cmd *cmd);

int miracast_cmd_sta_get_parameter(struct sigma_dut *dut,
				   struct sigma_conn *conn,
				   struct sigma_cmd *cmd);

void miracast_sta_reset_default(struct sigma_dut *dut, struct sigma_conn *conn,
				struct sigma_cmd *cmd);

#endif /* SIGMA_MIRACAST_H */
