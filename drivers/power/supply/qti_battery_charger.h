// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (C) 2019-2021 The Linux Foundation. All rights reserved.
//               2022      The LineageOS Project
//

#define MSG_OWNER_BC			32778
#define MSG_TYPE_REQ_RESP		1
#define MSG_TYPE_NOTIFY			2

/* adapter xiaomi */
#define ADAPTER_NONE              0x0
#define ADAPTER_XIAOMI_QC3_20W    0x9
#define ADAPTER_XIAOMI_PD_20W     0xa
#define ADAPTER_XIAOMI_CAR_20W    0xb
#define ADAPTER_XIAOMI_PD_30W     0xc
#define ADAPTER_VOICE_BOX_30W     0xd
#define ADAPTER_XIAOMI_PD_50W     0xe
#define ADAPTER_XIAOMI_PD_60W     0xf
#define ADAPTER_XIAOMI_PD_100W    0x10

/* opcode for battery charger */
#define BC_XM_STATUS_GET		0x50
#define BC_XM_STATUS_SET		0x51
#define BC_SET_NOTIFY_REQ		0x04
#define BC_NOTIFY_IND			0x07
#define BC_BATTERY_STATUS_GET		0x30
#define BC_BATTERY_STATUS_SET		0x31
#define BC_USB_STATUS_GET		0x32
#define BC_USB_STATUS_SET		0x33
#define BC_WLS_STATUS_GET		0x34
#define BC_WLS_STATUS_SET		0x35
#define BC_SHIP_MODE_REQ_SET		0x36
#define BC_SHUTDOWN_REQ_SET		0x37
#define BC_WLS_FW_CHECK_UPDATE		0x40
#define BC_WLS_FW_PUSH_BUF_REQ		0x41
#define BC_WLS_FW_UPDATE_STATUS_RESP	0x42
#define BC_WLS_FW_PUSH_BUF_RESP		0x43
#define BC_WLS_FW_GET_VERSION		0x44
#define BC_SHUTDOWN_NOTIFY		0x47
#define BC_GENERIC_NOTIFY		0x80

/* Generic definitions */
#define MAX_STR_LEN			128
#define BC_WAIT_TIME_MS			1000
#define WLS_FW_PREPARE_TIME_MS		300
#define WLS_FW_WAIT_TIME_MS		500
#define WLS_FW_UPDATE_TIME_MS		1000
#define WLS_FW_BUF_SIZE			128
#define DEFAULT_RESTRICT_FCC_UA		1000000

#if defined(CONFIG_BQ_FG_1S)
#define BATTERY_DIGEST_LEN 32
#else
#define BATTERY_DIGEST_LEN 20
#endif
#define BATTERY_SS_AUTH_DATA_LEN 4

#define ADAP_TYPE_SDP		1
#define ADAP_TYPE_CDP		3
#define ADAP_TYPE_DCP		2
#define ADAP_TYPE_PD		6

#define USBPD_UVDM_SS_LEN		4
#define USBPD_UVDM_VERIFIED_LEN		1

#define MAX_THERMAL_LEVEL		16

enum uvdm_state {
	USBPD_UVDM_DISCONNECT,
	USBPD_UVDM_CHARGER_VERSION,
	USBPD_UVDM_CHARGER_VOLTAGE,
	USBPD_UVDM_CHARGER_TEMP,
	USBPD_UVDM_SESSION_SEED,
	USBPD_UVDM_AUTHENTICATION,
	USBPD_UVDM_VERIFIED,
	USBPD_UVDM_REMOVE_COMPENSATION,
	USBPD_UVDM_REVERSE_AUTHEN,
	USBPD_UVDM_CONNECT,
};

enum usb_connector_type {
	USB_CONNECTOR_TYPE_TYPEC,
	USB_CONNECTOR_TYPE_MICRO_USB,
};

enum psy_type {
	PSY_TYPE_BATTERY,
	PSY_TYPE_USB,
	PSY_TYPE_WLS,
	PSY_TYPE_XM,
	PSY_TYPE_MAX,
};

