/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2014-2017, Qualcomm Atheros, Inc.
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

/*
 * This implementation in this file is based on source code released by
 * Wi-Fi Alliance under the following terms:
 *
* Copyright (c) 2014 Wi-Fi Alliance
*
* Permission to use, copy, modify, and/or distribute this software for any
* purpose with or without fee is hereby granted, provided that the above
* copyright notice and this permission notice appear in all copies.
*
* THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
* WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
* SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
* RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
* NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE
* USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "sigma_dut.h"

static void sigma_uapsd_reset(struct sigma_stream *s);
static void sigma_uapsd_stop(struct sigma_stream *s);
static void create_apts_hello_pkt(int msg, unsigned int *txbuf,
				  size_t txbuf_len, int tx_hello_cnt);


/* WMM-PS Test Case IDs */
#define B_D		1
#define B_H		2
#define B_B		3
#define B_M 		4
#define M_D		5
#define B_Z		6
#define M_Y		7
#define L_1		8
#define A_Y		9
#define B_W		10
#define A_J		11
#define M_V		12
#define M_U		13
#define A_U		14
#define M_L		15
#define B_K		16
#define M_B		17
#define M_K		18
#define M_W		19

/* WMM-PS APTS Msg IDs */
#define APTS_DEFAULT	(M_W + 0x01)
#define APTS_HELLO	(APTS_DEFAULT + 0x01)
#define APTS_BCST	(APTS_HELLO + 0x01)
#define APTS_CONFIRM	(APTS_BCST + 0x01)
#define APTS_STOP	(APTS_CONFIRM + 0x01)
#define APTS_CK_BE      (APTS_STOP + 0x01)
#define APTS_CK_BK      (APTS_CK_BE + 0x01)
#define APTS_CK_VI      (APTS_CK_BK + 0x01)
#define APTS_CK_VO      (APTS_CK_VI + 0x01)
#define APTS_RESET      (APTS_CK_VO + 0x01)
#define APTS_RESET_RESP (APTS_RESET + 0x01)
#define APTS_RESET_STOP (APTS_RESET_RESP + 0x01)
#define APTS_LAST       99

/* WMM-AC Test Case IDs */
extern int sigma_wmm_ac;
#ifdef CONFIG_WFA_WMM_AC
#define WMMAC_422_T02B            20
#define WMMAC_422_T03A            21
#define WMMAC_422_T04B            22
#define WMMAC_422_T05B            23
#define WMMAC_422_T06B            24
#define WMMAC_422_T07B            25
#define WMMAC_422_T08B            26

#define WMMAC_423_T04             27
#define WMMAC_424_T07t14          28
#define WMMAC_425_T04t06          29

#define WMMAC_521_T03             30
#define WMMAC_521_T05             31

#define WMMAC_522_T04             32
#define WMMAC_522_T06             33
#define WMMAC_522_T06o            34
#define WMMAC_524_T03             35
#define WMMAC_524_T03i            36
#define WMMAC_525_T07t10          37
#endif /* CONFIG_WFA_WMM_AC */

/* WMM-AC APTS Msg IDs */
/* WMMAC_APTS_DEFAULT Msg Id would be WMM-AC last test case
 * (WMMAC_525_T07t10) + 1 */
#define WMMAC_APTS_DEFAULT	38
#define WMMAC_APTS_HELLO	(WMMAC_APTS_DEFAULT + 0x01)
#define WMMAC_APTS_BCST		(WMMAC_APTS_HELLO + 0x01)
#define WMMAC_APTS_CONFIRM	(WMMAC_APTS_BCST + 0x01)
#define WMMAC_APTS_STOP		(WMMAC_APTS_CONFIRM + 0x01)
#define WMMAC_APTS_CK_BE      	(WMMAC_APTS_STOP + 0x01)
#define WMMAC_APTS_CK_BK      	(WMMAC_APTS_CK_BE + 0x01)
#define WMMAC_APTS_CK_VI      	(WMMAC_APTS_CK_BK + 0x01)
#define WMMAC_APTS_CK_VO      	(WMMAC_APTS_CK_VI + 0x01)
#define WMMAC_APTS_RESET      	(WMMAC_APTS_CK_VO + 0x01)
#define WMMAC_APTS_RESET_RESP 	(WMMAC_APTS_RESET + 0x01)
#define WMMAC_APTS_RESET_STOP 	(WMMAC_APTS_RESET_RESP + 0x01)
#define WMMAC_APTS_LAST       	99

#ifdef CONFIG_WFA_WMM_AC
#define LAST_TC		WMMAC_525_T07t10
#else /* CONFIG_WFA_WMM_AC */
#define LAST_TC		M_W
#endif /* CONFIG_WFA_WMM_AC */

struct apts_pkt {
	char *name;                     /* name of test */
	int cmd;                        /* msg num */
	int param0;                     /* number of packet exchanges */
	int param1;                     /* number of uplink frames */
	int param2;                     /* number of downlink frames */
	int param3;
};

/* WMM-PS APTS messages */
struct apts_pkt apts_pkts[] = {
	{0, -1, 0, 0, 0, 0},
	{"B.D", B_D, 0, 0, 0, 0},
	{"B.H", B_H, 0, 0, 0, 0},
	{"B.B", B_B, 0, 0, 0, 0},
	{"B.M", B_M, 0, 0, 0, 0},
	{"M.D", M_D, 0, 0, 0, 0},
	{"B.Z", B_Z, 0, 0, 0, 0},
	{"M.Y", M_Y, 0, 0, 0, 0},
	{"L.1", L_1, 0, 0, 0, 0},
	{"A.Y", A_Y, 0, 0, 0, 0},
	{"B.W", B_W, 0, 0, 0, 0},
	{"A.J", A_J, 0, 0, 0, 0},
	{"M.V", M_V, 0, 0, 0, 0},
	{"M.U", M_U, 0, 0, 0, 0},
	{"A.U", A_U, 0, 0, 0, 0},
	{"M.L", M_L, 0, 0, 0, 0},
	{"B.K", B_K, 0, 0, 0, 0},
	{"M.B", M_B, 0, 0, 0, 0},
	{"M.K", M_K, 0, 0, 0, 0},
	{"M.W", M_W, 0, 0, 0, 0},

	{"APTS TX         ", APTS_DEFAULT, 0, 0, 0, 0},
	{"APTS Hello      ", APTS_HELLO, 0, 0, 0, 0},
	{"APTS Broadcast  ", APTS_BCST, 0, 0, 0, 0},
	{"APTS Confirm    ", APTS_CONFIRM, 0, 0, 0, 0},
	{"APTS STOP       ", APTS_STOP, 0, 0, 0, 0},
	{"APTS CK BE      ", APTS_CK_BE, 0, 0, 0, 0},
	{"APTS CK BK      ", APTS_CK_BK, 0, 0, 0, 0},
	{"APTS CK VI      ", APTS_CK_VI, 0, 0, 0, 0},
	{"APTS CK VO      ", APTS_CK_VO, 0, 0, 0, 0},
	{"APTS RESET      ", APTS_RESET, 0, 0, 0, 0},
	{"APTS RESET RESP ", APTS_RESET_RESP, 0, 0, 0, 0},
	{"APTS RESET STOP ", APTS_RESET_STOP, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0}		/* APTS_LAST */
};

/* WMM-AC APTS messages */
struct apts_pkt wmm_ac_apts_pkts[] = {
	{0, -1, 0, 0, 0, 0},
	{"B.D", B_D, 0, 0, 0, 0},
	{"B.H", B_H, 0, 0, 0, 0},
	{"B.B", B_B, 0, 0, 0, 0},
	{"B.M", B_M, 0, 0, 0, 0},
	{"M.D", M_D, 0, 0, 0, 0},
	{"B.Z", B_Z, 0, 0, 0, 0},
	{"M.Y", M_Y, 0, 0, 0, 0},
	{"L.1", L_1, 0, 0, 0, 0},
	{"A.Y", A_Y, 0, 0, 0, 0},
	{"B.W", B_W, 0, 0, 0, 0},
	{"A.J", A_J, 0, 0, 0, 0},
	{"M.V", M_V, 0, 0, 0, 0},
	{"M.U", M_U, 0, 0, 0, 0},
	{"A.U", A_U, 0, 0, 0, 0},
	{"M.L", M_L, 0, 0, 0, 0},
	{"B.K", B_K, 0, 0, 0, 0},
	{"M.B", M_B, 0, 0, 0, 0},
	{"M.K", M_K, 0, 0, 0, 0},
	{"M.W", M_W, 0, 0, 0, 0},
#ifdef CONFIG_WFA_WMM_AC
	{"422.T02B", WMMAC_422_T02B, 0, 0, 0, 0},
	{"422.T03A", WMMAC_422_T03A, 0, 0, 0, 0},
	{"422.T04A", WMMAC_422_T04B, 0, 0, 0, 0},
	{"422.T05B", WMMAC_422_T05B, 0, 0, 0, 0},
	{"422.T06B", WMMAC_422_T06B, 0, 0, 0, 0},
	{"422.T07B", WMMAC_422_T07B, 0, 0, 0, 0},
	{"422.T08B", WMMAC_422_T08B, 0, 0, 0, 0},
	{"423.T04", WMMAC_423_T04, 0, 0, 0, 0},
	{"424.T07", WMMAC_424_T07t14, 0, 0, 0, 0},
	{"425.T04", WMMAC_425_T04t06, 0, 0, 0, 0},
	{"521.T03", WMMAC_521_T03, 0, 0, 0, 0},
	{"521.T05", WMMAC_521_T05, 0, 0, 0, 0},
	{"522.T04", WMMAC_522_T04, 0, 0, 0, 0},
	{"522.T06", WMMAC_522_T06, 0, 0, 0, 0},
	{"522.T06o", WMMAC_522_T06o, 0, 0, 0, 0},
	{"524.T03", WMMAC_524_T03, 0, 0, 0, 0},
	{"524.T03i", WMMAC_524_T03i, 0, 0, 0, 0},
	{"525.T07", WMMAC_525_T07t10, 0, 0, 0, 0},
#endif /* CONFIG_WFA_WMM_AC */
	{"APTS TX         ", WMMAC_APTS_DEFAULT, 0, 0, 0, 0},
	{"APTS Hello      ", WMMAC_APTS_HELLO, 0, 0, 0, 0},
	{"APTS Broadcast  ", WMMAC_APTS_BCST, 0, 0, 0, 0},
	{"APTS Confirm    ", WMMAC_APTS_CONFIRM, 0, 0, 0, 0},
	{"APTS STOP       ", WMMAC_APTS_STOP, 0, 0, 0, 0},
	{"APTS CK BE      ", WMMAC_APTS_CK_BE, 0, 0, 0, 0},
	{"APTS CK BK      ", WMMAC_APTS_CK_BK, 0, 0, 0, 0},
	{"APTS CK VI      ", WMMAC_APTS_CK_VI, 0, 0, 0, 0},
	{"APTS CK VO      ", WMMAC_APTS_CK_VO, 0, 0, 0, 0},
	{"APTS RESET      ", WMMAC_APTS_RESET, 0, 0, 0, 0},
	{"APTS RESET RESP ", WMMAC_APTS_RESET_RESP, 0, 0, 0, 0},
	{"APTS RESET STOP ", WMMAC_APTS_RESET_STOP, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0}		/* WMMAC_APTS_LAST */
};

