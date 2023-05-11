// SPDX-License-Identifier: GPL-2.0-only
/*
 * FRED nested exception tests
 *
 * Copyright (C) 2023, Intel, Inc.
 */
#define _GNU_SOURCE /* for program_invocation_short_name */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <asm/msr-index.h>

#include "kvm_util.h"
#include "test_util.h"
#include "guest_modes.h"
#include "processor.h"

#define FRED_STKLVL(v,l)		(_AT(unsigned long, l) << (2 * (v)))
#define FRED_CONFIG_ENTRYPOINT(p)	_AT(unsigned long, (p))

#define FRED_VALID_RSP			0x8000

static unsigned long fred_invalid_rsp[4] = {
	0x0, 0xf0000000, 0xe0000000, 0xd0000000,
};

extern char asm_user_wrmsr[];
extern char asm_user_ud[];
extern char asm_done_fault[];

extern void asm_test_fault(int test);

/*
 * user level code for triggering faults.
 */
asm(".pushsection .text\n"
    ".type asm_user_wrmsr, @function\n"
    ".align 4096\n"
    "asm_user_wrmsr:\n"
    /* Trigger a #GP */
    "wrmsr\n"

    ".fill asm_user_ud - ., 1, 0xcc\n"

    ".type asm_user_ud, @function\n"
    ".org asm_user_wrmsr + 16\n"
    "asm_user_ud:\n"
    /* Trigger a #UD */
    "ud2\n"

    ".align 4096, 0xcc\n"
    ".popsection");

/* Send current stack level and #PF address */
#define GUEST_SYNC_CSL_FA(__stage, __pf_address)		\
	GUEST_SYNC_ARGS(__stage, __pf_address, 0, 0, 0)

void fred_entry_from_user(struct fred_stack *stack)
{
	u32 current_stack_level = rdmsr(MSR_IA32_FRED_CONFIG) & 0x3;

	GUEST_SYNC_CSL_FA(current_stack_level, stack->event_data);

	/* Do NOT go back to user level, continue the next test instead */
	stack->ssx = 0x18;
	stack->csx = 0x10;
	stack->ip = (u64)&asm_done_fault;
}

void fred_entry_from_kernel(struct fred_stack *stack)
{
	TEST_FAIL("kernel events not allowed in FRED tests.");
}

/*
 * FRED entry points.
 */
asm(".pushsection .text\n"
    ".type asm_fred_entrypoint_user, @function\n"
    ".align 4096\n"
    "asm_fred_entrypoint_user:\n"
    "endbr64\n"
    "push %rdi\n"
    "push %rsi\n"
    "push %rdx\n"
    "push %rcx\n"
    "push %rax\n"
    "push %r8\n"
    "push %r9\n"
    "push %r10\n"
    "push %r11\n"
    "push %rbx\n"
    "push %rbp\n"
    "push %r12\n"
    "push %r13\n"
    "push %r14\n"
    "push %r15\n"
    "movq %rsp, %rdi\n"
    "call fred_entry_from_user\n"
    "pop %r15\n"
    "pop %r14\n"
    "pop %r13\n"
    "pop %r12\n"
    "pop %rbp\n"
    "pop %rbx\n"
    "pop %r11\n"
    "pop %r10\n"
    "pop %r9\n"
    "pop %r8\n"
    "pop %rax\n"
    "pop %rcx\n"
    "pop %rdx\n"
    "pop %rsi\n"
    "pop %rdi\n"
    /* Do NOT go back to user level, continue the next test instead */
    ".byte 0xf2,0x0f,0x01,0xca\n"	/* ERETS */

    ".fill asm_fred_entrypoint_kernel - ., 1, 0xcc\n"

    ".type asm_fred_entrypoint_kernel, @function\n"
    ".org asm_fred_entrypoint_user + 256\n"
    "asm_fred_entrypoint_kernel:\n"
    "endbr64\n"
    "push %rdi\n"
    "push %rsi\n"
    "push %rdx\n"
    "push %rcx\n"
    "push %rax\n"
    "push %r8\n"
    "push %r9\n"
    "push %r10\n"
    "push %r11\n"
    "push %rbx\n"
    "push %rbp\n"
    "push %r12\n"
    "push %r13\n"
    "push %r14\n"
    "push %r15\n"
    "movq %rsp, %rdi\n"
    "call fred_entry_from_kernel\n"
    "pop %r15\n"
    "pop %r14\n"
    "pop %r13\n"
    "pop %r12\n"
    "pop %rbp\n"
    "pop %rbx\n"
    "pop %r11\n"
    "pop %r10\n"
    "pop %r9\n"
    "pop %r8\n"
    "pop %rax\n"
    "pop %rcx\n"
    "pop %rdx\n"
    "pop %rsi\n"
    "pop %rdi\n"
    ".byte 0xf2,0x0f,0x01,0xca\n"	/* ERETS */
    ".align 4096, 0xcc\n"
    ".popsection");

