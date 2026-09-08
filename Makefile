obj-m += robot_arm_step.o

ccflags-y := -I$(src)/../../include/uapi

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
