
KDIR := /lib/modules/$(shell uname -r)/build

SRC_DIR := $(PWD)/src
INC_DIR := $(PWD)/include
BUILD_DIR := $(PWD)/build

# 模块目标
obj-m += virtio_demo.o

all:
	@mkdir -p $(BUILD_DIR)
	$(MAKE) -C $(KDIR) M=$(BUILD_DIR) src=$(SRC_DIR) EXTRA_CFLAGS="-I$(INC_DIR)" modules

clean:
	@rm -rf $(BUILD_DIR)/*
	@rm -rf $(BUILD_DIR)/.*.cmd
	@rmdir $(BUILD_DIR) 2>/dev/null || true

install:
	sudo insmod $(BUILD_DIR)/virtio_demo.ko

uninstall:
	sudo rmmod virtio_demo || true