enum ship_mode_type {
	SHIP_MODE_PMIC,
	SHIP_MODE_PACK_SIDE,
};

/* property ids */
enum battery_property_id {
	BATT_STATUS,
	BATT_HEALTH,
	BATT_PRESENT,
	BATT_CHG_TYPE,
	BATT_CAPACITY,
	BATT_SOH,
	BATT_VOLT_OCV,
	BATT_VOLT_NOW,
	BATT_VOLT_MAX,
	BATT_CURR_NOW,
	BATT_CHG_CTRL_LIM,
	BATT_CHG_CTRL_LIM_MAX,
	BATT_CONSTANT_CURRENT,
	BATT_TEMP,
	BATT_TECHNOLOGY,
	BATT_CHG_COUNTER,
	BATT_CYCLE_COUNT,
	BATT_CHG_FULL_DESIGN,
	BATT_CHG_FULL,
	BATT_MODEL_NAME,
	BATT_TTF_AVG,
	BATT_TTE_AVG,
	BATT_RESISTANCE,
	BATT_POWER_NOW,
	BATT_POWER_AVG,
	BATT_PROP_MAX,
};

enum usb_property_id {
	USB_ONLINE,
	USB_VOLT_NOW,
	USB_VOLT_MAX,
	USB_CURR_NOW,
	USB_CURR_MAX,
	USB_INPUT_CURR_LIMIT,
	USB_TYPE,
	USB_ADAP_TYPE,
	USB_MOISTURE_DET_EN,
	USB_MOISTURE_DET_STS,
	USB_TEMP,
	USB_REAL_TYPE,
	USB_TYPEC_COMPLIANT,
	USB_SCOPE,
	USB_CONNECTOR_TYPE,
	USB_PROP_MAX,
};

enum wireless_property_id {
	WLS_ONLINE,
	WLS_VOLT_NOW,
	WLS_VOLT_MAX,
	WLS_CURR_NOW,
	WLS_CURR_MAX,
	WLS_TYPE,
	WLS_BOOST_EN,
	WLS_FW_VER,
	WLS_TX_ADAPTER,
	WLS_REGISTER,
	WLS_INPUT_CURR,
	WLS_PROP_MAX,
};

