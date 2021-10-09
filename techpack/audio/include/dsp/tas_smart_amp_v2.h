#ifndef __TAS_SMART_AMP__
#define __TAS_SMART_AMP__

/*Should be defined in configuration file*/
//#define CONFIG_TAS25XX_ALGO
#define CONFIG_TAS25XX_ALGO_STEREO
#define CONFIG_SET_RE_IN_KERNEL

//FIXME
//update this value at OEM/ODM
//#pragma message ("########### FIXME ################")
#define SMARTAMP_SPEAKER_CALIBDATA_FILE "/mnt/vendor/persist/audio/tas25xx_calib.bin"
//#define SMARTAMP_SPEAKER_CALIBDATA_FILE "/persist/audio/smartamp_calib.bin"

#define APR_VERSION_V2 0
#define APR_VERSION_V3 1

#include <linux/types.h>
//#if APR_VERSION_V2
	#include <dsp/apr_audio-v2.h>
	#include <sound/soc.h>
//#else
//#endif
#include <linux/delay.h>

#define AFE_SMARTAMP_MODULE_RX          0x11111112
#define AFE_SMARTAMP_MODULE_TX          0x11111111

#define CAPI_V2_TAS_TX_ENABLE           0x10012D14
#define CAPI_V2_TAS_TX_CFG              0x10012D16
#define CAPI_V2_TAS_RX_ENABLE           0x10012D13
#define CAPI_V2_TAS_RX_CFG              0x10012D15

#define MAX_DSP_PARAM_INDEX             2048

#define TAS_PAYLOAD_SIZE        14
#define TAS_GET_PARAM           1
#define TAS_SET_PARAM           0

//FIXME
//Update as needed
#define TAS_RX_PORT             AFE_PORT_ID_QUATERNARY_MI2S_RX
#define TAS_TX_PORT             AFE_PORT_ID_QUATERNARY_MI2S_TX

#define CHANNEL0        1
#define CHANNEL1        2

#define TRUE            1
#define FALSE           0

#define TAS_SA_GET_F0          3810
#define TAS_SA_GET_Q           3811
#define TAS_SA_GET_TV          3812
#define TAS_SA_GET_RE          3813
#define TAS_SA_CALIB_INIT      3814
#define TAS_SA_CALIB_DEINIT    3815
#define TAS_SA_SET_RE          3816
#define TAS_SA_F0_TEST_INIT    3817
#define TAS_SA_F0_TEST_DEINIT  3818
#define TAS_SA_SET_PROFILE     3819
#define TAS_SA_GET_STATUS      3821
#define TAS_SA_SET_SPKID       3822
#define TAS_SA_SET_TCAL        3823

#define TAS_SA_IV_VBAT_FMT     3825
/*Added for DC Detection*/
#define CAPI_V2_TAS_SA_DC_DETECT	0x40404040

#define CALIB_START             1
#define CALIB_STOP              2
#define TEST_START              3
#define TEST_STOP               4

#define SLAVE1          0x98
#define SLAVE2          0x9A
#define SLAVE3          0x9C
#define SLAVE4          0x9E

#define CHANNEL0                           1
#define CHANNEL1                           2

#define TAS_SA_IS_SPL_IDX(X)           ((((X) >= 3810) && ((X) < 3899)) ? 1 : 0)
#define TAS_CALC_PARAM_IDX(I, LEN, CH)   ((I) | (LEN << 16) | (CH << 24))
#define AFE_SA_IS_SPL_IDX(X)           ((((X) >= 3810) && ((X) < 3899)) ? 1 : 0)
/* To Decode the Param ID */
#define TAS_PARAM_IDX_GET_ID(X)  (0xFFFF & (X))
#define TAS_PARAM_IDX_GET_LEN(X) ((0x0FFF0000 & (X)) >> 16)
#define TAS_PARAM_IDX_GET_CH(X)  ((0xF0000000 & (X)) >> 24)

typedef enum {
	IV_SENSE_FORMAT_NO_VBAT = 0,
	IV_SENSE_FORMAT_12_BIT_WITH_8BIT_VBAT = 1,
	IV_SENSE_FORMAT_8_BIT_WITH_8BIT_VBAT = 2,
} ti_smartamp_iv_vbat_format_t;

/*
 * List all the other profiles other than none and calibration.
 */
#define TAS_ALGO_PROFILE_LIST          "MUSIC", "VOICE", "VOIP", "RINGTONE"

struct afe_smartamp_get_set_params_t {
	uint32_t payload[TAS_PAYLOAD_SIZE];
} __packed;

#if APR_VERSION_V2
struct afe_smartamp_config_command {
	struct apr_hdr                      hdr;
	struct afe_port_cmd_set_param_v2    param;
	struct afe_port_param_data_v2       pdata;
	struct afe_smartamp_get_set_params_t  prot_config;
} __packed;

struct afe_smartamp_get_calib {
	struct apr_hdr hdr;
	struct afe_port_cmd_get_param_v2   get_param;
	struct afe_port_param_data_v2      pdata;
	struct afe_smartamp_get_set_params_t   res_cfg;
} __packed;

struct afe_smartamp_calib_get_resp {
	uint32_t status;
	struct afe_port_param_data_v2 pdata;
	struct afe_smartamp_get_set_params_t res_cfg;
} __packed;

#elif APR_VERSION_V3

//#define BUILD_STUB
/* only for build test */
#ifdef BUILD_STUB
#define INSTANCE_ID_0 0
#define AFE_PORT_CMDRSP_GET_PARAM_V3 0
struct param_hdr_v3 {
	int module_id;
	int instance_id;
	int param_id;
	int param_size;
};

struct mem_mapping_hdr {};

#endif

struct afe_smartamp_get_calib {
	struct apr_hdr hdr;
	struct mem_mapping_hdr mem_hdr;
	struct param_hdr_v3 pdata;
	struct afe_smartamp_get_set_params_t res_cfg;
} __packed;

struct afe_smartamp_calib_get_resp {
	uint32_t status;
	struct param_hdr_v3 pdata;
	struct afe_smartamp_get_set_params_t res_cfg;
} __packed;
#endif

typedef struct {
	int channel;
} dc_detection_data_t;

void register_tas256x_reset_func(void *fptr, dc_detection_data_t *data);
int afe_tas_smartamp_get_calib_data(uint32_t module_id, uint32_t param_id,
		int32_t length, uint8_t *data);
int afe_tas_smartamp_set_calib_data(uint32_t module_id, uint32_t param_id,
		int32_t length, uint8_t *data);

int tas25xx_smartamp_algo_ctrl(u8 *user_data,
		uint32_t param_id, uint8_t get_set, uint32_t length, uint32_t module_id);

void tas_smartamp_add_algo_controls (struct snd_soc_codec *codec);
//void tas_smartamp_add_algo_controls_for_platform (struct snd_soc_platform *platform);
void tas_smartamp_remove_algo_controls (struct snd_soc_codec *codec);
bool tas25xx_set_iv_bit_fomat(int iv_data_with, int vbat, int update_now);
#endif

