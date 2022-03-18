// SPDX-License-Identifier: GPL-2.0
/*
 * Resctrl tests
 *
 * Copyright (C) 2018 Intel Corporation
 *
 * Authors:
 *    Sai Praneeth Prakhya <sai.praneeth.prakhya@intel.com>,
 *    Fenghua Yu <fenghua.yu@intel.com>
 */
#include "resctrl.h"

#define BENCHMARK_ARGS		64
#define BENCHMARK_ARG_SIZE	64

bool is_amd;

void detect_amd(void)
{
	FILE *inf = fopen("/proc/cpuinfo", "r");
	char *res;

	if (!inf)
		return;

	res = fgrep(inf, "vendor_id");

	if (res) {
		char *s = strchr(res, ':');

		is_amd = s && !strcmp(s, ": AuthenticAMD\n");
		free(res);
	}
	fclose(inf);
}

static void cmd_help(void)
{
	printf("usage: resctrl_tests [-h] [-b \"benchmark_cmd [options]\"] [-t test list] [-n no_of_bits]\n");
	printf("\t-b benchmark_cmd [options]: run specified benchmark for MBM, MBA and CMT\n");
	printf("\t   default benchmark is builtin fill_buf\n");
	printf("\t-t test list: run tests specified in the test list, ");
	printf("e.g. -t mbm,mba,cmt,cat\n");
	printf("\t-n no_of_bits: run cache tests using specified no of bits in cache bit mask\n");
	printf("\t-p cpu_no: specify CPU number to run the test. 1 is default\n");
	printf("\t-h: help\n");
}

void tests_cleanup(void)
{
	mbm_test_cleanup();
	mba_test_cleanup();
	cmt_test_cleanup();
	cat_test_cleanup();
}

static void run_mbm_test(bool has_ben, char **benchmark_cmd, int span,
			 int cpu_no, char *bw_report)
{
	int res;

	ksft_print_msg("Starting MBM BW change ...\n");

	if (!validate_resctrl_feature_request(MBM_STR)) {
		ksft_test_result_skip("Hardware does not support MBM or MBM is disabled\n");
		return;
	}

	if (!has_ben)
		sprintf(benchmark_cmd[5], "%s", MBA_STR);
	res = mbm_bw_change(span, cpu_no, bw_report, benchmark_cmd);
	ksft_test_result(!res, "MBM: bw change\n");
	mbm_test_cleanup();
}

static int check_mba4_mode(bool is_mba4)
{
	int ret = -1;
	char *re_str;
	char mba4_status[32];

	FILE *inf = fopen(MBA4_MODE_PATH, "r");

	if (!inf)
		return errno;

	if (is_mba4)
		sprintf(mba4_status, "%s", ENABLED_STR);
	else
		sprintf(mba4_status, "%s", DISABLED_STR);

	re_str = fgrep(inf, mba4_status);
	if (re_str) {
		free(re_str);
		ret = 0;
	}

	fclose(inf);
	return ret;
}

static int check_mba4_msr(bool is_mba4)
{
	int ret = -1;
	int cpu_num;
	uint64_t mba4_extension;

	cpu_num = detect_cpu_num();

	for (int index = 0; index < cpu_num; index++) {
		ret = rdmsr(MSR_IA32_MBA4_EXTENSION_ADDR, index, &mba4_extension);
		if (ret)
			return ret;

		if (!is_mba4 && (mba4_extension & MSR_IA32_MBA4_EXTENSION))
			return ret;
		if (is_mba4 && !(mba4_extension & MSR_IA32_MBA4_EXTENSION))
			return ret;
	}

	return 0;
}

/*
 * detect_mba4:
 * 1. check the file of mba4_mode
 * 2. check MSR(0xC84).
 * @enable: parameters passed to detect_mba4()
 *
 * Return: =0 on success, non-zero on failure.
 */
static int detect_mba4(bool is_mba4)
{
	int ret = -1;

	ret = check_mba4_mode(is_mba4);
	if (ret) {
		perror("the file mba4_mode is not match current mba4 mode!");
		return ret;
	}

	ret = check_mba4_msr(is_mba4);
	if (ret) {
		perror("the value of msr 0xC84 is not match current mba4 mode!");
		return ret;
	}

	return ret;
}

