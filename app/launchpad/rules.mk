LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += \
	$(LOCAL_DIR)/launchpad.c \


MODULE_DEPS := \
    lib/cksum \
    lib/elf \
    lib/gfx \
    lib/tga

include make/module.mk