/* WMM definitions */
/* Atheros Madwifi use 0x88 for UPSD/Voice */
#define TOS_VO7	    0xE0  /* 111 0  0000 (7)  AC_VO tos/dscp values default */
#define TOS_VO      0xD0  /* AC_VO */
#define TOS_VO6     0xC0  /* 110 0  0000 */
/* console */
#define TOS_VO2     0xB8  /* 101 1  1000 */
/* DUT can set VO */

#define TOS_VI      0xA0  /* 101 0  0000 (5)  AC_VI */
#define TOS_VI4     0x80  /* 100 0  0000 (4)  AC_VI */
/* console */
#define TOS_VI5     0x88  /* 100 0  1000 */

#define TOS_BE      0x00  /* 000 0  0000 (0)  AC_BE */
#define TOS_EE      0x60  /* 011 0  0000 (3)  AC_BE */

#define TOS_BK      0x20  /* 001 0  0000 (1)  AC_BK */
#define TOS_LE      0x40  /* 010 0  0000 (2)  AC_BK */

#define MAX_RETRY 3
#define MAX_HELLO 20
#define MAX_STOP 10
#define LI_INT  2000000

enum uapsd_psave {
	PS_OFF = 0,
	PS_ON = 1
};

typedef int (*uapsd_tx_state_func_ptr)(struct sigma_stream *,
				       u32, enum uapsd_psave, u32);

struct uapsd_tx_state_table {
	uapsd_tx_state_func_ptr state_func;
	u32 usr_priority;
	enum uapsd_psave ps;
	u32 sleep_dur;
};

static int uapsd_tx_start(struct sigma_stream *s,
			  u32 usr_priority, enum uapsd_psave ps,
			  u32 sleep_duration);
static int uapsd_tx_confirm(struct sigma_stream *s,
			    u32 usr_priority, enum uapsd_psave ps,
			    u32 sleep_duration);
static int uapsd_tx_data(struct sigma_stream *s,
			 u32 usr_priority, enum uapsd_psave ps,
			 u32 sleep_duration);
static int uapsd_tx_stop(struct sigma_stream *s,
			 u32 usr_priority, enum uapsd_psave ps,
			 u32 sleep_duration);
static int uapsd_tx_cyclic(struct sigma_stream *s,
			   u32 usr_priority, enum uapsd_psave ps,
			   u32 sleep_duration);
static int uapsd_tx_data_twice(struct sigma_stream *s,
			       u32 usr_priority, enum uapsd_psave ps,
			       u32 sleep_duration);

/* The DUT WMM send table for each of the test cases */
struct uapsd_tx_state_table sta_uapsd_tx_tbl[LAST_TC + 1][11] = {
	/* B.D */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0},
	 {NULL, 0, 0, 0}},

	/* B.H */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0},
	 {NULL, 0, 0, 0}},

	/* B.B */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_BK, PS_ON, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* B.M */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, 30000000},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* M.D */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* B.Z */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT /2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* M.Y */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* L.1 */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_cyclic, TOS_VO7, PS_ON, 20000},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* A.Y */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_data, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* B.W */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* A.J */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_OFF, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0},
	 {NULL, 0, 0, 0}},

	/* M.V */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* M.U */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data_twice, TOS_VO7, PS_ON, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* A.U */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_data, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_OFF, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}},

	/* M.L */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_BE, PS_ON, LI_INT /2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* B.K */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* M.B */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_BK, PS_ON, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* M.K */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* M.W */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

#ifdef CONFIG_WFA_WMM_AC
	/* WMMAC_422_T02B */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* WMMAC_422_T03B or  WMMAC_422_T03A */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 20},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 4},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 20},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 20},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* WMMAC_422_T04B/ATC7 */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 20},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 4},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 20},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 20},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* WMMAC_422_T05B/ATC8 */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 4},
	 {uapsd_tx_data, TOS_VI, PS_ON, 700000},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 20},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* WMMAC_422_T06B/ATC9 */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_BE, PS_ON, LI_INT / 20},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 4},
	 {uapsd_tx_data, TOS_BE, PS_ON, LI_INT / 20},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 20},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* WMMAC_422_T07B/ATC10 */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 20},
	 {uapsd_tx_data, TOS_BE, PS_ON, LI_INT / 4},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 20},
	 {uapsd_tx_data, TOS_BK, PS_ON, LI_INT / 20},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* WMMAC_422_T08B/ATC11 */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_BE, PS_ON, LI_INT / 20},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 4},
	 {uapsd_tx_data, TOS_BE, PS_ON, LI_INT / 20},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 4},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* WMMAC_423_T04 */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* WMMAC_424_T07t14 */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 20},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 4},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 20},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* WMMAC_425_T04t06 */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_BE, PS_ON, LI_INT / 20},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* WMMAC_521_T03 */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* WMMAC_521_T05 */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* WMMAC_522_T04 */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* WMMAC_522_T06 */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* WMMAC_522_T06o */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* WMMAC_524_T03 */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VO7, PS_ON, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* WMMAC_524_T03i */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},

	/* WMMAC_525_T07t10 */
	{{uapsd_tx_start, TOS_BE, PS_OFF, LI_INT / 2},
	 {uapsd_tx_confirm, TOS_BE, PS_ON, LI_INT / 2},
	 {uapsd_tx_data, TOS_VI, PS_ON, LI_INT / 2},
	 {uapsd_tx_stop, TOS_BE, PS_ON, LI_INT / 2},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0},
	 {NULL, 0, 0, 0}, {NULL, 0, 0, 0}, {NULL, 0, 0, 0}},
#endif /* CONFIG_WFA_WMM_AC */
};

typedef int (*uapsd_recv_state_func_ptr)(struct sigma_stream *s,
					 unsigned int *, int);

struct uapsd_rcv_state_table {
	uapsd_recv_state_func_ptr state_func;
};

static int uapsd_rx_start(struct sigma_stream *s,
			  unsigned int *rxpkt, int rxpkt_len);
static int uapsd_rx_data(struct sigma_stream *s,
			 unsigned int *rxpkt, int rxpkt_len);
static int uapsd_rx_stop(struct sigma_stream *s,
			 unsigned int *rxpkt, int rxpkt_len);
static int uapsd_rx_cyclic_vo(struct sigma_stream *s,
			      unsigned int *rxpkt, int rxpkt_len);

/* The DUT WMM send table for each of the test cases */
struct uapsd_rcv_state_table sta_uapsd_recv_tbl[LAST_TC + 10][6] = {
	/* B.D */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_stop}, {NULL}, {NULL},
	 {NULL}},
	/* B.H */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_stop},
	 {NULL}, {NULL}},
	/* B.B */
	{{uapsd_rx_start}, {uapsd_rx_stop}, {NULL}, {NULL}, {NULL}, {NULL}},
	/* B.M */
	{{uapsd_rx_start}, {uapsd_rx_stop}, {NULL}, {NULL}, {NULL}, {NULL}},
	/* M.D */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_data},
	 {uapsd_rx_data}, {uapsd_rx_stop}},
	/* B.Z */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_stop},
	 {NULL}, {NULL}},
	/* M.Y */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_data},
	 {uapsd_rx_stop}, {NULL}},
	/* L.1 */
	{{uapsd_rx_start}, {uapsd_rx_cyclic_vo}, {NULL}, {NULL}, {NULL},
	 {NULL}},
	/* A.Y */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_data},
	 {uapsd_rx_stop}, {NULL}},
	/* B.W */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_data},
	 {uapsd_rx_data}, {uapsd_rx_stop}},
	/* A.J */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_data},
	 {uapsd_rx_data}, {uapsd_rx_stop}},
	/* M.V */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_data},
	 {uapsd_rx_stop}, {NULL}},
	/* M.U */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_data},
	 {uapsd_rx_data}, {uapsd_rx_stop}},
	/* A.U */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_data},
	 {uapsd_rx_stop}, {NULL}},
	/* M.L */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_stop}, {NULL}, {NULL},
	 {NULL}},
	/* B.K */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_stop},
	 {NULL}, {NULL}},
	/* M.B */
	{{uapsd_rx_start}, {uapsd_rx_stop}, {NULL}, {NULL}, {NULL}, {NULL}},
	/* M.K */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_stop},
	 {NULL}, {NULL}},
	/* M.W */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_data},
	 {uapsd_rx_data}, {uapsd_rx_stop}},

#ifdef CONFIG_WFA_WMM_AC
	/* WMMAC_422_T02B */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_data},
	 {uapsd_rx_data}, {uapsd_rx_stop}},
	/* WMMAC_422_T03B or WMMAC_422_T03A */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_data},
	 {uapsd_rx_stop}, {NULL}},
	/* WMMAC_422_T04B */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_data},
	 {uapsd_rx_data}, {uapsd_rx_stop}},
	/* WMMAC_422_T05B */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_data},
	 {uapsd_rx_stop}, {NULL}},
	/* WMMAC_422_T06B */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_data},
	 {uapsd_rx_data}, {uapsd_rx_stop}},
	/* WMMAC_422_T07B */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_data},
	 {uapsd_rx_data}, {uapsd_rx_stop}},
	/* WMMAC_422_T08B */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_data},
	 {uapsd_rx_data}, {uapsd_rx_stop}},
	/* WMMAC_423_T04 */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_stop}, {NULL}, {NULL},
	 {NULL}},
	/* WMMAC_424_T07t14 */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_stop},
	 {NULL}, {NULL}},
	/* WMMAC_425_T04t06 */
	{{uapsd_rx_start}, {uapsd_rx_stop}, {NULL}, {NULL}, {NULL}, {NULL}},
	/* WMMAC_521_T03 */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_stop},
	 {NULL}, {NULL}},
	/* WMMAC_521_T05 */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_stop},
	 {NULL}, {NULL}},
	/* WMMAC_522_T04 */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_stop},
	 {NULL}, {NULL}},
	/* WMMAC_522_T06 */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_data},
	 {uapsd_rx_data}, {uapsd_rx_stop}},
	/* WMMAC_522_T06o */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_data},
	 {uapsd_rx_data}, {uapsd_rx_stop}},
	/* WMMAC_524_T03 */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_stop},
	 {NULL}, {NULL}},
	/* WMMAC_524_T03i */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_data}, {uapsd_rx_stop},
	 {NULL}, {NULL}},
	/* WMMAC_525_T07t10 */
	{{uapsd_rx_start}, {uapsd_rx_data}, {uapsd_rx_stop}, {NULL},
	 {NULL}, {NULL}},
