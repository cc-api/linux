// SPDX-License-Identifier: GPL-2.0
/*
 * Memory Bandwidth Allocation (MBA4) test
 *
 * Copyright (C) 2021 Intel Corporation
 *
 * Authors:
 *    You Zhou <you.zhou@intel.com>
 */
#include "resctrl.h"

#define COMPETITION_FILE_NAME   "result_mba4_competition"
#define NOCOMPETITION_FILE_NAME "result_mba4_nocompetition"

void mba4_test_cleanup(void)
{
	if (!access(NOCOMPETITION_FILE_NAME, 0))
		remove(NOCOMPETITION_FILE_NAME);

	if (!access(COMPETITION_FILE_NAME, 0))
		remove(COMPETITION_FILE_NAME);
}

int mba_competition_test(char **benchmark_cmd)
{
	struct resctrl_val_param param = {
		.resctrl_val = MBA4_STR,
		.ctrlgrp = "c0",
		.mongrp = "m0",
		.mum_resctrlfs = 1,
		.filename = COMPETITION_FILE_NAME,
		.allocation = 100,
		.setup = mba4_setup,
		.mount_param = "mba4"
	};
	int ret, cpu_num;

	mba4_test_cleanup();
	/*get cpu num*/
	cpu_num = detect_cpu_num();

	ret = run_mba4(benchmark_cmd, &param, cpu_num, true);
	if (ret)
		goto out;

	ret = check_mba4_results(COMPETITION_FILE_NAME, true);

out:
	mba4_test_cleanup();
	return ret;
}

int mba_nocompetition_test(int cpu_no, char **benchmark_cmd)
{
	struct resctrl_val_param param = {
		.resctrl_val = MBA4_STR,
		.ctrlgrp = "c1",
		.mongrp = "m1",
		.cpu_no = cpu_no,
		.mum_resctrlfs = 1,
		.filename = NOCOMPETITION_FILE_NAME,
		.setup = mba4_setup,
		.allocation = 100,
		.mount_param = "mba4"
	};
	int ret;

	mba4_test_cleanup();

	ret = run_mba4(benchmark_cmd, &param, MBA4_NUM_OF_RUNS, false);
	if (ret)
		goto out;

	ret = check_mba4_results(NOCOMPETITION_FILE_NAME, false);

out:
	mba4_test_cleanup();
	return ret;
}
