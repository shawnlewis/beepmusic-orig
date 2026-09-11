#KERNELPATH ?= /lib/modules/`uname -r`/build

all:
	$(MAKE) -C $(KERNELPATH) M=$$PWD

clean:
	rm -rf .tmp_versions *.o *.ko *.mod.c modules.order Module.symvers .*.cmd