enum xm_property_id {
	XM_PROP_RESISTANCE_ID,
	XM_PROP_VERIFY_DIGEST,
	XM_PROP_CONNECTOR_TEMP,
	XM_PROP_AUTHENTIC,
	XM_PROP_CHIP_OK,
#if defined(CONFIG_DUAL_FUEL_GAUGE)
	XM_PROP_SLAVE_CHIP_OK,
	XM_PROP_SLAVE_AUTHENTIC,
	XM_PROP_FG1_VOL,
	XM_PROP_FG1_SOC,
	XM_PROP_FG1_TEMP,
	XM_PROP_FG1_IBATT,
	XM_PROP_FG2_VOL,
	XM_PROP_FG2_SOC,
	XM_PROP_FG2_TEMP,
	XM_PROP_FG2_IBATT,
	XM_PROP_FG2_QMAX,
	XM_PROP_FG2_RM,
	XM_PROP_FG2_FCC,
	XM_PROP_FG2_SOH,
	XM_PROP_FG2_FCC_SOH,
	XM_PROP_FG2_CYCLE,
	XM_PROP_FG2_FAST_CHARGE,
	XM_PROP_FG2_CURRENT_MAX,
	XM_PROP_FG2_VOL_MAX,
	XM_PROP_FG2_TSIM,
	XM_PROP_FG2_TAMBIENT,
	XM_PROP_FG2_TREMQ,
	XM_PROP_FG2_TFULLQ,
	XM_PROP_IS_OLD_HW,
#endif
	XM_PROP_SOC_DECIMAL,
	XM_PROP_SOC_DECIMAL_RATE,
	XM_PROP_SHUTDOWN_DELAY,
	XM_PROP_VBUS_DISABLE,
	XM_PROP_CC_ORIENTATION,
	XM_PROP_SLAVE_BATT_PRESENT,
#if defined(CONFIG_BQ2597X)
	XM_PROP_BQ2597X_CHIP_OK,
	XM_PROP_BQ2597X_SLAVE_CHIP_OK,
	XM_PROP_BQ2597X_BUS_CURRENT,
	XM_PROP_BQ2597X_SLAVE_BUS_CURRENT,
	XM_PROP_BQ2597X_BUS_DELTA,
	XM_PROP_BQ2597X_BUS_VOLTAGE,
	XM_PROP_BQ2597X_BATTERY_PRESENT,
	XM_PROP_BQ2597X_SLAVE_BATTERY_PRESENT,
	XM_PROP_BQ2597X_BATTERY_VOLTAGE,
	XM_PROP_COOL_MODE,
#endif
#if defined(CONFIG_REDWOOD_FOR_BUILD)
	XM_PROP_BQ2597X_SLAVE_CONNECTOR,
#endif
#if !defined(CONFIG_VENUS_FOR_BUILD)
	XM_PROP_BT_TRANSFER_START,
#endif
	XM_PROP_MASTER_SMB1396_ONLINE,
	XM_PROP_MASTER_SMB1396_IIN,
	XM_PROP_SLAVE_SMB1396_ONLINE,
	XM_PROP_SLAVE_SMB1396_IIN,
	XM_PROP_SMB_IIN_DIFF,
	/* wireless charge infor */
	XM_PROP_TX_MACL,
	XM_PROP_TX_MACH,
	XM_PROP_RX_CRL,
	XM_PROP_RX_CRH,
	XM_PROP_RX_CEP,
	XM_PROP_BT_STATE,
	XM_PROP_REVERSE_CHG_MODE,
	XM_PROP_REVERSE_CHG_STATE,
#if !defined(CONFIG_VENUS_FOR_BUILD)
	XM_PROP_WLS_FW_STATE,
#endif
	XM_PROP_RX_VOUT,
	XM_PROP_RX_VRECT,
	XM_PROP_RX_IOUT,
	XM_PROP_TX_ADAPTER,
	XM_PROP_OP_MODE,
	XM_PROP_WLS_DIE_TEMP,
	XM_PROP_WLS_CAR_ADAPTER,
#if !defined(CONFIG_VENUS_FOR_BUILD)
	XM_PROP_WLS_TX_SPEED,
#endif
	/**********************/
	XM_PROP_INPUT_SUSPEND,
	XM_PROP_REAL_TYPE,
	/*used for pd authentic*/
	XM_PROP_VERIFY_PROCESS,
	XM_PROP_VDM_CMD_CHARGER_VERSION,
	XM_PROP_VDM_CMD_CHARGER_VOLTAGE,
	XM_PROP_VDM_CMD_CHARGER_TEMP,
	XM_PROP_VDM_CMD_SESSION_SEED,
	XM_PROP_VDM_CMD_AUTHENTICATION,
	XM_PROP_VDM_CMD_VERIFIED,
	XM_PROP_VDM_CMD_REMOVE_COMPENSATION,
#if !defined(CONFIG_VENUS_FOR_BUILD)
	XM_PROP_VDM_CMD_REVERSE_AUTHEN,
#endif
	XM_PROP_CURRENT_STATE,
	XM_PROP_ADAPTER_ID,
	XM_PROP_ADAPTER_SVID,
	XM_PROP_PD_VERIFED,
	XM_PROP_PDO2,
	XM_PROP_UVDM_STATE,
#if !defined(CONFIG_VENUS_FOR_BUILD) && !defined(CONFIG_REDWOOD_FOR_BUILD)
	/* use for MI SMART INTERCHG */
	XM_PROP_VDM_CMD_SINK_SOC,
#endif
	/*****************/
	XM_PROP_WLS_BIN,
	XM_PROP_FASTCHGMODE,
	XM_PROP_APDO_MAX,
	XM_PROP_THERMAL_REMOVE,
	XM_PROP_VOTER_DEBUG,
	XM_PROP_FG_RM,
	XM_PROP_WLSCHARGE_CONTROL_LIMIT,
	XM_PROP_MTBF_CURRENT,
	XM_PROP_FAKE_TEMP,
	XM_PROP_QBG_VBAT,
	XM_PROP_QBG_VPH_PWR,
	XM_PROP_QBG_TEMP,
	XM_PROP_FB_BLANK_STATE,
	XM_PROP_THERMAL_TEMP,
	XM_PROP_TYPEC_MODE,
	XM_PROP_NIGHT_CHARGING,
#if !defined(CONFIG_VENUS_FOR_BUILD)
	XM_PROP_SMART_BATT,
	XM_PROP_FG1_QMAX,
	XM_PROP_FG1_RM,
	XM_PROP_FG1_FCC,
	XM_PROP_FG1_SOH,
	XM_PROP_FG1_FCC_SOH,
	XM_PROP_FG1_CYCLE,
	XM_PROP_FG1_FAST_CHARGE,
	XM_PROP_FG1_CURRENT_MAX,
	XM_PROP_FG1_VOL_MAX,
	XM_PROP_FG1_TSIM,
	XM_PROP_FG1_TAMBIENT,
	XM_PROP_FG1_TREMQ,
	XM_PROP_FG1_TFULLQ,
#if !defined(CONFIG_REDWOOD_FOR_BUILD)
	XM_PROP_FG_UPDATE_TIME,
#endif
#if defined(CONFIG_REDWOOD_FOR_BUILD)
	XM_PROP_FG1_SEAL_STATE,
	XM_PROP_FG1_SEAL_SET,
	XM_PROP_FG1_DF_CHECK,
	XM_PROP_FG1_VOLTAGE_MAX,
	XM_PROP_FG1_Charge_Current_MAX,
	XM_PROP_FG1_Discharge_Current_MAX,
	XM_PROP_FG1_TEMP_MAX,
	XM_PROP_FG1_TEMP_MIN,
	XM_PROP_FG1_TIME_HT,
	XM_PROP_FG1_TIME_OT,
	XM_PROP_FG1_TIME_UT,
	XM_PROP_FG1_TIME_LT,
#endif
#if defined(CONFIG_BQ_CLOUD_AUTHENTICATION)
	XM_PROP_SERVER_SN,
	XM_PROP_SERVER_RESULT,
	XM_PROP_ADSP_RESULT,
#endif
#if defined(CONFIG_REDWOOD_FOR_BUILD)
	XM_PROP_SHIPMODE_COUNT_RESET,
	XM_PROP_SPORT_MODE,
	XM_PROP_CELL1_VOLT,
	XM_PROP_CELL2_VOLT,
	XM_PROP_FG_VENDOR_ID,
#endif
#if defined(CONFIG_AI_RSOC_M20)
	XM_PROP_FG1_RSOC,
	XM_PROP_FG1_AI,
#endif
#endif
	XM_PROP_MAX,
};
enum {
	QTI_POWER_SUPPLY_USB_TYPE_HVDCP = 0x80,
	QTI_POWER_SUPPLY_USB_TYPE_HVDCP_3,
	QTI_POWER_SUPPLY_USB_TYPE_HVDCP_3P5,
	QTI_POWER_SUPPLY_USB_TYPE_USB_FLOAT,
	QTI_POWER_SUPPLY_USB_TYPE_HVDCP_3_CLASSB,
};

