/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_REGS_H
#define __PERF_REGS_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/bitmap.h>
#include <linux/perf_event.h>
#include "util/record.h"

struct regs_dump;

struct sample_reg {
	const char *name;
	union {
		uint64_t mask;
		DECLARE_BITMAP(mask_ext, PERF_NUM_INTR_REGS * 64);
	};
};

#define SMPL_REG_MASK(b) (1ULL << (b))
#define SMPL_REG(n, b) { .name = #n, .mask = SMPL_REG_MASK(b) }
#define SMPL_REG2_MASK(b) (3ULL << (b))
#define SMPL_REG2(n, b) { .name = #n, .mask = SMPL_REG2_MASK(b) }
#define SMPL_REG_EXT(n, b) { .name = #n, .mask_ext[b / __BITS_PER_LONG] = 0x1ULL << (b % __BITS_PER_LONG) }
#define SMPL_REG2_EXT(n, b) { .name = #n, .mask_ext[b / __BITS_PER_LONG] = 0x3ULL << (b % __BITS_PER_LONG) }
#define SMPL_REG4_EXT(n, b) { .name = #n, .mask_ext[b / __BITS_PER_LONG] = 0xfULL << (b % __BITS_PER_LONG) }
#define SMPL_REG8_EXT(n, b) { .name = #n, .mask_ext[b / __BITS_PER_LONG] = 0xffULL << (b % __BITS_PER_LONG) }
#define SMPL_REG_END { .name = NULL }

enum {
	SDT_ARG_VALID = 0,
	SDT_ARG_SKIP,
};

int arch_sdt_arg_parse_op(char *old_op, char **new_op);
void arch__intr_reg_mask(unsigned long *mask);
uint64_t arch__user_reg_mask(void);

#ifdef HAVE_PERF_REGS_SUPPORT
extern const struct sample_reg sample_reg_masks[];

const char *perf_reg_name(int id, const char *arch);
int perf_reg_value(u64 *valp, struct regs_dump *regs, int id);
uint64_t perf_arch_reg_ip(const char *arch);
uint64_t perf_arch_reg_sp(const char *arch);
const char *__perf_reg_name_arm64(int id);
uint64_t __perf_reg_ip_arm64(void);
uint64_t __perf_reg_sp_arm64(void);
const char *__perf_reg_name_arm(int id);
uint64_t __perf_reg_ip_arm(void);
uint64_t __perf_reg_sp_arm(void);
const char *__perf_reg_name_csky(int id);
uint64_t __perf_reg_ip_csky(void);
uint64_t __perf_reg_sp_csky(void);
const char *__perf_reg_name_loongarch(int id);
uint64_t __perf_reg_ip_loongarch(void);
uint64_t __perf_reg_sp_loongarch(void);
const char *__perf_reg_name_mips(int id);
uint64_t __perf_reg_ip_mips(void);
uint64_t __perf_reg_sp_mips(void);
const char *__perf_reg_name_powerpc(int id);
uint64_t __perf_reg_ip_powerpc(void);
uint64_t __perf_reg_sp_powerpc(void);
const char *__perf_reg_name_riscv(int id);
uint64_t __perf_reg_ip_riscv(void);
uint64_t __perf_reg_sp_riscv(void);
const char *__perf_reg_name_s390(int id);
uint64_t __perf_reg_ip_s390(void);
uint64_t __perf_reg_sp_s390(void);
const char *__perf_reg_name_x86(int id);
uint64_t __perf_reg_ip_x86(void);
uint64_t __perf_reg_sp_x86(void);

static inline uint64_t DWARF_MINIMAL_REGS(const char *arch)
{
	return (1ULL << perf_arch_reg_ip(arch)) | (1ULL << perf_arch_reg_sp(arch));
}

#else

static inline uint64_t DWARF_MINIMAL_REGS(const char *arch __maybe_unused)
{
	return 0;
}

static inline const char *perf_reg_name(int id __maybe_unused, const char *arch __maybe_unused)
{
	return "unknown";
}

static inline int perf_reg_value(u64 *valp __maybe_unused,
				 struct regs_dump *regs __maybe_unused,
				 int id __maybe_unused)
{
	return 0;
}

static inline uint64_t perf_arch_reg_ip(const char *arch __maybe_unused)
{
	return 0;
}

static inline uint64_t perf_arch_reg_sp(const char *arch __maybe_unused)
{
	return 0;
}

#endif /* HAVE_PERF_REGS_SUPPORT */
#endif /* __PERF_REGS_H */