#endif /* CONFIG_WFA_WMM_AC */
};

/* U-APSD console data */
#define NETWORK_CLASS_C 0x00ffffff
#define UAPSD_CONSOLE_TIMER 200

typedef int (*uapsd_console_state_func_ptr)(struct sigma_stream *s,
					    unsigned int *rx_msg_buf, int len);
struct uapsd_console_state_table {
	uapsd_console_state_func_ptr state_func;
};

static int console_rx_hello(struct sigma_stream *, unsigned int *, int);
static int console_rx_confirm(struct sigma_stream *, unsigned int *, int);
static int console_rx_confirm_tx_vi(struct sigma_stream *, unsigned int *, int);
static int console_rx_tx_stop(struct sigma_stream *, unsigned int *, int);
static int console_rx_vo_tx_stop(struct sigma_stream *, unsigned int *, int);
static int console_rx_vi_tx_stop(struct sigma_stream *, unsigned int *, int);
static int console_rx_vi_tx_vi(struct sigma_stream *, unsigned int *, int);
static int console_rx_vi_tx_bcst(struct sigma_stream *, unsigned int *, int);
static int console_rx_be_tx_bcst(struct sigma_stream *, unsigned int *, int);
static int console_rx_be_tx_be(struct sigma_stream *, unsigned int *, int);
static int console_rx_be_tx_be_tx_stop(struct sigma_stream *, unsigned int *,
				       int);
static int console_rx_bk_tx_stop(struct sigma_stream *, unsigned int *, int);
static int console_rx_vi_tx_vo_tx_stop(struct sigma_stream *, unsigned int *,
				       int);
static int console_rx_vo_tx_bcst_tx_stop(struct sigma_stream *, unsigned int *,
					 int);
static int console_rx_vi_tx_be(struct sigma_stream *, unsigned int *, int);
static int console_rx_vi_tx_bk(struct sigma_stream *, unsigned int *, int);
static int console_rx_vo_tx_vo_cyclic(struct sigma_stream *, unsigned int *,
				      int);
static int console_rx_vo_tx_vo(struct sigma_stream *, unsigned int *, int);
static int console_rx_vo_tx_vo_tx_stop(struct sigma_stream *, unsigned int *,
				       int);
static int console_rx_vo_tx_2vo(struct sigma_stream *, unsigned int *, int);
static int console_rx_be_tx_bcst_tx_stop(struct sigma_stream *, unsigned int *,
					 int);
static int console_rx_vo(struct sigma_stream *, unsigned int *, int);
static int console_rx_vi(struct sigma_stream *, unsigned int *, int);
static int console_rx_be(struct sigma_stream *, unsigned int *, int);
static int console_rx_vo_tx_all_tx_stop(struct sigma_stream *, unsigned int *,
					int);
static int console_rx_vi_tx_vi_tx_stop(struct sigma_stream *, unsigned int *,
				       int);

/* U-APSD console process table for each of the test cases */
struct uapsd_console_state_table uapsd_console_state_tbl[LAST_TC + 1][10] = {
	/* Ini */
	{{console_rx_hello}, {console_rx_confirm}, {console_rx_vo_tx_vo},
	 {console_rx_tx_stop}, {NULL}, {NULL}, {NULL}, {NULL}, {NULL}, {NULL}},

	/* B.D */
	{{console_rx_hello}, {console_rx_confirm}, {console_rx_vo_tx_vo},
	 {console_rx_vo_tx_stop}, {console_rx_tx_stop}, {console_rx_tx_stop},
	 {NULL}, {NULL}, {NULL}, {NULL}},

	/* B.H */
	{{console_rx_hello}, {console_rx_confirm}, {console_rx_vo_tx_2vo},
	 {console_rx_vo_tx_stop}, {console_rx_tx_stop}, {console_rx_tx_stop},
	 {NULL}, {NULL}, {NULL}, {NULL}},

	/* B.B */
	{{console_rx_hello}, {console_rx_confirm}, {console_rx_vo},
	 {console_rx_vi}, {console_rx_be}, {console_rx_bk_tx_stop},
	 {console_rx_tx_stop}, {NULL}, {NULL}, {NULL}},

	/* B.M */
	{{console_rx_hello}, {console_rx_confirm}, {console_rx_vi_tx_stop},
	 {console_rx_tx_stop}, {NULL}, {NULL}, {NULL}, {NULL}, {NULL}, {NULL}},

	/* M.D */
	{{console_rx_hello}, {console_rx_confirm}, {console_rx_vi_tx_be},
	 {console_rx_vi_tx_bk}, {console_rx_vi_tx_vi},
	 {console_rx_vi_tx_vo_tx_stop}, {console_rx_tx_stop},
	 {NULL}, {NULL}, {NULL}},

	/* B.Z */
	{{console_rx_hello}, {console_rx_confirm_tx_vi},
	 {console_rx_vo_tx_bcst_tx_stop}, {console_rx_tx_stop},
	 {NULL}, {NULL}, {NULL}, {NULL}},

	/* M.Y */
	{{console_rx_hello}, {console_rx_confirm}, {console_rx_vi_tx_vi},
	 {console_rx_vo}, {console_rx_be_tx_be},
	 {console_rx_be_tx_bcst_tx_stop}, {console_rx_tx_stop},
	 {NULL}, {NULL}, {NULL}},

	/* L.1 */
	{{console_rx_hello}, {console_rx_confirm}, {console_rx_vo_tx_vo_cyclic},
	 {console_rx_tx_stop}, {NULL}, {NULL}, {NULL}, {NULL}, {NULL}, {NULL}},

	/* A.Y */
	{{console_rx_hello}, {console_rx_confirm}, {console_rx_vi_tx_vi},
	 {console_rx_vo}, {console_rx_be_tx_be}, {console_rx_be},
	 {console_rx_be_tx_bcst_tx_stop}, {console_rx_tx_stop}, {NULL}, {NULL}},

	/* B.W */
	{{console_rx_hello}, {console_rx_confirm}, {console_rx_vi_tx_bcst},
	 {console_rx_vi_tx_bcst}, {console_rx_vi_tx_stop}, {console_rx_tx_stop},
	 {NULL}, {NULL}, {NULL}, {NULL}},

	/* A.J */
	{{console_rx_hello}, {console_rx_confirm}, {console_rx_vo},
	 {console_rx_vo_tx_all_tx_stop}, {console_rx_tx_stop},
	 {NULL}, {NULL}, {NULL}, {NULL}, {NULL}},

	/* M.V */
	{{console_rx_hello}, {console_rx_confirm}, {console_rx_vi_tx_vi},
	 {console_rx_be_tx_be}, {console_rx_vi_tx_vi_tx_stop},
	 {console_rx_tx_stop}, {NULL}, {NULL}, {NULL}, {NULL}},

	/* M.U */
	{{console_rx_hello}, {console_rx_confirm}, {console_rx_vi_tx_vi},
	 {console_rx_be_tx_be}, {console_rx_vo_tx_vo},
	 {console_rx_vo_tx_vo_tx_stop}, {console_rx_tx_stop},
	 {NULL}, {NULL}, {NULL}},

	/* A.U */
	{{console_rx_hello}, {console_rx_confirm}, {console_rx_vi_tx_vi},
	 {console_rx_be}, {console_rx_be_tx_be}, {console_rx_be},
	 {console_rx_vo_tx_vo}, {console_rx_vo_tx_stop}, {console_rx_tx_stop},
	 {NULL}},

	/* M.L */
	{{console_rx_hello}, {console_rx_confirm},
	 {console_rx_be_tx_be_tx_stop}, {console_rx_tx_stop},
	 {NULL}, {NULL}, {NULL}, {NULL}, {NULL}, {NULL}},

	/* B.K */
	{{console_rx_hello}, {console_rx_confirm}, {console_rx_vi_tx_vi},
	 {console_rx_be_tx_be}, {console_rx_vi_tx_stop}, {console_rx_tx_stop},
	 {NULL}, {NULL}, {NULL}, {NULL}},

	/* M.B */
	{{console_rx_hello}, {console_rx_confirm}, {console_rx_vo},
	 {console_rx_vi}, {console_rx_be}, {console_rx_bk_tx_stop},
	 {console_rx_tx_stop}, {NULL}, {NULL}, {NULL}},

	/* M.K */
	{{console_rx_hello}, {console_rx_confirm}, {console_rx_vi_tx_vi},
	 {console_rx_be_tx_be}, {console_rx_vi_tx_stop}, {console_rx_tx_stop},
	 {NULL}, {NULL}, {NULL}, {NULL}},

	/* M.W */
	{{console_rx_hello}, {console_rx_confirm}, {console_rx_vi_tx_bcst},
	 {console_rx_be_tx_bcst}, {console_rx_vi_tx_stop}, {console_rx_tx_stop},
	 {NULL}, {NULL}, {NULL}, {NULL}}
};


static void create_apts_pkt(int msg, unsigned int txbuf[], size_t txbuf_len,
			    u32 uapsd_dscp, struct sigma_stream *s)
{
	struct apts_pkt *t;

	if (!sigma_wmm_ac)
		t = &apts_pkts[msg];
	else
		t = &wmm_ac_apts_pkts[msg];

	txbuf[0] = s->rx_cookie;
	txbuf[1] = uapsd_dscp;
	txbuf[2] = 0;
	txbuf[3] = 0;
	txbuf[4] = 0;
	txbuf[5] = 0;
	txbuf[9] = s->sta_id;
	txbuf[10] = t->cmd;
	strlcpy((char *) &txbuf[11], t->name,
		txbuf_len - sizeof(txbuf[0]) * 11);
}


static int uapsd_tx_start(struct sigma_stream *s,
			  u32 usr_priority, enum uapsd_psave ps,
			  u32 sleep_duration)
{
	unsigned int *tpkt;
	int pktlen = 256;
	struct sigma_dut *dut = s->dut;
	u32 msgid;

	sigma_dut_print(dut, DUT_MSG_INFO, "send_uapsd: Enter uapsd_tx_start");
	tpkt = malloc(s->payload_size);
	if (tpkt == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"send_uapsd: Send buffer allocation failed");
		return -1;
	}

	/* check whether a test case is received */
	if (s->uapsd_rx_state > 0) {
		s->uapsd_tx_state++;
	} else {
		set_ps(s->ifname, dut, 0);
		if (s->tx_hello_cnt <= MAX_HELLO) {
			memset(tpkt, 0, s->payload_size);
			/* if test is for WMM-AC set APTS HELLO to 39 */
			msgid = sigma_wmm_ac ? WMMAC_APTS_HELLO : APTS_HELLO;
			create_apts_hello_pkt(msgid, tpkt, s->payload_size,
					      s->tx_hello_cnt);
			if (send(s->sock, tpkt, pktlen, 0) <= 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"send_uapsd: Send failed");
			}
			s->tx_hello_cnt++;
			sigma_dut_print(dut, DUT_MSG_INFO,
					"send_uapsd: Hello Sent cnt %d",
					s->tx_hello_cnt);
			sleep(1);
		} else {
			printf("\n send_uapsd: Too many Hellos Sent... \n");
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"send_uapsd: Too many Hellos sent... ");
			s->stop = 1;
		}
	}

	free(tpkt);
	sigma_dut_print(dut, DUT_MSG_INFO,
			"Exit uapsd_tx_start uapsd_sta_tc %d uapsd_tx_state %d",
			s->uapsd_sta_tc, s->uapsd_tx_state);

	return 0;
}


