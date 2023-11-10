# SPDX-License-Identifier: GPL-2.0

CAN_BUILD_UINTR := $(shell ./../../x86/check_cc.sh $(CC) ../../x86/trivial_64bit_program.c -muintr)

CAN_BUILD_UMSR := $(shell ./../../x86/check_cc.sh $(CC) ../../x86/trivial_64bit_program.c -muintr -mumsr)

# Use O2 as temporary hack to avoid internal GCC issue wrt User MSR

ifeq ($(CAN_BUILD_UINTR),1)
CFLAGS += -g -muintr -mgeneral-regs-only -O2
LDLIBS += -lpthread

TEST_GEN_PROGS = syscall_utimer

ifeq ($(CAN_BUILD_UMSR),1)
TEST_GEN_PROGS += umsr_utimer
CFLAGS += -mumsr
endif

endif

include ../../lib.mk
