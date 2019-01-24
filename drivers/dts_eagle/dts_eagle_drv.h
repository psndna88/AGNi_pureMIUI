/* DTS_EAGLE START */
#ifndef DTS_EAGLE_DRV_H
#define DTS_EAGLE_DRV_H


#include <linux/ioctl.h>

#define EAGLE_DRIVER_ID 0xF2

#ifdef CONFIG_COMPAT
#include <linux/compat.h>
    #define DTS_EAGLE_COMPAT_IOCTL_GET_CACHE_SIZE           _IOR(EAGLE_DRIVER_ID, 0, __s32)
    #define DTS_EAGLE_COMPAT_IOCTL_SET_CACHE_SIZE           _IOW(EAGLE_DRIVER_ID, 1, __s32)
    #define DTS_EAGLE_COMPAT_IOCTL_GET_PARAM                _IOR(EAGLE_DRIVER_ID, 2, compat_uptr_t)
    #define DTS_EAGLE_COMPAT_IOCTL_SET_PARAM                _IOW(EAGLE_DRIVER_ID, 3, compat_uptr_t)
    #define DTS_EAGLE_COMPAT_IOCTL_SET_CACHE_BLOCK          _IOW(EAGLE_DRIVER_ID, 4, compat_uptr_t)
    #define DTS_EAGLE_COMPAT_IOCTL_SET_ACTIVE_DEVICE        _IOW(EAGLE_DRIVER_ID, 5, compat_uptr_t)
    #define DTS_EAGLE_COMPAT_IOCTL_GET_LICENSE              _IOR(EAGLE_DRIVER_ID, 6, compat_uptr_t)
    #define DTS_EAGLE_COMPAT_IOCTL_SET_LICENSE              _IOW(EAGLE_DRIVER_ID, 7, compat_uptr_t)
    #define DTS_EAGLE_COMPAT_IOCTL_SEND_LICENSE             _IOW(EAGLE_DRIVER_ID, 8, __s32)
    #define DTS_EAGLE_COMPAT_IOCTL_SET_VOLUME_COMMANDS      _IOW(EAGLE_DRIVER_ID, 9, compat_uptr_t)
    #define DTS_EAGLE_COMPAT_IOCTL_GET_CACHE_PREMIX         _IOR(EAGLE_DRIVER_ID, 10, compat_uptr_t)
    #define DTS_EAGLE_COMPAT_IOCTL_GET_CACHE_POSTMIX        _IOR(EAGLE_DRIVER_ID, 11, compat_uptr_t)
#endif

#define DTS_EAGLE_IOCTL_GET_CACHE_SIZE                      _IOR(EAGLE_DRIVER_ID, 0, int)
#define DTS_EAGLE_IOCTL_SET_CACHE_SIZE                      _IOW(EAGLE_DRIVER_ID, 1, int)
#define DTS_EAGLE_IOCTL_GET_PARAM                           _IOR(EAGLE_DRIVER_ID, 2, void*)
#define DTS_EAGLE_IOCTL_SET_PARAM                           _IOW(EAGLE_DRIVER_ID, 3, void*)
#define DTS_EAGLE_IOCTL_SET_CACHE_BLOCK                     _IOW(EAGLE_DRIVER_ID, 4, void*)
#define DTS_EAGLE_IOCTL_SET_ACTIVE_DEVICE                   _IOW(EAGLE_DRIVER_ID, 5, void*)
#define DTS_EAGLE_IOCTL_GET_LICENSE                         _IOR(EAGLE_DRIVER_ID, 6, void*)
#define DTS_EAGLE_IOCTL_SET_LICENSE                         _IOW(EAGLE_DRIVER_ID, 7, void*)
#define DTS_EAGLE_IOCTL_SEND_LICENSE                        _IOW(EAGLE_DRIVER_ID, 8, int)
#define DTS_EAGLE_IOCTL_SET_VOLUME_COMMANDS                 _IOW(EAGLE_DRIVER_ID, 9, void*)
#define DTS_EAGLE_IOCTL_GET_CACHE_PREMIX                    _IOR(EAGLE_DRIVER_ID, 10, void*)
#define DTS_EAGLE_IOCTL_GET_CACHE_POSTMIX                   _IOR(EAGLE_DRIVER_ID, 11, void*)


#define DTS_EAGLE_FLAG_IOCTL_PRE                            (1<<30)
#define DTS_EAGLE_FLAG_IOCTL_JUSTSETCACHE                   (1<<31)
#define DTS_EAGLE_FLAG_IOCTL_GETFROMCORE                    DTS_EAGLE_FLAG_IOCTL_JUSTSETCACHE
#define DTS_EAGLE_FLAG_IOCTL_MASK                           (~(DTS_EAGLE_FLAG_IOCTL_PRE | DTS_EAGLE_FLAG_IOCTL_JUSTSETCACHE))

struct dts_eagle_param_desc {
	uint32_t        id;
	uint32_t        size;
	int32_t         offset;
	uint32_t        device;
	uint32_t        rate;
} __packed;

struct dts_eagle_cache_block {
       uint32_t rate;
       void *data;
} __packed;

#endif
/* DTS_EAGLE END */