struct battery_charger_set_notify_msg {
	struct pmic_glink_hdr	hdr;
	u32			battery_id;
	u32			power_state;
	u32			low_capacity;
	u32			high_capacity;
};

struct battery_charger_notify_msg {
	struct pmic_glink_hdr	hdr;
	u32			notification;
};

struct battery_charger_req_msg {
	struct pmic_glink_hdr	hdr;
	u32			battery_id;
	u32			property_id;
	u32			value;
};

struct battery_charger_resp_msg {
	struct pmic_glink_hdr	hdr;
	u32			property_id;
	u32			value;
	u32			ret_code;
};

struct wls_fw_resp_msg {
	struct pmic_glink_hdr   hdr;
	u32                     property_id;
	u32			value;
	char                    version[MAX_STR_LEN];
};

struct battery_model_resp_msg {
	struct pmic_glink_hdr	hdr;
	u32			property_id;
	char			model[MAX_STR_LEN];
};


struct xm_verify_digest_resp_msg {
	struct pmic_glink_hdr	hdr;
	u32			property_id;
	u8			digest[BATTERY_DIGEST_LEN];
	bool			slave_fg;
};

struct xm_set_wls_bin_req_msg {
	struct pmic_glink_hdr hdr;
	u32 property_id;
	u16 total_length;
	u8 serial_number;
	u8 fw_area;
	u8 wls_fw_bin[MAX_STR_LEN];
};  /* Message */