static int uapsd_tx_confirm(struct sigma_stream *s,
			    u32 usr_priority, enum uapsd_psave ps,
			    u32 sleep_duration)
{
	unsigned int *tpkt;
	int pktlen = 256;
	struct sigma_dut *dut = s->dut;
	u32 msgid;

	sigma_dut_print(dut, DUT_MSG_INFO,
			"send_uapsd: Enter uapsd_tx_confirm");
	tpkt = malloc(s->payload_size);
	if (tpkt == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"send_uapsd: Send buffer allocation failed");
		return -1;
	}

	usleep(sleep_duration);
	set_ps(s->ifname, dut, ps);
	memset(tpkt, 0, s->payload_size);
	/* if test is for WMM-AC set APTS CONFIRM to 41 */
	msgid = sigma_wmm_ac ? WMMAC_APTS_CONFIRM : APTS_CONFIRM;
	create_apts_pkt(msgid, tpkt, s->payload_size, usr_priority, s);
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &usr_priority,
		   sizeof(usr_priority));
	if (send(s->sock, tpkt, pktlen, 0) > 0) {
		s->uapsd_tx_state++;
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"send_uapsd: Send failed");
	}
	free(tpkt);
	sigma_dut_print(dut, DUT_MSG_INFO,
			"Exit uapsd_tx_confirm uapsd_sta_tc %d uapsd_tx_state %d",
			s->uapsd_sta_tc, s->uapsd_tx_state);

	return 0;
}


static int uapsd_tx_data(struct sigma_stream *s,
			 u32 usr_priority, enum uapsd_psave ps,
			 u32 sleep_duration)
{
	unsigned int *tpkt;
	int pktlen = 256;
	struct sigma_dut *dut = s->dut;
	u32 msgid;

	sigma_dut_print(dut, DUT_MSG_INFO, "send_uapsd: Enter uapsd_tx_data");

	tpkt = malloc(s->payload_size);
	if (tpkt == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"send_uapsd: Send buffer allocation failed");
		return -1;
	}
	usleep(sleep_duration);
	set_ps(s->ifname, dut, ps);
	memset(tpkt, 0, s->payload_size);
	/* if test is for WMM-AC set APTS DEFAULT to 38 */
	msgid = sigma_wmm_ac ? WMMAC_APTS_DEFAULT : APTS_DEFAULT;
	create_apts_pkt(msgid, tpkt, s->payload_size, usr_priority, s);
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &usr_priority,
		   sizeof(usr_priority));
	if (send(s->sock, tpkt, pktlen, 0) > 0) {
		s->uapsd_tx_state++;
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"send_uapsd: Send failed");
	}

	free(tpkt);
	sigma_dut_print(dut, DUT_MSG_INFO,
			"Exit uapsd_tx_data uapsd_sta_tc %d uapsd_tx_state %d",
			s->uapsd_sta_tc, s->uapsd_tx_state);

	return 0;
}


static int uapsd_tx_data_twice(struct sigma_stream *s,
			       u32 usr_priority, enum uapsd_psave ps,
			       u32 sleep_duration)
{
	unsigned int *tpkt;
	int pktlen = 256, i = 0, tx_status = 0;
	struct sigma_dut *dut = s->dut;
	u32 msgid;

	sigma_dut_print(dut, DUT_MSG_INFO,
			"send_uapsd: Enter uapsd_tx_data_twice");
	tpkt = malloc(s->payload_size);
	if (tpkt == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"send_uapsd: Send buffer allocation failed");
		return -1;
	}
	usleep(sleep_duration);
	set_ps(s->ifname, dut, ps);
	memset(tpkt, 0, s->payload_size);
	/* if test is for WMM-AC set APTS DEFAULT to 38 */
	msgid = sigma_wmm_ac ? WMMAC_APTS_DEFAULT : APTS_DEFAULT;
	create_apts_pkt(msgid, tpkt, s->payload_size, usr_priority, s);
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &usr_priority,
		   sizeof(usr_priority));
	for(i = 0; i < 2; i++) {
		if (send(s->sock, tpkt, pktlen, 0) <= 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"send_uapsd: Send failed");
			tx_status = -1;
		}
	}
	if (tx_status == 0)
		s->uapsd_tx_state++;
	free(tpkt);
	sigma_dut_print(dut, DUT_MSG_INFO,
			"Exit uapsd_tx_data_twice uapsd_sta_tc %d uapsd_tx_state %d",
			s->uapsd_sta_tc, s->uapsd_tx_state);

	return 0;
}


static int uapsd_tx_cyclic(struct sigma_stream *s,
			   u32 usr_priority, enum uapsd_psave ps,
			   u32 sleep_duration)
{
	unsigned int *tpkt;
	int pktlen = 256, i = 0, tx_status = 0;
	struct sigma_dut *dut = s->dut;
	u32 msgid;

	sigma_dut_print(dut, DUT_MSG_INFO, "send_uapsd: Enter uapsd_tx_cyclic");
	tpkt = malloc(s->payload_size);
	if (tpkt == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"send_uapsd: Send buffer allocation failed");
		return -1;
	}

	set_ps(s->ifname, dut, ps);
	for (i = 0; i < 3000; i++) {
		usleep(sleep_duration);
		memset(tpkt, 0, s->payload_size);
		/* if test is for WMM-AC set APTS DEFAULT to 38 */
		msgid = sigma_wmm_ac ? WMMAC_APTS_DEFAULT : APTS_DEFAULT;
		create_apts_pkt(msgid, tpkt, s->payload_size, usr_priority, s);
		setsockopt(s->sock, IPPROTO_IP, IP_TOS, &usr_priority,
			   sizeof(usr_priority));
		if (send(s->sock, tpkt, pktlen, 0) <= 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"send_uapsd: Send failed");
			tx_status = -1;
		}
	}
	if (tx_status == 0)
		s->uapsd_tx_state++;
	free(tpkt);
	sigma_dut_print(dut, DUT_MSG_INFO,
			"Exit uapsd_tx_cyclic uapsd_sta_tc %d uapsd_tx_state %d",
			s->uapsd_sta_tc, s->uapsd_tx_state);

	return 0;
}


static int uapsd_tx_stop(struct sigma_stream *s,
			 u32 usr_priority, enum uapsd_psave ps,
			 u32 sleep_duration)
{
	unsigned int *tpkt;
	int pktlen = 256;
	struct sigma_dut *dut = s->dut;
	u32 msgid;

	sigma_dut_print(dut, DUT_MSG_INFO, "send_uapsd: Enter uapsd_tx_stop");
	tpkt = malloc(s->payload_size);
	if (tpkt == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"send_uapsd: Send buffer allocation failed");
		return -1;
	}
	usleep(sleep_duration);
	if(!s->tx_stop_cnt)
		set_ps(s->ifname, dut, ps);
	s->tx_stop_cnt++;
	memset(tpkt, 0, s->payload_size);
	/* if test is for WMM-AC set APTS STOP to 42 */
	msgid = sigma_wmm_ac ? WMMAC_APTS_STOP : APTS_STOP;
	create_apts_pkt(msgid, tpkt, s->payload_size, usr_priority, s);
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &usr_priority,
		   sizeof(usr_priority));
	if (send(s->sock, tpkt, pktlen, 0) <= 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"send_uapsd: Send failed");
	}
	pthread_mutex_lock(&s->tx_thr_mutex);
	pthread_cond_signal(&s->tx_thr_cond);
	pthread_mutex_unlock(&s->tx_thr_mutex);
	if (s->tx_stop_cnt > MAX_STOP) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"send_uapsd: Enter TX_STOP Max Stop sent %d",
				s->tx_stop_cnt);
		s->stop = 1;
	}
	free(tpkt);
	sigma_dut_print(dut, DUT_MSG_INFO,
			"Exit uapsd_tx_stop uapsd_sta_tc %d uapsd_tx_state %d",
			s->uapsd_sta_tc, s->uapsd_tx_state);

	return 0;
}


static int uapsd_rx_start(struct sigma_stream *s,
			  unsigned int *rxpkt, int rxpkt_len)
{
	int test_num = 0;
	struct sigma_dut *dut = s->dut;
	int msgid;

	test_num = rxpkt[10];

	sigma_dut_print(dut, DUT_MSG_INFO,
			"receive_uapsd: Enter uapsd_rx_start");
	/* if test is for WMM-AC set LAST_TC to 37 */
	msgid = sigma_wmm_ac ? LAST_TC : M_W;

	if (!((test_num >= B_D) && (test_num <= msgid)))
		return -1;

	/*
	 * Test numbers start from 1. Hence decrement by 1
	 * to match the array index.
	 */
	s->uapsd_sta_tc = (rxpkt[10] - 1);
	s->sta_id = rxpkt[9];
	(s->uapsd_rx_state)++;

	return 0;
}


static int uapsd_rx_data(struct sigma_stream *s,
			 unsigned int *rxpkt, int rxpkt_len)
{
	struct sigma_dut *dut = s->dut;
	u32 msgid;

	sigma_dut_print(dut, DUT_MSG_INFO,
			"receive_uapsd: Enter uapsd_rx_data");
	/* if test is for WMM-AC set APTS DEFAULT to 38 */
	msgid = sigma_wmm_ac ? WMMAC_APTS_DEFAULT : APTS_DEFAULT;

	if ((rxpkt[10] == msgid) &&
	    ((rxpkt[1] == TOS_BE) ||
	     (rxpkt[1] == TOS_BK) ||
	     (rxpkt[1] == TOS_VI) ||
	     (rxpkt[1] == TOS_VO) ||
	     (rxpkt[1] == TOS_VO7) ||
	     (rxpkt[1] == TOS_VO6))) {
		s->rx_cookie = rxpkt[0];
		(s->uapsd_rx_state)++;
		sigma_dut_print(dut, DUT_MSG_INFO,
				"receive_uapsd: Recv in uapsd_rx_data uapsd_rx_state %d",
				s->uapsd_rx_state);
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"receive_uapsd: BAD Pkt recv in uapsd_rx_data");
		sigma_uapsd_reset(s);
	}

	sigma_dut_print(dut, DUT_MSG_INFO,
			"receive_uapsd: Exit uapsd_rx_data");

	return 0;
}


