# ndk-build -C jni   (gera libs/<abi>/libretro.so). Fontes e defines vem do Makefile.common, o mesmo do Makefile principal.
LOCAL_PATH := $(call my-dir)
CORE_DIR   := $(LOCAL_PATH)/..

include $(CORE_DIR)/Makefile.common

# --- leitor de CHD (terceiros): biblioteca estatica a parte, sem -Wall e com os defines do CHD
CHD_ABS := $(foreach f,$(CHD_SRC),$(firstword $(wildcard $(foreach d,$(CHD_DIRS) .,$(CORE_DIR)/$(d)/$(f)))))
include $(CLEAR_VARS)
LOCAL_MODULE    := phoenix_chd
LOCAL_SRC_FILES := $(CHD_ABS:$(LOCAL_PATH)/%=%)
LOCAL_CFLAGS    := -O2 -std=gnu99 -w -fPIC -I$(CORE_DIR)/libretro-common/include $(CHD_DEFS)
include $(BUILD_STATIC_LIBRARY)

# --- nucleo
include $(CLEAR_VARS)
LOCAL_MODULE           := retro
LOCAL_SRC_FILES        := $(addprefix ../,libretro.c $(CORE))
LOCAL_CFLAGS           := -O2 -std=c11 -Wall -Wextra -fwrapv -fsigned-char -fvisibility=hidden -I$(CORE_DIR)/libretro-common/include
LOCAL_LDFLAGS          := -Wl,--version-script=$(CORE_DIR)/link.T
LOCAL_LDLIBS           := -lm
LOCAL_STATIC_LIBRARIES := phoenix_chd
include $(BUILD_SHARED_LIBRARY)
