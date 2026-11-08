
GEN_CSRCS += $(notdir $(wildcard $(PRJ_DIR)/custom/*.c))
GEN_CSRCS += $(notdir $(wildcard $(PRJ_DIR)/custom/fonts/*.c))
GEN_CSRCS += $(notdir $(wildcard $(PRJ_DIR)/custom/images/*.c))

DEPPATH += --dep-path $(PRJ_DIR)/custom --dep-path $(PRJ_DIR)/custom/fonts --dep-path $(PRJ_DIR)/custom/images
VPATH += :$(PRJ_DIR)/custom:$(PRJ_DIR)/custom/fonts:$(PRJ_DIR)/custom/images

CFLAGS += "-I$(PRJ_DIR)/custom"
AFLAGS += "-I$(PRJ_DIR)/custom"
