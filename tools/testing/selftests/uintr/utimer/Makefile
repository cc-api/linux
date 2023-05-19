# SPDX-License-Identifier: GPL-2.0

CAN_BUILD_UINTR := $(shell ./../../x86/check_cc.sh $(CC) ../../x86/trivial_64bit_program.c -muintr)

ifeq ($(CAN_BUILD_UINTR),1)
CFLAGS += -g -muintr -mgeneral-regs-only -O2
LDLIBS += -lpthread

TEST_GEN_PROGS = syscall_utimer

endif

include ../../lib.mk
