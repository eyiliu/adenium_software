ifneq ($(KERNELRELEASE),)
ccflags-y := -std=gnu99 -Wno-declaration-after-statement
ccflags-y += -I$(src)/include

obj-m := alteldma.o
alteldma-y := axidmachar.o
# CFLAGS_axidmachar.o := -I$(src)/include

else

KERNEL_SRC ?= /usr/src/kernels/$(shell uname -r)

all: modules

modules:
	make -C  $(KERNEL_SRC) M=$$PWD modules

modules_install:
	make -C  $(KERNEL_SRC) M=$$PWD modules_install

clean:
	make -C  $(KERNEL_SRC) M=$$PWD clean

endif
