ccflags-y += -I$(src)			# needed for trace events

ifeq ($(CONFIG_MACH_XIAOMI_KENZO_AGNI_LOS_O),y)
obj-$(CONFIG_ANDROID_BINDER_IPC)	+= binder.o
else
obj-$(CONFIG_ANDROID_BINDER_IPC)	+= binder_n.o
endif
