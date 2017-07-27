#
# arch/arm64/boot/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2012, ARM Ltd.
# Author: Will Deacon <will.deacon@arm.com>
#
# Based on the ia64 boot/Makefile.
#

include $(srctree)/arch/arm64/boot/dts/qcom/Makefile

ifeq ($(CONFIG_KERNEL_GZIP),y)
COMP_TYPE=gz
endif
ifeq ($(CONFIG_KERNEL_LZ4),y)
COMP_TYPE=lz4
endif

targets := Image Image.$(COMP_TYPE)

DTB_NAMES := $(subst $\",,$(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE_NAMES))
ifneq ($(DTB_NAMES),)
DTB_LIST := $(addsuffix .dtb,$(DTB_NAMES))
else
DTB_LIST := $(dtb-y)
endif
DTB_OBJS := $(addprefix $(srctree)/arch/arm/boot/dts/,$(DTB_LIST))

$(obj)/Image: vmlinux FORCE
	$(call if_changed,objcopy)

ifeq ($(CONFIG_KERNEL_GZIP),y)
$(obj)/Image.$(COMP_TYPE): $(obj)/Image FORCE
	$(call if_changed,gzip)
endif
ifeq ($(CONFIG_KERNEL_LZ4),y)
$(obj)/Image.$(COMP_TYPE): $(obj)/Image FORCE
	$(call if_changed,lz4)
endif

$(obj)/Image.$(COMP_TYPE)-dtb: $(obj)/Image.$(COMP_TYPE) $(DTB_OBJS) FORCE
	$(call if_changed,cat)

install: $(obj)/Image
	$(CONFIG_SHELL) $(srctree)/$(src)/install.sh $(KERNELRELEASE) \
	$(obj)/Image System.map "$(INSTALL_PATH)"

zinstall: $(obj)/Image.$(COMP_TYPE)
	$(CONFIG_SHELL) $(srctree)/$(src)/install.sh $(KERNELRELEASE) \
	$(obj)/Image.$(COMP_TYPE) System.map "$(INSTALL_PATH)"

clean-files := Image Image-dtb Image.$(COMP_TYPE) Image.$(COMP_TYPE)-dtb