static int uapsd_rx_stop(struct sigma_stream *s,
			 unsigned int *rxpkt, int rxpkt_len)
{
	struct sigma_dut *dut = s->dut;
	u32 msgid;

	sigma_dut_print(dut, DUT_MSG_INFO,
			"receive_uapsd: Enter uapsd_rx_stop");
	/* if test is for WMM-AC set APTS STOP to 42 */
	msgid = sigma_wmm_ac ? WMMAC_APTS_STOP : APTS_STOP;

	if (rxpkt[10] != msgid) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"receive_uapsd: BAD Pkt recv in uapsd_rx_stop");
	} else {
		sigma_uapsd_stop(s);
	}
	sigma_dut_print(dut, DUT_MSG_INFO, "receive_uapsd: Exit uapsd_rx_stop");

	return 0;
}


static int uapsd_rx_cyclic_vo(struct sigma_stream *s,
			      unsigned int *rxpkt, int rxpkt_len)
{
	struct sigma_dut *dut = s->dut;
	u32 msgid, msgid2;

	sigma_dut_print(dut, DUT_MSG_INFO,
			"receive_uapsd: Enter uapsd_rx_cyclic_vo");
	/* if test is for WMM-AC set
	 * APTS STOP to 42 and
	 * APTS DEFAULT to 38 */
	if (!sigma_wmm_ac) {
		msgid = APTS_STOP;
		msgid2 = APTS_DEFAULT;
	} else {
		msgid = WMMAC_APTS_STOP;
		msgid2 = WMMAC_APTS_DEFAULT;
	}

	if (rxpkt[10] != msgid) {
		if ((rxpkt[10] == msgid2) &&
		    ((rxpkt[1] == TOS_VO) ||
		     (rxpkt[1] == TOS_VO7) ||
		     (rxpkt[1] == TOS_VO6))) {
			/* ; 5.7 */
			s->rx_cookie = rxpkt[0];
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"receive_uapsd: BAD Pkt recv in uapsd_rx_cyclic_vo");
		}
	} else {
		sigma_uapsd_stop(s);
	}
	sigma_dut_print(dut, DUT_MSG_INFO,
			"receive_uapsd: Exit uapsd_rx_cyclic_vo");

	return 0;
}


static void create_apts_hello_pkt(int msg, unsigned int *txbuf,
				  size_t txbuf_len, int tx_hello_cnt)
{
	struct apts_pkt *t;

	if (!sigma_wmm_ac)
		t = &apts_pkts[msg];
	else
		t = &wmm_ac_apts_pkts[msg];

	txbuf[0] = tx_hello_cnt;
	txbuf[1] = 0;
	txbuf[2] = 0;
	txbuf[3] = 0;
	txbuf[4] = 0;
	txbuf[5] = 0;
	txbuf[6] = t->param0;
	txbuf[7] = t->param1;
	txbuf[8] = t->param2;
	txbuf[9] = t->param3;
	txbuf[10] = t->cmd;
	strlcpy((char *) &txbuf[11], t->name,
		txbuf_len - sizeof(txbuf[0]) * 11);
	printf("create_apts_hello_pkt (%s) %d\n", t->name, t->cmd);
}


static void sigma_uapsd_init(struct sigma_stream *s)
{
	s->uapsd_sta_tc = 0; /* Test Case to execute or row to select */
	/* in a test case row, next column or next state function to execute */
	s->uapsd_rx_state = 0;
	s->uapsd_tx_state = 0;

	s->sta_id = 0;
	s->uapsd_send_thr = 0;

	s->reset_rx = 0;
	s->num_retry = 0;
	s->tx_stop_cnt = 0;
	s->tx_hello_cnt = 0;
}


static void sigma_uapsd_stop(struct sigma_stream *s)
{
	pthread_mutex_lock(&s->tx_thr_mutex);
	pthread_cond_wait(&s->tx_thr_cond, &s->tx_thr_mutex);
	pthread_mutex_unlock(&s->tx_thr_mutex);
	s->stop = 1;
	sleep(1);
}


static void sigma_uapsd_reset(struct sigma_stream *s)
{
	int tos = TOS_BE;
	unsigned int *reset_pkt;
	struct sigma_dut *dut = s->dut;
	u32 msgid;

	s->num_retry++;

	/* if reset is called from U-APSD console set it */
	s->reset = 1;

	reset_pkt = malloc(s->payload_size);
	if (reset_pkt == NULL)
		return;

	if (s->num_retry > MAX_RETRY) {
		/* if test is for WMM-AC set APTS RESET STOP to 49 */
		msgid = sigma_wmm_ac ? WMMAC_APTS_RESET_STOP : APTS_RESET_STOP;
		create_apts_pkt(msgid, reset_pkt, s->payload_size, tos, s);
		setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
		send(s->sock, reset_pkt, s->payload_size, 0);
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"sigma_uapsd_reset: Too many Reset retries");
		s->stop = 1;
	}

	if (!(s->reset_rx)) {
		/* if test is for WMM-AC set APTS RESET to 47 */
		msgid = sigma_wmm_ac ? WMMAC_APTS_RESET : APTS_RESET;
		create_apts_pkt(msgid, reset_pkt, s->payload_size, tos, s);
		setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
		send(s->sock, reset_pkt, s->payload_size, 0);
	} else {
		/* if test is for WMM-AC set APTS RESET RESP to 48 */
		msgid = sigma_wmm_ac ? WMMAC_APTS_RESET_RESP : APTS_RESET_RESP;
		create_apts_pkt(msgid, reset_pkt, s->payload_size, tos, s);
		setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
		send(s->sock, reset_pkt, s->payload_size, 0);
		s->reset_rx = 0;
	}
	free(reset_pkt);
}


static void * send_uapsd(void *data)
{
	struct sigma_stream *s = data;
	struct sigma_dut *dut = s->dut;
	uapsd_tx_state_func_ptr tx_state_func;
	u32 usr_priority, sleep_duration;
	enum uapsd_psave ps;

	sigma_dut_print(dut, DUT_MSG_INFO, "send_uapsd: Uapsd TX Start");

	s->payload_size = 512;

	while (!s->stop) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"send_uapsd: running  while uapsd_rx_state %d",
				s->uapsd_rx_state);

		tx_state_func = sta_uapsd_tx_tbl[s->uapsd_sta_tc]
			[s->uapsd_tx_state].state_func;
		usr_priority = sta_uapsd_tx_tbl[s->uapsd_sta_tc]
			[s->uapsd_tx_state].usr_priority;
		sleep_duration = sta_uapsd_tx_tbl[s->uapsd_sta_tc]
			[s->uapsd_tx_state].sleep_dur;
		ps = sta_uapsd_tx_tbl[s->uapsd_sta_tc][s->uapsd_tx_state].ps;

		sigma_dut_print(dut, DUT_MSG_INFO,
				"send_uapsd: uapsd_sta_tc %d uapsd_tx_state %d",
				s->uapsd_sta_tc, s->uapsd_tx_state);
		if (tx_state_func) {
			tx_state_func(s, usr_priority, ps, sleep_duration);
		} else {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"send_uapsd: Null Function Detected for TC : %d in uapsd_tx_state : %d",
					s->uapsd_sta_tc, s->uapsd_tx_state);
		}
	}

	sigma_dut_print(dut, DUT_MSG_INFO, "send_uapsd: Uapsd TX End");

	return NULL;
}


void receive_uapsd(struct sigma_stream *s)
{
	struct timeval tv;
	fd_set rfds;
	int res = 0, ret = 0, rxpkt_len = 0;
	unsigned int *rxpkt;
	uapsd_recv_state_func_ptr recv_state_func;
	struct sigma_dut *dut = s->dut;
	u32 msgid;

	sigma_dut_print(dut, DUT_MSG_INFO, "receive_uapsd: Uapsd RX Start");
	sigma_uapsd_init(s);

	ret = pthread_mutex_init(&s->tx_thr_mutex, NULL);
	if (ret != 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"receive_uapsd: pthread_mutex_init failed");
		return;
	}

	ret = pthread_cond_init(&s->tx_thr_cond, NULL);
	if (ret != 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"receive_uapsd: pthread_cond_init failed");
		pthread_mutex_destroy(&s->tx_thr_mutex);
		return;
	}

	if (pthread_create(&s->uapsd_send_thr, NULL, send_uapsd, s)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"receive_uapsd: send_uapsd tx thread creation failed");
		pthread_cond_destroy(&s->tx_thr_cond);
		pthread_mutex_destroy(&s->tx_thr_mutex);
		return;
	}

	s->payload_size = 512;
	rxpkt = malloc(s->payload_size);
	if (rxpkt == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"receive_uapsd: Receive buffer allocation failed");
		s->stop = 1;
	}

	while (!s->stop) {
		FD_ZERO(&rfds);
		FD_SET(s->sock, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 100000;
		res = select(s->sock + 1, &rfds, NULL, NULL, &tv);
		if (res < 0) {
			perror("select");
			usleep(10000);
			continue;
		}

		if (!FD_ISSET(s->sock, &rfds))
			continue;

		memset(rxpkt, 0, s->payload_size);
		rxpkt_len = recv(s->sock, rxpkt, s->payload_size, 0);
		sigma_dut_print(dut, DUT_MSG_INFO,
				"receive_uapsd: running res %d cookie %d dscp %d apts-pkt %d sta-id %d",
				res, rxpkt[0], rxpkt[1], rxpkt[10],
				rxpkt[9]);

		if (rxpkt_len > 0) {
			s->rx_frames++;
			s->rx_payload_bytes += res;

			/* if test is for WMM-AC set APTS RESET to 47 */
			msgid = sigma_wmm_ac ? WMMAC_APTS_RESET : APTS_RESET;
			if (msgid == rxpkt[10]) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"receive_uapsd: RESET Pkt recv");
				s->reset_rx = 1;
				sigma_uapsd_reset(s);
				continue;
			}

			recv_state_func = sta_uapsd_recv_tbl[s->uapsd_sta_tc]
				[s->uapsd_rx_state].state_func;
			sigma_dut_print(dut, DUT_MSG_INFO,
					"receive_uapsd: running s->uapsd_sta_tc %d uapsd_rx_state %d",
					s->uapsd_sta_tc, s->uapsd_rx_state);
			if (recv_state_func) {
				recv_state_func(s, rxpkt, rxpkt_len);
			} else {
				sigma_dut_print(dut, DUT_MSG_INFO,
						"receive_uapsd: Null Function Detected for TC : %d in uapsd_rx_state : %d",
						s->uapsd_sta_tc,
						s->uapsd_rx_state);
			}
		} else if (res < 0) {
			perror("recv");
			break;
		}
	}

	if (rxpkt)
		free(rxpkt);
	sigma_dut_print(dut, DUT_MSG_INFO, "receive_uapsd: Uapsd RX End");
	if (s->sock >= 0) {
		pthread_join(s->uapsd_send_thr, NULL);
		close(s->sock);
		s->sock = -1;
	}
	pthread_cond_destroy(&s->tx_thr_cond);
	pthread_mutex_destroy(&s->tx_thr_mutex);
}


