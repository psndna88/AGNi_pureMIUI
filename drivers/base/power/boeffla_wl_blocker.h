/*
 * Author: andip71, 01.09.2017
 *
 * Version 1.1.0
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define BOEFFLA_WL_BLOCKER_VERSION	"1.1.0"

#define LIST_WL_DEFAULT				"NETLINK;IPA_WS;[timerfd];IPCRTR_lpass_rx;hal_bluetooth_lock;tx_swr_ctrl;qrtr_0;IPA_CLIENT_APPS_LAN_CONS;IPA_CLIENT_APPS_WAN_CONS;smp2p-sleepstate;88c000.qcom,qup_uart;rmnet_ipa%d;qg"

#define LENGTH_LIST_WL				255
#define LENGTH_LIST_WL_DEFAULT		181
#define LENGTH_LIST_WL_SEARCH		LENGTH_LIST_WL + LENGTH_LIST_WL_DEFAULT + 5
