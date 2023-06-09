// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, Intel Corporation.
 *
 * Sohil Mehta <sohil.mehta@intel.com>
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <x86gprintrin.h>

#include "../uintr_common.h"

#define TIMER_DELAY	0x100000
#define TIMER_VECTOR	1

volatile int uintr_received;
int nerrs;

static void __attribute__((interrupt)) uintr_handler(struct __uintr_frame *ui_frame,
						     unsigned long long vector)
{
		uintr_received = 1;
}

static int setup_uintr_timer(void)
{
	if (uintr_register_handler(uintr_handler, 0)) {
		printf("[SKIP]\t%s:%d\n", __func__, __LINE__);
		return -EINVAL;
	}

	_stui();

	return 0;
}

void test_utimer_umsr(void)
{
	uintr_received = 0;
	int count = 0;
	unsigned long long tsc;

	printf("[RUN]\tUtimer-Umsr: Base test\n");

	tsc = _rdtsc();
	_uwrmsr(0x1b00, ((tsc + TIMER_DELAY) & ~0x1F) | TIMER_VECTOR);

	printf("[INFO]\tUtimer-Umsr: Timer deadline MSR:%llx TSC:%llx\n", _urdmsr(0x1b00), tsc);

	while(!uintr_received) {
		count++;
	}

	printf("[OK]\tUtimer-Umsr: Interrupt received after %d integer counts\n", count);
}

void test_utimer_clear_msr(void)
{
	printf("[RUN]\tUtimer-Umsr: MSR cleared\n");

	uintr_received = 0;
	_uwrmsr(0x1b00, ((_rdtsc() + TIMER_DELAY) & ~0x1F) | TIMER_VECTOR);

	while(!uintr_received);

	if (_urdmsr(0x1b00)!= 0)
		printf("[FAIL]\tUtimer-Umsr: MSR not cleared after timer delivery\n");
	else
		printf("[OK]\tUtimer-Umsr: MSR cleared after timer delivery\n");
}

void test_utimer_syscall_umsr(void)
{
	uintr_received = 0;
	int count = 0;

	printf("[RUN]\tUtimer-Umsr: syscall impact\n");

	uintr_set_timer(_rdtsc() + TIMER_DELAY, TIMER_VECTOR, 0);

	while(!uintr_received) {
		usleep(100);
		count++;
		printf("[INFO]\tUtimer-Umsr: Timer msr after syscall %llx\n", _urdmsr(0x1b00));
	}

	printf("[OK]\tUtimer-Umsr: Interrupt received after %d usleep(100) calls\n", count);
}

int main(void)
{
	if (!uintr_supported())
		return EXIT_SUCCESS;

	if (setup_uintr_timer())
		return 0;

	test_utimer_umsr();

	test_utimer_clear_msr();

	test_utimer_syscall_umsr();

	return nerrs ? EXIT_FAILURE : EXIT_SUCCESS;
}