/* U-APSD apts console code implementation */
static int packet_expected(struct sigma_stream *s, unsigned int *rpkt,
			   unsigned int type, u32 tos)
{
	u8 type_ok = 0;
	u8 tos_ok = 0;
	int res = 0;
	struct sigma_dut *dut = s->dut;

	type_ok = (rpkt[10] == type) ? 1 : 0;

	switch (tos) {
	case TOS_VO7:
	case TOS_VO:
	case TOS_VO6:
	case TOS_VO2:
		if (rpkt[1] == TOS_VO7 || rpkt[1] == TOS_VO ||
		    rpkt[1] == TOS_VO6 || rpkt[1] == TOS_VO2)
			tos_ok = 1;
		break;
	case TOS_VI:
	case TOS_VI4:
	case TOS_VI5:
		if (rpkt[1] == TOS_VI || rpkt[1] == TOS_VI4 ||
		    rpkt[1] == TOS_VI5)
			tos_ok = 1;
		break;
	case TOS_BE:
	case TOS_EE:
		if (rpkt[1] == TOS_BE || rpkt[1] == TOS_EE)
			tos_ok = 1;
		break;
	case TOS_BK:
	case TOS_LE:
		if (rpkt[1] == TOS_BK || rpkt[1] == TOS_LE)
			tos_ok = 1;
		break;
	default:
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"packet_expected: recv not known tos=0x%x",
				tos);
		break;
	}

	res = type_ok && tos_ok;
	if (!res) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"packet_expected: No match: received pkt_type %u expected type %u, received dscp 0x%x expected dscp 0x%x",
				rpkt[10], type, rpkt[1], tos);
	}

	return res;
}


static int console_send(struct sigma_stream *s, u32 pkt_type, u32 tos)
{
	u32 *tpkt;
	int res = 0;
	struct sigma_dut *dut = s->dut;

	tpkt = malloc(s->payload_size);
	if (tpkt == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_send: Send buffer allocation failed");
		return 0;
	}
	memset(tpkt, 0, s->payload_size);
	create_apts_pkt(pkt_type, tpkt, s->payload_size, tos, s);
	if (pkt_type == APTS_DEFAULT || pkt_type == APTS_STOP)
		tpkt[0] = ++(s->rx_cookie);
	tpkt[1] = tos;
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
	res = send(s->sock, tpkt, s->payload_size / 2, 0);
	if (res >= 0) {
		s->tx_frames++;
		s->tx_payload_bytes += res;
	}
	s->uapsd_rx_state++;
	sigma_dut_print(dut, DUT_MSG_INFO,
			"console_send: Sent packet return %d Type %d Tos %d",
			res, tpkt[10], tpkt[1]);
	free(tpkt);

	return 0;
}


static int console_rx_hello(struct sigma_stream *s, unsigned int *rpkt, int len)
{
	struct sigma_dut *dut = s->dut;

	if (!packet_expected(s, rpkt, APTS_HELLO, TOS_BE)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_hello: Hello not Recv or Bad TOS");
		return 0;
	}

	s->rx_cookie = 0;
	sigma_dut_print(dut, DUT_MSG_INFO,
			"console_rx_hello: Recv Hello, Sent Test Case");
	console_send(s, s->uapsd_sta_tc, TOS_BE);

	return 0;
}


static int console_rx_confirm(struct sigma_stream *s, unsigned int *rpkt,
			      int len)
{
	struct sigma_dut *dut = s->dut;

	if (!packet_expected(s, rpkt, APTS_CONFIRM, TOS_BE)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_confirm: Confirm not Recv or Bad TOS");
		return 0;
	}

	s->rx_cookie = 0;
	s->uapsd_rx_state++;
	sigma_dut_print(dut, DUT_MSG_INFO, "console_rx_confirm: Recv Confirm");

	return 0;
}


static int console_rx_confirm_tx_vi(struct sigma_stream *s, unsigned int *rpkt,
				    int len)
{
	struct sigma_dut *dut = s->dut;

	if (!packet_expected(s, rpkt, APTS_CONFIRM, TOS_BE)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_confirm_tx_vi: Confirm not Recv or Bad TOS");
		return 0;
	}

	s->rx_cookie = 0;
	console_send(s, APTS_DEFAULT, TOS_VI);
	sigma_dut_print(dut, DUT_MSG_INFO,
			"console_rx_confirm_tx_vi: Recv Confirm, Sent VI");

	return 0;

}


static int console_rx_tx_stop(struct sigma_stream *s, unsigned int *rpkt,
			      int len)
{
	struct sigma_dut *dut = s->dut;

	sigma_dut_print(dut, DUT_MSG_INFO,
			"console_rx_tx_stop: Send stop to STA again quit %d stop %d",
			s->can_quit, s->stop);

	console_send(s, APTS_STOP, TOS_BE);

	if (packet_expected(s, rpkt, APTS_STOP, TOS_BE)) {
		if (s->can_quit) {
			s->stop = 1;
			sigma_dut_print(dut, DUT_MSG_INFO,
					"console_rx_tx_stop: Send stop to STA again quit %d stop %d",
					s->can_quit, s->stop);
		} else {
			s->can_quit = 1;
		}
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_tx_stop: STOP not Recv or Bad TOS");
	}

	return 0;
}


static int console_rx_vo_tx_vo(struct sigma_stream *s, unsigned int *rpkt,
			       int len)
{
	struct sigma_dut *dut = s->dut;
	unsigned int tos = TOS_VO7;
	int res;

	if (!packet_expected(s, rpkt, APTS_DEFAULT, TOS_VO)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_vo_tx_vo: Expected Pkt not Recv or Bad TOS");
		return 0;
	}

	rpkt[0] = ++(s->rx_cookie);
	rpkt[1] = TOS_VO;
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
	res = send(s->sock, rpkt, s->payload_size / 2, 0);
	if (res >= 0) {
		s->tx_frames++;
		s->tx_payload_bytes += res;
	}
	s->uapsd_rx_state++;
	sigma_dut_print(dut, DUT_MSG_INFO,
			"console_rx_vo_tx_vo: Recv VO, Sent VO");

	return 0;
}


static int console_rx_vo_tx_vo_tx_stop(struct sigma_stream *s,
				       unsigned int *rpkt, int len)
{
	struct sigma_dut *dut = s->dut;
	unsigned int tos = TOS_VO7;
	int res;

	if (!packet_expected(s, rpkt, APTS_DEFAULT, TOS_VO)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_vo_tx_vo_tx_stop: Expected Pkt not Recv or Bad TOS");
		return 0;
	}

	rpkt[0] = ++(s->rx_cookie);
	rpkt[1] = TOS_VO;
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
	res = send(s->sock, rpkt, s->payload_size / 2, 0);
	if (res >= 0) {
		s->tx_frames++;
		s->tx_payload_bytes += res;
	}
	sigma_dut_print(dut, DUT_MSG_INFO,
			"console_rx_vo_tx_vo_tx_stop: Recv VO, Sent VO");
		usleep(500000);
		console_send(s, APTS_STOP, TOS_BE);

	return 0;
}


static int console_rx_vo_tx_all_tx_stop(struct sigma_stream *s,
					unsigned int *rpkt, int len)
{
	struct sigma_dut *dut = s->dut;
	u32 tos = TOS_VO7;
	int res;

	if (!packet_expected(s, rpkt, APTS_DEFAULT, TOS_VO)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_vo_tx_all_tx_stop: Expected Pkt not Recv or Bad TOS");
		return 0;
	}

	rpkt[0] = ++(s->rx_cookie);
	rpkt[1] = TOS_VO;
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
	res = send(s->sock, rpkt, s->payload_size / 2, 0);
	if (res >= 0) {
		s->tx_frames++;
		s->tx_payload_bytes += res;
	}
	sigma_dut_print(dut, DUT_MSG_INFO,
			"console_rx_vo_tx_all_tx_stop: Recv VO, Sent VO");
	rpkt[0] = ++(s->rx_cookie);
	tos = TOS_VI;
	rpkt[1] = tos;
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
	res = send(s->sock, rpkt, s->payload_size / 2, 0);
	if (res >= 0) {
		s->tx_frames++;
		s->tx_payload_bytes += res;
	}
	sigma_dut_print(dut, DUT_MSG_INFO,
			"console_rx_vo_tx_all_tx_stop: Sent VI");
	rpkt[0] = ++(s->rx_cookie);
	tos = TOS_BE;
	rpkt[1] = tos;
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
	res = send(s->sock, rpkt, s->payload_size / 2, 0);
	if (res >= 0) {
		s->tx_frames++;
		s->tx_payload_bytes += res;
	}
	sigma_dut_print(dut, DUT_MSG_INFO,
			"console_rx_vo_tx_all_tx_stop: Sent BE");
	rpkt[0] = ++(s->rx_cookie);
	tos = TOS_BK;
	rpkt[1] = tos;
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
	res = send(s->sock, rpkt, s->payload_size / 2, 0);
	if (res >= 0) {
		s->tx_frames++;
		s->tx_payload_bytes += res;
	}
	sigma_dut_print(dut, DUT_MSG_INFO,
			"console_rx_vo_tx_all_tx_stop: Sent BK");
	usleep(500000);
	console_send(s, APTS_STOP, TOS_BE);

	return 0;
}


static int console_rx_be_tx_be(struct  sigma_stream *s, unsigned int *rpkt,
			       int len)
{
	struct sigma_dut *dut = s->dut;
	u32 tos = TOS_BE;
	int res;

	if (!packet_expected(s, rpkt, APTS_DEFAULT, TOS_BE)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_be_tx_be: Expected Pkt not Recv or Bad TOS");
		return 0;
	}

	rpkt[0] = ++(s->rx_cookie);
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
	res = send(s->sock, rpkt, s->payload_size / 2, 0);
	if (res >= 0) {
		s->tx_frames++;
		s->tx_payload_bytes += res;
	}
	s->uapsd_rx_state++;
	sigma_dut_print(dut, DUT_MSG_INFO,
			"console_rx_be_tx_be: Recv BE, Sent BE");

	return 0;
}


static int console_rx_be_tx_be_tx_stop(struct sigma_stream *s,
				       unsigned int *rpkt, int len)
{
	struct sigma_dut *dut = s->dut;
	u32 tos = TOS_BE;
	int res;

	if (!packet_expected(s, rpkt, APTS_DEFAULT, TOS_BE)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_be_tx_be_tx_stop: Expected Pkt not Recv or Bad TOS");
		return 0;
	}

	rpkt[0] = ++(s->rx_cookie);
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
	res = send(s->sock, rpkt, s->payload_size / 2, 0);
	if (res >= 0) {
		s->tx_frames++;
		s->tx_payload_bytes += res;
	}
	sigma_dut_print(dut, DUT_MSG_INFO,
			"console_rx_be_tx_be_tx_stop: Recv BE, Sent BE");
	console_send(s, APTS_STOP, TOS_BE);

	return 0;
}


