#ifndef __BOARD_ID_H__
#define __BOARD_ID_H__

extern void board_id_get_hwname(char *str);
extern bool board_get_33w_supported(void);
extern bool board_get_nfc_supported(void);
extern int board_id_get_hwlevel(void);
extern int board_id_get_hwversion_product_num(void);
extern int board_id_get_hwversion_major_num(void);
extern int board_id_get_hwversion_minor_num(void);
extern void charging_temps_thresholds(void);

#endif //__BOARD_ID_H__
