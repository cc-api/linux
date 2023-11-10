#include <stdio.h>
#include <stdint.h>
#include <err.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <signal.h>
#include <setjmp.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <string.h>
#include "../kselftest.h"

struct umsr_req {
	unsigned int msr;
	unsigned int allow;
	unsigned long long addr;
};

#define UMSR_ALLOW_ENABLE	0x1
#define UMSR_ALLOW_READ		0x2
#define UMSR_ALLOW_WRITE	0x4

#define MSR_TSC 0x10
#define MSR_APIC_BASE 0x1b
#define MSR_UTIMER 0x1b00

/* Set timer delay to be a big value to avoid timeout */
#define TIMER_DELAY 0xf0000000
#define TIMER_VECTOR 0x1

static inline int umsr_supported(void)
{
	unsigned int eax, ebx, ecx, edx;

	__cpuid_count(0x7, 0x1, eax, ebx, ecx, edx);

	return (edx & (1 << 15));
}

static inline int utimer_supported(void)
{
	unsigned int eax, ebx, ecx, edx;

	__cpuid_count(0x7, 0x1, eax, ebx, ecx, edx);

	return (edx & (1 << 13));
}

static jmp_buf jmpbuf;
static void sethandler(int sig, void (*handler)(int, siginfo_t *, void *),
		       int flags)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = handler;
	sa.sa_flags = SA_SIGINFO | flags;
	sigemptyset(&sa.sa_mask);
	if (sigaction(sig, &sa, 0))
		err(1, "sigaction");
}

static void clearhandler(int sig)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	if (sigaction(sig, &sa, 0))
		err(1, "sigaction");
}

static void sighandler(int sig, siginfo_t *si, void *ctx_void)
{
	siglongjmp(jmpbuf, 1);
}

static bool try_rdmsr(unsigned long msr_index, unsigned long long *value)
{
	sethandler(SIGSEGV, sighandler, SA_RESETHAND);
	if (sigsetjmp(jmpbuf, 1) != 0) {
		return false;
	} else {
		unsigned long long msrv;
		asm volatile(
			" mov %1, %%rax\n\t"
			".byte 0xF2, 0x48, 0x0f, 0x38, 0xF8, 0xc0\n\t"
			" mov %%rax, %0\n\t"
			: "=r" (msrv)
			: "r" (msr_index)
			:"rax"
			);
		if (value)
			*value = msrv;
		return true;
	}
	clearhandler(SIGSEGV);
}

static bool try_wrmsr(unsigned long msr_index, unsigned long long msrv)
{
	sethandler(SIGSEGV, sighandler, SA_RESETHAND);
	if (sigsetjmp(jmpbuf, 1) != 0) {
		return false;
	} else {
		asm volatile(
			" mov %0, %%rax\n\t"
			" mov %1, %%rdx\n\t"
			/* uwrmsr %rdx, %rax, rax: msr address, rdx: value */
			".byte 0xf3, 0x48, 0x0f, 0x38, 0xf8, 0xc2\n\t"
			:
			: "r" (msr_index), "r"(msrv)
			:"rax", "rdx"
			);
		return true;
	}
	clearhandler(SIGSEGV);
}

/* value is not used when !read */
static void expect_ok(int msr_index, bool read, unsigned long long value)
{
	bool ret;

	if (read)
		ret = try_rdmsr(msr_index, NULL);
	else
		ret = try_wrmsr(msr_index, value);
	if (!ret) {
		printf("[FAIL]\t %s to 0x%x failed\n", read?"urdmsr":"uwrmsr", msr_index);
		exit(1);
	}

	printf("[OK]\t %s to 0x%x worked\n", read?"urdmsr":"uwrmsr", msr_index);
}

static void expect_rdok(int msr_index)
{
	expect_ok(msr_index, true, 0);
}

static void expect_wrok(int msr_index, unsigned long long value)
{
	expect_ok(msr_index, false, value);
}

static void expect_gp(int msr_index, bool read, unsigned long long value)
{
	bool ret;
	if (read)
		ret = try_rdmsr(msr_index, NULL);
	else
		ret = try_wrmsr(msr_index, value);
	if (ret) {
		printf("[FAIL]\t%s to 0x%x worked\n", read?"urdmsr":"uwrmsr", msr_index);
		exit(1);
	}

	printf("[OK]\t%s to 0x%0x failed\n", read?"urdmsr":"uwrmsr", msr_index);
}

static void expect_rdgp(int msr_index)
{
	expect_gp(msr_index, true, 0);
}

static void expect_wrgp(int msr_index, unsigned long long value)
{
	expect_gp(msr_index, false, value);
}

int main(void)
{
	int fd;
	struct umsr_req r;
	int ret;

	if (!umsr_supported()) {
		ksft_print_msg("System does not support user msr\n");
		/* 4 is SKIP in kselftest */
		return 4;
	}

	fd = open("/dev/umsr", O_RDWR);

	if (fd < 0) {
		printf("can't open the umsr");
		return  -1;
	}

	r.allow = UMSR_ALLOW_ENABLE | UMSR_ALLOW_READ;
	r.msr = MSR_TSC;
	ret = write(fd, &r, sizeof(struct umsr_req));
	if (ret < 0) {
		printf("can't write the umsr device");
		close(fd);
		return  -1;
	}

	expect_rdok(MSR_TSC);
	expect_rdgp(MSR_APIC_BASE);

	r.allow = UMSR_ALLOW_ENABLE | UMSR_ALLOW_READ;
	r.msr = MSR_APIC_BASE;
	ret = write(fd, &r, sizeof(struct umsr_req));
	if (ret < 0) {
		printf("can't write the umsr device");
		close(fd);
		return  -1;
	}
	expect_rdok(MSR_APIC_BASE);

	/* Not find right MSR to test uwrmsr, use User Timer MSR */
	if (utimer_supported()) {
		unsigned long long tsc = 0, utimer = 0;

		try_rdmsr(MSR_TSC, &tsc);
		expect_wrgp(MSR_UTIMER, ((tsc + TIMER_DELAY) & ~0x1F) | TIMER_VECTOR);

		r.allow = UMSR_ALLOW_ENABLE | UMSR_ALLOW_READ | UMSR_ALLOW_WRITE;
		r.msr = MSR_UTIMER;
		ret = write(fd, &r, sizeof(struct umsr_req));
		if (ret < 0) {
			printf("can't write the umsr device");
			close(fd);
			return  -1;
		}

		expect_rdok(MSR_UTIMER);

		try_rdmsr(MSR_UTIMER, &utimer);
		ksft_print_msg("before write, utimer is %llx\n", utimer);

		/* rdmsr here is safe as tested above already */
		try_rdmsr(MSR_TSC, &tsc);
		utimer = ((tsc + TIMER_DELAY) & ~0x1F) | TIMER_VECTOR;
		ksft_print_msg("write utimer 0x%llx to hardware\n", utimer);
		expect_wrok(MSR_UTIMER, utimer);

		try_rdmsr(MSR_UTIMER, &utimer);
		ksft_print_msg("after write, utimer is %llx\n", utimer);
	}

	return 0;
}
