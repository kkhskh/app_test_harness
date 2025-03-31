obj-m := app_test_harness.o

# Add dependency
KBUILD_EXTRA_SYMBOLS := $(PWD)/../recovery_evaluator/Module.symvers

KERNELDIR ?= /lib/modules/$(shell uname -r)/build

all:
	make -C $(KERNELDIR) M=$(PWD) modules

clean:
	make -C $(KERNELDIR) M=$(PWD) clean