static int console_rx_vi_tx_be(struct sigma_stream *s, unsigned int *rpkt,
			       int len)
{
	struct sigma_dut *dut = s->dut;
	u32 tos = TOS_BE;
	int res;

	if (!packet_expected(s, rpkt, APTS_DEFAULT, TOS_VI)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_vi_tx_be: Expected Pkt not Recv or Bad TOS");
		return 0;
	}

	rpkt[0] = ++(s->rx_cookie);
	rpkt[1] = tos;
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
	res = send(s->sock, rpkt, s->payload_size / 2, 0);
	if (res >= 0) {
		s->tx_frames++;
		s->tx_payload_bytes += res;
	}
	s->uapsd_rx_state++;
	sigma_dut_print(dut, DUT_MSG_INFO,
			"console_rx_vi_tx_be: Recv VI, Sent BE");

	return 0;
}


static int console_rx_vi_tx_bk(struct sigma_stream *s, unsigned int *rpkt,
			       int len)
{
	struct sigma_dut *dut = s->dut;
	u32 tos = TOS_BK;
	int res;

	if (!packet_expected(s, rpkt, APTS_DEFAULT, TOS_VI)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_vi_tx_bk: Expected Pkt not Recv or Bad TOS");
		return 0;
	}

	rpkt[0] = ++(s->rx_cookie);
	rpkt[1] = tos;
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
	res = send(s->sock, rpkt, s->payload_size / 2, 0);
	if (res >= 0) {
		s->tx_frames++;
		s->tx_payload_bytes += res;
	}
	s->uapsd_rx_state++;
	sigma_dut_print(dut, DUT_MSG_INFO,
			"console_rx_vi_tx_bk: Recv VI, Sent BK");

	return 0;
}


static int console_rx_vo_tx_bcst_tx_stop(struct sigma_stream *s,
					 unsigned int *rpkt, int len)
{
	struct sigma_dut *dut = s->dut;
	u32 tos = TOS_BE;
	int res;
	int broadcast = 1;
	struct sockaddr_in bcst_addr;

	if (!packet_expected(s, rpkt, APTS_DEFAULT, TOS_VO)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_vo_tx_bcst_tx_stop: Expected Pkt not Recv or Bad TOS");
		return 0;
	}

	memset(&bcst_addr, 0, sizeof(struct sockaddr_in));
	bcst_addr.sin_family = AF_INET;
	bcst_addr.sin_port = htons(s->dst_port);
	bcst_addr.sin_addr.s_addr = (s->dst.s_addr | ~(NETWORK_CLASS_C));

	usleep(300000);
	rpkt[0] = ++(s->rx_cookie);
	rpkt[1] = tos;
	setsockopt(s->sock, SOL_SOCKET, SO_BROADCAST, &broadcast,
		   sizeof(broadcast));
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
	res = sendto(s->sock, rpkt, s->payload_size / 2, 0,
		     (struct sockaddr *) &bcst_addr, sizeof(bcst_addr));
	if (res >= 0) {
		s->tx_frames++;
		s->tx_payload_bytes += res;
	}
	sigma_dut_print(dut, DUT_MSG_INFO,
			"console_rx_vo_tx_bcst_tx_stop: Recv VO, Sent Broadcast res = %d",
			res);
	console_send(s, APTS_STOP, TOS_BE);

	return 0;
}


static int console_rx_vi_tx_bcst(struct sigma_stream *s, unsigned int *rpkt,
				 int len)
{
	struct sigma_dut *dut = s->dut;
	u32 tos = TOS_VI;
	int res;
	int broadcast = 1;
	struct sockaddr_in bcst_addr;

	if (!packet_expected(s, rpkt, APTS_DEFAULT, TOS_VI)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_vi_tx_bcst: Expected Pkt not Recv or Bad TOS");
		return 0;
	}

	memset(&bcst_addr, 0, sizeof(struct sockaddr_in));
	bcst_addr.sin_family = AF_INET;
	bcst_addr.sin_port = htons(s->dst_port);
	bcst_addr.sin_addr.s_addr = (s->dst.s_addr | ~(NETWORK_CLASS_C));

	rpkt[0] = ++(s->rx_cookie);
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
	res = send(s->sock, rpkt, s->payload_size / 2, 0);
	if (res >= 0) {
		s->tx_frames++;
		s->tx_payload_bytes += res;
	}
	tos = TOS_BE;
	rpkt[0] = ++(s->rx_cookie);
	rpkt[1] = tos;
	setsockopt(s->sock, SOL_SOCKET, SO_BROADCAST, &broadcast,
		   sizeof(broadcast));
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
	res = sendto(s->sock, rpkt, s->payload_size / 2, 0,
		     (struct sockaddr *) &bcst_addr, sizeof(bcst_addr));
	if (res >= 0) {
		s->tx_frames++;
		s->tx_payload_bytes += res;
	}
	sigma_dut_print(dut, DUT_MSG_INFO,
			"console_rx_vi_tx_bcst: Recv/Sent VI, Sent Broadcast res = %d",
			res);
	s->uapsd_rx_state++;

	return 0;
}


static int console_rx_be_tx_bcst(struct sigma_stream *s, unsigned int *rpkt,
				 int len)
{
	struct sigma_dut *dut = s->dut;
	u32 tos = TOS_BE;
	int res;
	int broadcast = 1;
	struct sockaddr_in bcst_addr;

	if (!packet_expected(s, rpkt, APTS_DEFAULT, TOS_BE)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_be_tx_bcst: Expected Pkt not Recv or Bad TOS");
		return 0;
	}

	memset(&bcst_addr, 0, sizeof(struct sockaddr_in));
	bcst_addr.sin_family = AF_INET;
	bcst_addr.sin_port = htons(s->dst_port);
	bcst_addr.sin_addr.s_addr = (s->dst.s_addr | ~(NETWORK_CLASS_C));

	rpkt[0] = ++(s->rx_cookie);
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
	res = send(s->sock, rpkt, s->payload_size / 2, 0);
	if (res >= 0) {
		s->tx_frames++;
		s->tx_payload_bytes += res;
	}
	rpkt[0] = ++(s->rx_cookie);
	rpkt[1] = tos;
	setsockopt(s->sock, SOL_SOCKET, SO_BROADCAST, &broadcast,
		   sizeof(broadcast));
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
	res = sendto(s->sock, rpkt, s->payload_size / 2, 0,
		     (struct sockaddr *) &bcst_addr, sizeof(bcst_addr));
	if (res >= 0) {
		s->tx_frames++;
		s->tx_payload_bytes += res;
	}
	sigma_dut_print(dut, DUT_MSG_INFO,
			"console_rx_be_tx_bcst: Recv/Sent BE, Sent Broadcast res = %d",
			res);
	s->uapsd_rx_state++;

	return 0;
}


static int console_rx_be_tx_bcst_tx_stop(struct sigma_stream *s,
					 unsigned int *rpkt, int len)
{
	struct sigma_dut *dut = s->dut;
	u32 tos = TOS_BE;
	int res;
	int broadcast = 1;
	struct sockaddr_in bcst_addr;

	if (!packet_expected(s, rpkt, APTS_DEFAULT, TOS_BE)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_be_tx_bcst_tx_stop: Expected Pkt not Recv or Bad TOS");
		return 0;
	}

	memset(&bcst_addr, 0, sizeof(struct sockaddr_in));
	bcst_addr.sin_family = AF_INET;
	bcst_addr.sin_port = htons(s->dst_port);
	bcst_addr.sin_addr.s_addr = (s->dst.s_addr | ~(NETWORK_CLASS_C));

	rpkt[0] = ++(s->rx_cookie);
	setsockopt(s->sock, SOL_SOCKET, SO_BROADCAST, &broadcast,
		   sizeof(broadcast));
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
	res = sendto(s->sock, rpkt, s->payload_size / 2, 0,
		     (struct sockaddr *) &bcst_addr, sizeof(bcst_addr));
	if (res >= 0) {
		s->tx_frames++;
		s->tx_payload_bytes += res;
	}
	sigma_dut_print(dut, DUT_MSG_INFO,
			"console_rx_be_tx_bcst_tx_stop: Recv BE, Sent Broadcast res = %d",
			res);
	console_send(s, APTS_STOP, TOS_BE);

	return 0;
}


static int console_rx_vi_tx_vi(struct sigma_stream *s, unsigned int *rpkt,
			       int len)
{
	struct sigma_dut *dut = s->dut;
	u32 tos = TOS_VI;
	int res;

	if (!packet_expected(s, rpkt, APTS_DEFAULT, TOS_VI)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_vi_tx_vi: Expected Pkt not Recv or Bad TOS");
		return 0;
	}

	rpkt[0] = ++(s->rx_cookie);
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
	res = send(s->sock, rpkt, s->payload_size / 2, 0);
	if (res >= 0) {
		s->tx_frames++;
		s->tx_payload_bytes += res;
	}
	s->uapsd_rx_state++;
	sigma_dut_print(dut, DUT_MSG_INFO,
			"console_rx_vi_tx_vi: Recv VI, Sent VI");

	return 0;
}


static int console_rx_vi_tx_vi_tx_stop(struct sigma_stream *s,
				       unsigned int *rpkt, int len)
{
	struct sigma_dut *dut = s->dut;
	u32 tos = TOS_VI;
	int res;

	if (!packet_expected(s, rpkt, APTS_DEFAULT, TOS_VI)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_vi_tx_vi_tx_stop: Expected Pkt not Recv or Bad TOS");
		return 0;
	}

	rpkt[0] = ++(s->rx_cookie);
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
	res = send(s->sock, rpkt, s->payload_size / 2, 0);
	if (res >= 0) {
		s->tx_frames++;
		s->tx_payload_bytes += res;
	}
	sigma_dut_print(dut, DUT_MSG_INFO,
			"console_rx_vi_tx_vi_tx_stop: Recv VI, Sent VI");
	console_send(s, APTS_STOP, TOS_BE);

	return 0;
}


static int console_rx_vi_tx_vo_tx_stop(struct sigma_stream *s,
				       unsigned int *rpkt, int len)
{
	struct sigma_dut *dut = s->dut;
	u32 tos = TOS_VO7;
	int res;

	if (!packet_expected(s, rpkt, APTS_DEFAULT, TOS_VI)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_vi_tx_vo_tx_stop: Expected Pkt not Recv or Bad TOS");
		return 0;
	}

	rpkt[0] = ++(s->rx_cookie);
	rpkt[1] = TOS_VO;
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
	res = send(s->sock, rpkt, s->payload_size / 2, 0);
	if (res >= 0) {
		s->tx_frames++;
		s->tx_payload_bytes += res;
	}
	sigma_dut_print(dut, DUT_MSG_INFO,
			"console_rx_vi_tx_vo_tx_stop: Recv VI, Sent VO");
	console_send(s, APTS_STOP, TOS_BE);

	return 0;
}