struct wireless_fw_check_req {
	struct pmic_glink_hdr	hdr;
	u32			fw_version;
	u32			fw_size;
	u32			fw_crc;
};

struct wireless_fw_check_resp {
	struct pmic_glink_hdr	hdr;
	u32			ret_code;
};

struct wireless_fw_push_buf_req {
	struct pmic_glink_hdr	hdr;
	u8			buf[WLS_FW_BUF_SIZE];
	u32			fw_chunk_id;
};

struct wireless_fw_push_buf_resp {
	struct pmic_glink_hdr	hdr;
	u32			fw_update_status;
};

struct wireless_fw_update_status {
	struct pmic_glink_hdr	hdr;
	u32			fw_update_done;
};

struct wireless_fw_get_version_req {
	struct pmic_glink_hdr	hdr;
};

struct wireless_fw_get_version_resp {
	struct pmic_glink_hdr	hdr;
	u32			fw_version;
};

struct battery_charger_ship_mode_req_msg {
	struct pmic_glink_hdr	hdr;
	u32			ship_mode_type;
};

struct battery_charger_shutdown_req_msg {
	struct pmic_glink_hdr	hdr;
};

struct xm_ss_auth_resp_msg {
	struct pmic_glink_hdr	hdr;
	u32			property_id;
	u32			data[BATTERY_SS_AUTH_DATA_LEN];
};

struct psy_state {
	struct power_supply	*psy;
	char			*model;
	char			*version;
	const int		*map;
	u32			*prop;
	u32			prop_count;
	u32			opcode_get;
	u32			opcode_set;
};

struct battery_chg_dev {
	struct device			*dev;
	struct class			battery_class;
	struct pmic_glink_client	*client;
	struct typec_role_class		*typec_class;
	struct mutex			rw_lock;
	struct completion		ack;
	struct completion		fw_buf_ack;
	struct completion		fw_update_ack;
	struct psy_state		psy_list[PSY_TYPE_MAX];
	struct dentry			*debugfs_dir;
	u8				*digest;
	bool				slave_fg_verify_flag;
	u32				*ss_auth_data;
	/* extcon for VBUS/ID notification for USB for micro USB */
	struct extcon_dev		*extcon;
	u32				*thermal_levels;
	const char			*wls_fw_name;
	int				curr_thermal_level;
	int				curr_wlsthermal_level;
	int				num_thermal_levels;
	atomic_t			state;
	struct work_struct		subsys_up_work;
	struct work_struct		usb_type_work;
	struct work_struct		fb_notifier_work;
	int				fake_soc;
	bool				block_tx;
	bool				ship_mode_en;
	bool				debug_battery_detected;
	bool				wls_fw_update_reqd;
	u32				wls_fw_version;
	u16				wls_fw_crc;
	struct notifier_block		reboot_notifier;
	struct notifier_block 		fb_notifier;
	struct notifier_block		shutdown_notifier;
	int				blank_state;
	u32				thermal_fcc_ua;
	u32				restrict_fcc_ua;
	u32				last_fcc_ua;
	u32				usb_icl_ua;
	u32				reverse_chg_flag;
	u32				hw_version_build;
	u32				connector_type;
	u32				usb_prev_mode;
	bool				restrict_chg_en;
	bool				shutdown_delay_en;
	bool				support_wireless_charge;
	bool				support_2s_charging;
	struct delayed_work		xm_prop_change_work;
	struct delayed_work		charger_debug_info_print_work;
	/* To track the driver initialization status */
	bool				initialized;
};
