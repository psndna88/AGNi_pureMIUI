OBJS=sigma_dut.c
OBJS += utils.c
OBJS += wpa_ctrl.c
OBJS += wpa_helpers.c

OBJS += cmds_reg.c
OBJS += basic.c
OBJS += sta.c
OBJS += traffic.c
OBJS += p2p.c
OBJS += dev.c
OBJS += ap.c
OBJS += powerswitch.c
OBJS += atheros.c

# Initialize CFLAGS to limit to local module
CFLAGS =
ifndef NO_TRAFFIC_AGENT
CFLAGS += -DCONFIG_TRAFFIC_AGENT -DCONFIG_WFA_WMM_AC
OBJS += traffic_agent.c
OBJS += uapsd_stream.c
endif

ifndef NO_WLANTEST
CFLAGS += -DCONFIG_WLANTEST
OBJS += wlantest.c
endif

CFLAGS += -DCONFIG_CTRL_IFACE_CLIENT_DIR=\"/data/misc/wifi/sockets\"
CFLAGS += -DSIGMA_TMPDIR=\"/data\"

LOCAL_PATH := $(call my-dir)
FRAMEWORK_GIT_VER := $(shell cd $(ANDROID_BUILD_TOP/)frameworks/base && git describe)
SIGMA_GIT_VER := $(shell cd $(LOCAL_PATH) && git describe --dirty=+)
ifeq ($(SIGMA_GIT_VER),)
ifeq ($(FRAMEWORK_GIT_VER),)
SIGMA_VER = android-$(PLATFORM_VERSION)-$(TARGET_PRODUCT)-$(BUILD_ID)
else
SIGMA_VER = framework-$(FRAMEWORK_VER)
endif
else
ifeq ($(FRAMEWORK_GIT_VER),)
SIGMA_VER = android-$(PLATFORM_VERSION)-$(TARGET_PRODUCT)-$(BUILD_ID)-sigma-$(SIGMA_GIT_VER)
else
SIGMA_VER = framework-$(FRAMEWORK_GIT_VER)-sigma-$(SIGMA_GIT_VER)
endif
endif
CFLAGS += -DSIGMA_DUT_VER=\"$(SIGMA_VER)\"

include $(CLEAR_VARS)
LOCAL_MODULE := sigma_dut
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES += \
	$(LOCAL_PATH) frameworks/base/cmds/keystore system/security/keystore \
	$(LOCAL_PATH) hardware/qcom/wlan/qcwcn/wifi_hal \
	$(LOCAL_PATH) hardware/libhardware_legacy/include/hardware_legacy
LOCAL_SHARED_LIBRARIES := libc libcutils
LOCAL_SHARED_LIBRARIES += libhardware_legacy
ifdef SIGMA_DUT_NAN
ifneq ($(wildcard hardware/qcom/wlan/qcwcn/wifi_hal/nan.h),)
LOCAL_SHARED_LIBRARIES := libwifi-hal-qcom
OBJS += nan.c
CFLAGS += -DANDROID_NAN
endif
ifneq ($(wildcard external/libnl),)
LOCAL_SHARED_LIBRARIES += libnl
else
LOCAL_STATIC_LIBRARIES += libnl_2
endif
endif
ver = $(filter 4.3%,$(PLATFORM_VERSION))
ver += $(filter 4.4%,$(PLATFORM_VERSION))
ver += $(filter 5.0%,$(PLATFORM_VERSION))
ver += $(filter 5.1%,$(PLATFORM_VERSION))
ver += $(filter L%,$(PLATFORM_VERSION))
ver += $(filter M%,$(PLATFORM_VERSION))
ver += $(filter 6.0%,$(PLATFORM_VERSION))
ver += $(filter N%,$(PLATFORM_VERSION))
ver += $(filter 7.0%,$(PLATFORM_VERSION))
ifneq (,$(strip $(ver)))
CFLAGS += -DANDROID43
CFLAGS += -Wno-unused-parameter
LOCAL_C_INCLUDES += system/security/keystore/include/keystore
LOCAL_SHARED_LIBRARIES += liblog
LOCAL_SHARED_LIBRARIES += libkeystore_binder
endif
LOCAL_SRC_FILES := $(OBJS)
LOCAL_CFLAGS := $(CFLAGS)
include $(BUILD_EXECUTABLE)

# Add building of e_loop
include $(CLEAR_VARS)
LOCAL_SRC_FILES:= e_loop.c
LOCAL_MODULE := e_loop
LOCAL_CFLAGS := -DWITHOUT_IFADDRS -Wno-sign-compare
include $(BUILD_EXECUTABLE)
