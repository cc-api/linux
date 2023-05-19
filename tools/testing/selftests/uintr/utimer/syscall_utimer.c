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

static inline void cpu_delay_long(void)
{
	long long dl = 1000;
	volatile long long cnt = dl << 10;

	while (cnt--)
		dl++;
}

static inline void cpu_delay_short(void)
{
	long long dl = 1000;
	volatile long long cnt = 10;

	while (cnt--)
		dl++;
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

void test_utimer(void)
{
	uintr_received = 0;
	int count = 0;

	printf("[RUN]\tUtimer: Base test\n");

	uintr_set_timer(_rdtsc() + TIMER_DELAY, TIMER_VECTOR, 0);

	while(!uintr_received) {
		count++;
	}

	printf("[OK]\tUtimer: Interrupt received after %d integer counts\n", count);
}

void test_utimer_past(void)
{
	uintr_received = 0;
	int count = 0;

	printf("[RUN]\tUtimer: Deadline in the past\n");

	/* Note the negative below */
	uintr_set_timer(_rdtsc() - 1, TIMER_VECTOR, 0);

	while(!uintr_received) {
		count++;
	}

	printf("[OK]\tUtimer: Interrupt received after %d integer counts\n", count);
}

void test_utimer_short_delay(void)
{
	uintr_received = 0;
	int count = 0;

	printf("[RUN]\tUtimer: Short delay\n");

	uintr_set_timer(_rdtsc() + 0x20, TIMER_VECTOR, 0);

	while(!uintr_received) {
		cpu_delay_short();
		//printf("[INFO]\tUtimer: Interrupt not yet received\n");
		count++;
	}

	printf("[OK]\tUtimer: Interrupt received after %d short delays\n", count);
}

void test_utimer_long_delay(void)
{
	uintr_received = 0;

	printf("[RUN]\tUtimer: Long delay\n");

	uintr_set_timer(_rdtsc() + TIMER_DELAY, TIMER_VECTOR, 0);

	cpu_delay_long();

	if (uintr_received) {
		printf("[OK]\tUtimer: Interrupt received\n");
	} else {
		printf("[FAIL]\tUtimer: Interrupt not received\n");
		nerrs++;
	}

	while(!uintr_received);
}

void test_utimer_zero_value(void)
{
	uintr_received = 0;

	printf("[RUN]\tUtimer: Zero value\n");

	uintr_set_timer(0, TIMER_VECTOR, 0);

	cpu_delay_long();

	if (uintr_received) {
		printf("[FAIL]\tUtimer: Interrupt received but was not expected\n");
		nerrs++;
	} else {
		printf("[OK]\tUtimer: Interrupt not received\n");
	}
}

void test_utimer_syscall_delay(void)
{
	uintr_received = 0;
	int count = 0;

	printf("[RUN]\tUtimer: syscall delay\n");

	uintr_set_timer(_rdtsc() + TIMER_DELAY, TIMER_VECTOR, 0);

	while(!uintr_received) {
		usleep(1);
		count++;
	}

	printf("[OK]\tUtimer: Interrupt received after %d usleep(1) calls\n", count);
}

void test_utimer_twice(void)
{
	int count_a = 0, count_b = 0;

	printf("[RUN]\tUtimer: Twice\n");

	uintr_received = 0;
	uintr_set_timer(_rdtsc() + TIMER_DELAY, TIMER_VECTOR, 0);
	while(!uintr_received) {
		count_a++;
	}

	uintr_received = 0;
	uintr_set_timer(_rdtsc() + TIMER_DELAY, TIMER_VECTOR, 0);
	while(!uintr_received) {
		count_b++;
	}

	printf("[OK]\tUtimer: Interrupt received twice count_a %d count_b %d\n",
	       count_a, count_b);
}


int main(void)
{
	if (!uintr_supported())
		return EXIT_SUCCESS;

	if (setup_uintr_timer())
		return 0;

	test_utimer();

	test_utimer_past();

	test_utimer_short_delay();

	test_utimer_long_delay();

	test_utimer_zero_value();

	test_utimer_syscall_delay();

	test_utimer_twice();

	//test_utimer_custom_vector();

	return nerrs ? EXIT_FAILURE : EXIT_SUCCESS;
}
