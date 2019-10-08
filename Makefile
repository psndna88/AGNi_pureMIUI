# SPDX-License-Identifier: GPL-2.0-only

# auto-detect subdirs
ifeq ($(CONFIG_ARCH_KONA), y)
include $(srctree)/techpack/video/config/konavid.conf
endif

ifeq ($(CONFIG_ARCH_KONA), y)
LINUXINCLUDE    += -include $(srctree)/techpack/video/config/konavidconf.h
endif

# auto-detect subdirs
ifeq ($(CONFIG_ARCH_LITO), y)
include $(srctree)/techpack/video/config/litovid.conf
endif

ifeq ($(CONFIG_ARCH_LITO), y)
LINUXINCLUDE    += -include $(srctree)/techpack/video/config/litovidconf.h
endif

LINUXINCLUDE    += -I$(srctree)/techpack/video/include \
                   -I$(srctree)/techpack/video/include/uapi

USERINCLUDE     += -I$(srctree)/techpack/video/include/uapi

obj-y +=msm/