/*
 * @mount_param: parameters of mount
 *
 * Return: =0 on success, non-zero on failure.
 *
 * For mount_param == "mba4",
 * 1. check whether the current kernel supports mba4 option.
 * 2. mount resctrl filesystem with mba4 option.
 * 3. check MSR related to mba4.
 * For mount_param == NULL,
 * 1. mount resctrl filesystem without mba4 option.
 * 2. check MSR related to mba4.
 */
static int mba4_support_test_case(const char *mount_param)
{
	int res = -1, cpu_num;
	bool is_mba4;
	uint64_t  ia32_core_caps;
	int mum_resctrlfs = 1;
	unsigned int op = 0x7, count = 0x0;
	uint32_t eax = 0x0, ebx = 0x0, ecx = 0x0, edx = 0x0;

	if (mount_param && !strncmp(mount_param, "mba4", sizeof("mba4"))) {
		is_mba4 = true;
		res = cpuid(op, count, &eax, &ebx, &ecx, &edx);
		if (res)
			return res;

		if (!(edx & CORE_CAPABILITIES)) {
			ksft_print_msg("CORE_CAPABILITIES is not support!\n");
			return res;
		}

		cpu_num = detect_cpu_num();

		for (int index = 0; index < cpu_num; index++) {
			res = rdmsr(MSR_IA32_CORE_CAPS, index, &ia32_core_caps);
			if (res)
				return res;

			if (!(ia32_core_caps & MSR_IA32_CORE_CAPS_MBA4)) {
				ksft_print_msg("MBA4.0 feature is not support on CPU%d!\n", index);
				return res;
			}
		}
	} else if (!mount_param) {
		is_mba4 = false;
	} else {
		perror("error mount param!");
		return res;
	}

	res = remount_resctrlfs(mum_resctrlfs, mount_param);
	if (res)
		return res;

	res = detect_mba4(is_mba4);

	return res;
}

static void run_mba_test(bool has_ben, char **benchmark_cmd, int span,
			 int cpu_no, char *bw_report)
{
	int res;
	bool is_failed = false;

	ksft_print_msg("Starting MBA Schemata change ...\n");

	if (!validate_resctrl_feature_request(MBA_STR)) {
		ksft_test_result_skip("Hardware does not support MBA or MBA is disabled\n");
		return;
	}

	if (!has_ben)
		sprintf(benchmark_cmd[1], "%d", span);
	res = mba_schemata_change(cpu_no, bw_report, benchmark_cmd);
	is_failed |= res;
	mba_test_cleanup();
	ksft_print_msg("ending mba_schemata_change: %s\n", res ? "failed" : "success");

	/*mount resctrl filesystem with "mba4"*/
	ksft_print_msg("starting mount resctrl filesystem with mba4 ...\n");
	res = mba4_support_test_case("mba4");
	is_failed |= res;
	ksft_print_msg("ending mount resctrl filesystem with mba4: %s\n",
		       res ? "failed" : "success");

	/*mount resctrl filesystem without "mba4"*/
	ksft_print_msg("starting mount resctrl filesystem without mba4 ...\n");
	res = mba4_support_test_case(NULL);
	is_failed |= res;
	ksft_print_msg("ending mount resctrl filesystem without mba4: %s\n",
		       res ? "failed" : "success");

	ksft_test_result(!is_failed, "MBA: test cases.\n");
}

static void run_cmt_test(bool has_ben, char **benchmark_cmd, int cpu_no)
{
	int res;

	ksft_print_msg("Starting CMT test ...\n");
	if (!validate_resctrl_feature_request(CMT_STR)) {
		ksft_test_result_skip("Hardware does not support CMT or CMT is disabled\n");
		return;
	}

	if (!has_ben)
		sprintf(benchmark_cmd[5], "%s", CMT_STR);
	res = cmt_resctrl_val(cpu_no, 5, benchmark_cmd);
	ksft_test_result(!res, "CMT: test\n");
	cmt_test_cleanup();
}

static void run_cat_test(int cpu_no, int no_of_bits)
{
	int res;

	ksft_print_msg("Starting CAT test ...\n");

	if (!validate_resctrl_feature_request(CAT_STR)) {
		ksft_test_result_skip("Hardware does not support CAT or CAT is disabled\n");
		return;
	}

	res = cat_perf_miss_val(cpu_no, no_of_bits, "L3");
	ksft_test_result(!res, "CAT: test\n");
	cat_test_cleanup();
}

