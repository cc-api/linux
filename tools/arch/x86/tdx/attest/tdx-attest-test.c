// SPDX-License-Identifier: GPL-2.0
/*
 * tdx-attest-test.c - Utility to test TDX attestation feature.
 *
 * Copyright (C) 2021 - 2022 Intel Corporation. All rights reserved.
 *
 * Author: Kuppuswamy Sathyanarayanan <sathyanarayanan.kuppuswamy@linux.intel.com>
 *
 */

#include <linux/types.h>
#include <linux/ioctl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>
#include <getopt.h>
#include <stdint.h> /* uintmax_t */
#include <sys/mman.h>
#include <time.h>

#include "../../../../../arch/x86/include/uapi/asm/tdx.h"

#define devname		"/dev/tdx-attest"

#define QUOTE_SIZE		8192

#define ATTESTATION_TEST_BIN_VERSION "0.1"

struct tdx_attest_args {
	bool is_test_tdreport;
	bool is_test_quote;
	char *out_file;
};

static int get_tdreport(int devfd, struct tdx_report_req *req)
{
	int i;

	if (!req)
		return -EINVAL;

	/* Initalize reportdata with random data */
	srand(time(NULL));
	for (i = 0; i < TDX_REPORTDATA_LEN; i++)
		req->reportdata[i] = rand();

	if (ioctl(devfd, TDX_CMD_GET_REPORT, req)) {
		printf("TDX_CMD_GET_TDREPORT ioctl() failed\n");
		return -EIO;
	}

	return 0;
}

/*
 * Test TDX_CMD_GET_TDREPORT IOCTL using random reportdata.
 */
static void test_tdreport(int devfd)
{
	struct tdx_report_req req;
	int ret;

	ret = get_tdreport(devfd, &req);

	printf("TDREPORT generation is %s\n", ret ? "failed" :"successful");
}

void dump_quote_hdr(struct tdx_quote_hdr *hdr)
{
	if (!hdr)
		return;

	printf("Version: %llx \n", hdr->version);
	printf("Status: %llx \n", hdr->status);
	printf("In Len: %d \n", hdr->in_len);
	printf("Out Len: %d \n", hdr->out_len);
}

/*
 * Test GetQuote functionality by sending request to VMM and
 * verifying the return status.
 */
static void test_quote(int devfd)
{
	struct tdx_quote_hdr *quote_hdr;
	struct tdx_report_req *report_req;
	struct tdx_quote_req quote_req;
	__u64 quote_buf_size, err = 0;
	__u8 *quote_buf = NULL;
	long ret;

	/* Add size for quote header */
	quote_buf_size = sizeof(*quote_hdr) + QUOTE_SIZE;

	/* Allocate quote buffer */
	quote_buf = malloc(quote_buf_size);
	if (!quote_buf) {
		printf("%s queue data alloc failed\n", devname);
		ret = -ENOMEM;
		goto done;
	}

	/* Initialize GetQuote header */
	quote_hdr = (struct tdx_quote_hdr *)quote_buf;
	quote_hdr->version = 1;
	quote_hdr->status  = GET_QUOTE_SUCCESS;
	quote_hdr->in_len  = TDX_REPORT_LEN ;
	quote_hdr->out_len = 0;

	dump_quote_hdr(quote_hdr);

	/* Get TDREPORT */
	report_req = (struct tdx_report_req *)&quote_hdr->data;
	ret = get_tdreport(devfd, report_req);
	if (ret)
		goto done;

	/* Fill GetQuote request */
	quote_req.buf	  = (__u64)quote_buf;
	quote_req.len	  = quote_buf_size;

	ret = ioctl(devfd, TDX_CMD_GET_QUOTE, &quote_req);
	if (ret)
		printf("TDX_CMD_GEN_QUOTE ioctl() failed\n");

	/* Make sure GetQuote request is successful */
	if (quote_hdr->status) {
		err = quote_hdr->status;
		ret = -EIO;
	}

done:
	dump_quote_hdr(quote_hdr);
	if (quote_buf)
		free(quote_buf);

	printf("TDX GENQUOTE generation is %s, status:%llx\n",
			ret ? "failed" :"successful", err);

	return;
}

static void usage(void)
{
	puts("\nUsage:\n");
	puts("tdx_attest [options]\n");

	puts("Attestation device test utility.");

	puts("\nOptions:\n");
	puts(" -r, --test-tdreport        Test get TDREPORT");
	puts(" -g, --test-quote           Test generate TDQUOTE");
}

int main(int argc, char **argv)
{
	int ret, devfd;
	struct tdx_attest_args args = {0};

	static const struct option longopts[] = {
		{ "test-tdreport",   required_argument, NULL, 'r' },
		{ "test-getquote",   required_argument, NULL, 'g' },
		{ "version",         no_argument,       NULL, 'V' },
		{ NULL,              0, NULL, 0 }
	};

	while ((ret = getopt_long(argc, argv, "hrgV", longopts,
				  NULL)) != -1) {
		switch (ret) {
		case 'r':
			args.is_test_tdreport = true;
			break;
		case 'g':
			args.is_test_quote = true;
			break;
		case 'h':
			usage();
			return 0;
		case 'V':
			printf("Version: %s\n", ATTESTATION_TEST_BIN_VERSION);
			return 0;
		default:
			printf("Invalid options\n");
			usage();
			return -EINVAL;
		}
	}

	devfd = open(devname, O_RDWR | O_SYNC);
	if (devfd < 0) {
		printf("%s open() failed\n", devname);
		return -ENODEV;
	}

	if (args.is_test_tdreport)
		test_tdreport(devfd);

	if (args.is_test_quote)
		test_quote(devfd);

	close(devfd);

	return 0;
}