extern char asm_fred_entrypoint_user[];

/*
 * Prepare a FRED stack frame for ERETU, and ERETU to the next instruction
 * WRMSR, which causes #GP. However because the FRED RSP0 is not yet mapped
 * in the page table, the delivery of the #GP causes a #PF on the FRED RSP0,
 * which is a nested #PF, and will be then delivered on the FRED RSP3.
 *
 * If the FRED RSP3 is not yet mapped, the CPU will generate a triple fault.
 */
asm(".pushsection .text\n"
    ".type asm_test_fault, @function\n"
    ".align 4096\n"
    "asm_test_fault:\n"
    "endbr64\n"
    "push %rbp\n"
    "mov %rsp, %rbp\n"
    "and $(~0x3f), %rsp\n"
    "push $0\n"
    "push $0\n"
    "mov $0x2b, %rax\n"
    "bts $57, %rax\n"
    "push %rax\n"
    /* The FRED user level test code does NOT need a stack. */
    "push $0\n"
    "pushf\n"
    "mov $0x33, %rax\n"
    "push %rax\n"
    "cmp $0, %edi\n"
    "jne 1f\n"
    "lea asm_user_wrmsr(%rip), %rax\n"
    "jmp 2f\n"
    "1: lea asm_user_ud(%rip), %rax\n"
    "2: push %rax\n"
    "push $0\n"
    /* ERETU to user level code to generate a fault immediately */
    ".byte 0xf3,0x0f,0x01,0xca\n"
    "asm_done_fault:\n"
    "mov %rbp, %rsp\n"
    "pop %rbp\n"
    "ret\n"
    ".align 4096, 0xcc\n"
    ".popsection");

static void guest_code(void)
{
	wrmsr(MSR_IA32_FRED_CONFIG,
	      FRED_CONFIG_ENTRYPOINT(asm_fred_entrypoint_user));

	wrmsr(MSR_IA32_FRED_RSP1, FRED_VALID_RSP);
	wrmsr(MSR_IA32_FRED_RSP2, FRED_VALID_RSP);
	wrmsr(MSR_IA32_FRED_RSP3, FRED_VALID_RSP);

	/* Enable FRED */
	set_cr4(get_cr4() | X86_CR4_FRED);

	/* 0: wrmsr to generate #GP */
	wrmsr(MSR_IA32_FRED_STKLVLS,
	      FRED_STKLVL(PF_VECTOR, 1));
	wrmsr(MSR_IA32_FRED_RSP0, fred_invalid_rsp[1]);
	asm_test_fault(0);

	/* 1: ud2 to generate #UD */
	wrmsr(MSR_IA32_FRED_STKLVLS,
	      FRED_STKLVL(PF_VECTOR, 2));
	wrmsr(MSR_IA32_FRED_RSP0, fred_invalid_rsp[2]);
	asm_test_fault(1);

	/* 0: wrmsr to generate #GP */
	wrmsr(MSR_IA32_FRED_STKLVLS,
	      FRED_STKLVL(PF_VECTOR, 3));
	wrmsr(MSR_IA32_FRED_RSP0, fred_invalid_rsp[3]);
	asm_test_fault(0);

	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;
	struct kvm_cpuid2 *kvm_cpuid;
	uint64_t expected_current_stack_level = 1;

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_FRED));

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	kvm_cpuid = allocate_kvm_cpuid2(vcpu->cpuid->nent + 1);
	memcpy(kvm_cpuid, vcpu->cpuid, kvm_cpuid2_size(vcpu->cpuid->nent));
	kvm_cpuid->entries[kvm_cpuid->nent - 1].function = 7;
	kvm_cpuid->entries[kvm_cpuid->nent - 1].index = 1;
	kvm_cpuid->entries[kvm_cpuid->nent - 1].eax |=
		1 << X86_FEATURE_FRED.bit | 1 << X86_FEATURE_LKGS.bit;
	vcpu->cpuid = kvm_cpuid;
	__vcpu_set_cpuid(vcpu);

	while (true) {
		uint64_t r;

		vcpu_run(vcpu);

		r = get_ucall(vcpu, &uc);

		if (r == UCALL_DONE)
			break;

		if (r == UCALL_SYNC) {
			TEST_ASSERT((uc.args[1] == expected_current_stack_level) &&
				    (uc.args[2] == fred_invalid_rsp[expected_current_stack_level] - 1),
				    "Incorrect stack level %lx and #PF address %lx\n",
				    uc.args[1], uc.args[2]);
			expected_current_stack_level++;
		}
	}

	kvm_vm_free(vm);
	return 0;
}