int main(int argc, char **argv)
{
	bool has_ben = false, mbm_test = true, mba_test = true, cmt_test = true;
	int c, cpu_no = 1, span = 250, argc_new = argc, i, no_of_bits = 0;
	char *benchmark_cmd[BENCHMARK_ARGS], bw_report[64], bm_type[64];
	char benchmark_cmd_area[BENCHMARK_ARGS][BENCHMARK_ARG_SIZE];
	int ben_ind, ben_count, tests = 0;
	bool cat_test = true;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "-b") == 0) {
			ben_ind = i + 1;
			ben_count = argc - ben_ind;
			argc_new = ben_ind - 1;
			has_ben = true;
			break;
		}
	}

	while ((c = getopt(argc_new, argv, "ht:b:n:p:")) != -1) {
		char *token;

		switch (c) {
		case 't':
			token = strtok(optarg, ",");

			mbm_test = false;
			mba_test = false;
			cmt_test = false;
			cat_test = false;
			while (token) {
				if (!strncmp(token, MBM_STR, sizeof(MBM_STR))) {
					mbm_test = true;
					tests++;
				} else if (!strncmp(token, MBA_STR, sizeof(MBA_STR))) {
					mba_test = true;
					tests++;
				} else if (!strncmp(token, CMT_STR, sizeof(CMT_STR))) {
					cmt_test = true;
					tests++;
				} else if (!strncmp(token, CAT_STR, sizeof(CAT_STR))) {
					cat_test = true;
					tests++;
				} else {
					printf("invalid argument\n");

					return -1;
				}
				token = strtok(NULL, ",");
			}
			break;
		case 'p':
			cpu_no = atoi(optarg);
			break;
		case 'n':
			no_of_bits = atoi(optarg);
			if (no_of_bits <= 0) {
				printf("Bail out! invalid argument for no_of_bits\n");
				return -1;
			}
			break;
		case 'h':
			cmd_help();

			return 0;
		default:
			printf("invalid argument\n");

			return -1;
		}
	}

	ksft_print_header();

	/*
	 * Typically we need root privileges, because:
	 * 1. We write to resctrl FS
	 * 2. We execute perf commands
	 */
	if (geteuid() != 0)
		return ksft_exit_fail_msg("Not running as root, abort testing.\n");

	/* Detect AMD vendor */
	detect_amd();

	if (has_ben) {
		/* Extract benchmark command from command line. */
		for (i = ben_ind; i < argc; i++) {
			benchmark_cmd[i - ben_ind] = benchmark_cmd_area[i];
			sprintf(benchmark_cmd[i - ben_ind], "%s", argv[i]);
		}
		benchmark_cmd[ben_count] = NULL;
	} else {
		/* If no benchmark is given by "-b" argument, use fill_buf. */
		for (i = 0; i < 6; i++)
			benchmark_cmd[i] = benchmark_cmd_area[i];

		strcpy(benchmark_cmd[0], "fill_buf");
		sprintf(benchmark_cmd[1], "%d", span);
		strcpy(benchmark_cmd[2], "1");
		strcpy(benchmark_cmd[3], "1");
		strcpy(benchmark_cmd[4], "0");
		strcpy(benchmark_cmd[5], "");
		benchmark_cmd[6] = NULL;
	}

	sprintf(bw_report, "reads");
	sprintf(bm_type, "fill_buf");

	if (!check_resctrlfs_support())
		return ksft_exit_fail_msg("resctrl FS does not exist\n");

	filter_dmesg();

	ksft_set_plan(tests ? : 4);

	if (!is_amd && mbm_test)
		run_mbm_test(has_ben, benchmark_cmd, span, cpu_no, bw_report);

	if (!is_amd && mba_test)
		run_mba_test(has_ben, benchmark_cmd, span, cpu_no, bw_report);

	if (cmt_test)
		run_cmt_test(has_ben, benchmark_cmd, cpu_no);

	if (cat_test)
		run_cat_test(cpu_no, no_of_bits);

	umount_resctrlfs();

	return ksft_exit_pass();
}
