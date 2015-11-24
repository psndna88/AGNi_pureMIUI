#
# Makefile for the kernel mmc device drivers.
#
CFLAGS_KERNEL := -fno-unsafe-math-optimizations -fno-strict-aliasing

subdir-ccflags-$(CONFIG_MMC_DEBUG) := -DDEBUG

obj-$(CONFIG_MMC)		+= core/
obj-$(CONFIG_MMC)		+= card/
obj-$(subst m,y,$(CONFIG_MMC))	+= host/
