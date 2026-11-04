# SKRoot 模块的编译参数，跟官方示例（module_web_ui_example/jni）保持一致。
# 只编 arm64：SKRoot Pro 目前只有 aarch64 环境。
COMMON_CPPFLAGS := \
    -std=c++20 \
    -fPIC \
    -fvisibility=hidden \
    -fno-stack-protector \
    -ffunction-sections \
    -fdata-sections \
    -flto=thin

COMMON_LDFLAGS := \
    -Wl,-O2 \
    -Wl,--gc-sections \
    -Wl,-s \
    -flto=thin

COMMON_INCLUDES := $(LOCAL_PATH)/..

LOCAL_CPPFLAGS += $(COMMON_CPPFLAGS)
LOCAL_LDFLAGS += $(COMMON_LDFLAGS)
LOCAL_C_INCLUDES += $(COMMON_INCLUDES)
