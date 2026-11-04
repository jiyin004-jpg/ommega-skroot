LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := ommega_skroot

LOCAL_SRC_FILES := \
    ../ommega_skroot.cpp \
    ../util.cpp \
    ../webui.cpp

KERNEL_MODULE_KIT := $(LOCAL_PATH)/../../vendor/kernel_module_kit

LOCAL_C_INCLUDES += $(KERNEL_MODULE_KIT)/include
LOCAL_LDFLAGS += $(KERNEL_MODULE_KIT)/lib/libkernel_module_kit_static.a
LOCAL_LDLIBS += -lz -ldl

include $(LOCAL_PATH)/build_macros.mk

include $(BUILD_SHARED_LIBRARY)