static int console_rx_vo_tx_stop(struct sigma_stream *s, unsigned int *rpkt,
				 int len)
{
	struct sigma_dut *dut = s->dut;

	if (!packet_expected(s, rpkt, APTS_DEFAULT, TOS_VO)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_vo_tx_stop: Expected Pkt not Recv or Bad TOS");
		return 0;
	}

	sigma_dut_print(dut, DUT_MSG_INFO, "console_rx_vo_tx_stop: Recv VO");
	sleep(1);
	console_send(s, APTS_STOP, TOS_BE);

	return 0;
}


static int console_rx_vi_tx_stop(struct sigma_stream *s, unsigned int *rpkt,
				 int len)
{
	struct sigma_dut *dut = s->dut;

	if (!packet_expected(s, rpkt, APTS_DEFAULT, TOS_VI)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_vi_tx_stop: Expected Pkt not Recv or Bad TOS");
		return 0;
	}

	sigma_dut_print(dut, DUT_MSG_INFO, "console_rx_vi_tx_stop: Recv VI");
	console_send(s, APTS_STOP, TOS_BE);

	return 0;
}


static int console_rx_bk_tx_stop(struct  sigma_stream *s, unsigned int *rpkt,
				 int len)
{
	struct sigma_dut *dut = s->dut;

	if (!packet_expected(s, rpkt, APTS_DEFAULT, TOS_BK)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_bk_tx_stop: Expected Pkt not Recv or Bad TOS");
		return 0;
	}

	sigma_dut_print(dut, DUT_MSG_INFO, "console_rx_bk_tx_stop: Recv BK");
	console_send(s, APTS_STOP, TOS_BE);

	return 0;
}


static int console_rx_vo_tx_2vo(struct sigma_stream *s, unsigned int *rpkt,
				int len)
{
	struct sigma_dut *dut = s->dut;
	u32 tos = TOS_VO7;
	int res;

	if (!packet_expected(s, rpkt, APTS_DEFAULT, TOS_VO)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_vo_tx_2vo: Expected Pkt not Recv or Bad TOS");
		return 0;
	}

	rpkt[0] = ++(s->rx_cookie);
	rpkt[1] = TOS_VO;
	setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
	res = send(s->sock, rpkt, s->payload_size / 2, 0);
	if (res >= 0) {
		s->tx_frames++;
		s->tx_payload_bytes += res;
	}
	rpkt[0] = ++(s->rx_cookie);
	res = send(s->sock, rpkt, s->payload_size / 2, 0);
	if (res >= 0) {
		s->tx_frames++;
		s->tx_payload_bytes += res;
	}
	s->uapsd_rx_state++;
	sigma_dut_print(dut, DUT_MSG_INFO,
			"console_rx_vo_tx_2vo: Recv VO, Sent 2 VO");

	return 0;
}


static int console_rx_be(struct sigma_stream *s, unsigned int *rpkt, int len)
{
	struct sigma_dut *dut = s->dut;

	if (!packet_expected(s, rpkt, APTS_DEFAULT, TOS_BE)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_be: Expected Pkt not Recv or Bad TOS");
		return 0;
	}

	s->uapsd_rx_state++;
	sigma_dut_print(dut, DUT_MSG_INFO, "console_rx_be: Recv BE");

	return 0;
}


static int console_rx_vi(struct sigma_stream *s, unsigned int *rpkt, int len)
{
	struct sigma_dut *dut = s->dut;

	if (!packet_expected(s, rpkt, APTS_DEFAULT, TOS_VI)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_vi: Expected Pkt not Recv or Bad TOS");
		return 0;
	}

	s->uapsd_rx_state++;
	sigma_dut_print(dut, DUT_MSG_INFO, "console_rx_vi: Recv VI");

	return 0;
}


static int console_rx_vo(struct sigma_stream *s, unsigned int *rpkt, int len)
{
	struct sigma_dut *dut = s->dut;

	if (!packet_expected(s, rpkt, APTS_DEFAULT, TOS_VO)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"console_rx_vo : Expected Pkt not Recv or Bad TOS");
		return 0;
	}

	s->uapsd_rx_state++;
	sigma_dut_print(dut, DUT_MSG_INFO, "console_rx_vo: Recv VO");

	return 0;

}


static int console_rx_vo_tx_vo_cyclic(struct sigma_stream *s,
				      unsigned int *rpkt, int len)
{
	int res = 0;
	unsigned int tos = 0;
	unsigned int *tpkt;
	struct sigma_dut *dut = s->dut;

	tpkt = malloc(s->payload_size);
	if (tpkt == NULL)
		return -1;

	if (!packet_expected(s, rpkt, APTS_DEFAULT, TOS_VO)) {
		if (rpkt[10] != APTS_STOP)
			sigma_uapsd_reset(s);

		if (!packet_expected(s, rpkt, APTS_STOP, TOS_BE)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"console_rx_vo_tx_vo_cyclic: Expected STOP Pkt not Recv or Bad TOS");
			free(tpkt);
			return 0;
		}

		memset(tpkt, 0, s->payload_size);
		tpkt[0] = s->rx_cookie;
		tos = TOS_BE;
		setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
		create_apts_pkt(APTS_STOP, tpkt, s->payload_size, tos, s);
		tpkt[1] = tos;
		if (s->can_quit) {
			const char *stop_cmd = "APTSL1 STOP";
			size_t stop_cmd_len = strlen(stop_cmd);

			if (s->payload_size > 11 * sizeof(int) + stop_cmd_len)
				memcpy(&tpkt[11], stop_cmd, stop_cmd_len + 1);
			res = send(s->sock, tpkt, s->payload_size / 2, 0);
			if (res >= 0) {
				s->tx_frames++;
				s->tx_payload_bytes += res;
			}
			sigma_dut_print(dut, DUT_MSG_INFO,
					"console_rx_vo_tx_vo_cyclic: Sent STOP");
			sleep(5);
			s->stop = 1;
		} else {
			res = send(s->sock, tpkt, s->payload_size / 2, 0);
			if (res >= 0) {
				s->tx_frames++;
				s->tx_payload_bytes += res;
			}
			s->can_quit = 1;
		}
	} else {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"console_rx_vo_tx_vo_cyclic: Recv Pkt: %d Cookie: %d Sta-id %d",
				rpkt[0], s->rx_cookie, s->sta_id);
		rpkt[0] = ++(s->rx_cookie);
		tos = TOS_VO7;
		rpkt[1] = TOS_VO;
		res = setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos,
				 sizeof(tos));
		res = send(s->sock, rpkt, s->payload_size / 2, 0);
		if (res >= 0) {
			s->tx_frames++;
			s->tx_payload_bytes += res;
		}

		if (s->rx_cookie >= 3000) {
			/* No state change for L.1 */
		}
	}

	free(tpkt);

	return 0;
}


static struct apts_pkt * apts_lookup(const char *s)
{
	struct apts_pkt *t;

	for (t = &apts_pkts[1]; s && t->cmd; t++) {
		if (strcmp(s, "L.1AP") == 0)
			s = "L.1";
		if (t->name && strcmp(t->name, s) == 0)
			return t;
	}

	return NULL;
}


void send_uapsd_console(struct sigma_stream *s)
{
	struct timeval tv;
	fd_set rfds;
	int res;
	unsigned int *rpkt;
	uapsd_console_state_func_ptr console_state_func;
	struct apts_pkt *testcase;
	struct sigma_dut *dut = s->dut;
	/* start timer for self exit */
	int uapsd_timer = UAPSD_CONSOLE_TIMER;

	s->can_quit = 0;
	s->reset = 0;
	s->reset_rx = 0;
	s->uapsd_sta_tc = 0;

	testcase = apts_lookup(s->test_name);
	if (testcase == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"send_uapsd_console: no testcase found");
		return;
	}

	/* send test case number to be executed */
	s->uapsd_sta_tc = testcase->cmd;
	if (s->uapsd_sta_tc < B_D || s->uapsd_sta_tc > LAST_TC) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"send_uapsd_console: Test Case: %s Unknown",
				s->test_name);
		return;
	}

	s->payload_size = 512;
	rpkt = malloc(s->payload_size);
	if (rpkt == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"send_uapsd_console: buffer allocation failed");
		return;
	}

	sigma_dut_print(dut, DUT_MSG_INFO,
			"send_uapsd_console: Uapsd Console Start");

	while (!s->stop) {
		uapsd_timer--;
		FD_ZERO(&rfds);
		FD_SET(s->sock, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 300000;
		res = select(s->sock + 1, &rfds, NULL, NULL, &tv);
		if (res < 0) {
			perror("select");
			usleep(10000);
		} else if (FD_ISSET(s->sock, &rfds)) {
			memset(rpkt, 0, s->payload_size);
			res = recv(s->sock, rpkt, s->payload_size, 0);
			if (res < 0) {
				perror("recv");
				break;
			}

			sigma_dut_print(dut, DUT_MSG_INFO,
					"send_uapsd_console: running res %d cookie %d dscp %d apts-pkt %d sta-id %d",
					res, rpkt[0], rpkt[1], rpkt[10],
					rpkt[9]);
			if (res == 0)
				continue;

			s->rx_frames++;
			s->rx_payload_bytes += res;
			/*
			 * Reset the timer as packet is received
			 * within steps.
			 */
			uapsd_timer = UAPSD_CONSOLE_TIMER;

			if (rpkt[10] == APTS_HELLO) {
				if (s->reset)
					s->reset = 0;
				s->rx_cookie = 0;
				/* assign a unique id to this sta */
				s->sta_id = s->stream_id;
				/* uapsd console process table state */
				s->uapsd_rx_state = 0;
				s->can_quit = 1;
			} else {
				if (s->reset)
					continue;
			}

			if (rpkt[10] == APTS_RESET) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"send_uapsd_console: RESET Pkt recv");
				s->reset_rx = 1;
				sigma_uapsd_reset(s);
			}

			if (rpkt[10] == APTS_RESET_STOP) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"send_uapsd_console: RESET STOP Pkt recv");
				s->stop = 1;
			}

			if (rpkt[10] == APTS_BCST) {
				sigma_dut_print(dut, DUT_MSG_INFO,
						"send_uapsd_console: Broadcast Pkt recv");
				continue;
			}

			console_state_func =
				uapsd_console_state_tbl[s->uapsd_sta_tc]
				[s->uapsd_rx_state].state_func;
			if (console_state_func) {
				console_state_func(s, rpkt, res);
			} else {
				sigma_dut_print(dut, DUT_MSG_INFO,
						"send_uapsd_console: Null function detected for TC: %d in uapsd_rx_state: %d",
						s->uapsd_sta_tc,
						s->uapsd_rx_state);
			}
		}

		/* Stop the thread. No transactions for the set time */
		if (uapsd_timer == 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"send_uapsd_console: Timer Expired");
			s->stop = 1;
		}
	}

	free(rpkt);
	sigma_dut_print(dut, DUT_MSG_INFO,
			"send_uapsd_console: Uapsd Console End");
}
