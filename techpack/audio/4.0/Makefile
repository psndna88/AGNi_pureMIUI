ifeq ($(CONFIG_ARCH_ATOLL), y)
include $(srctree)/techpack/audio/4.0/config/atollauto.conf
export

# Use USERINCLUDE when you must reference the UAPI directories only.
USERINCLUDE     += \
                -I$(srctree)/techpack/audio/4.0/include/uapi \

# Use LINUXINCLUDE when you must reference the include/ directory.
# Needed to be compatible with the O= option
LINUXINCLUDE    += \
                -I$(srctree)/techpack/audio/4.0/include/uapi \
                -I$(srctree)/techpack/audio/4.0/include

LINUXINCLUDE    += \
                -include $(srctree)/techpack/audio/4.0/config/atollautoconf.h

obj-y += soc/
obj-y += dsp/
obj-y += ipc/
obj-y += asoc/
endif
