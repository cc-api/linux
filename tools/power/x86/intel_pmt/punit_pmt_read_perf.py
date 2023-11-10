#!/usr/bin/env python

""" This module read punit telemetry from PMT space """

import re
import os
import sys
import time

os.system('sudo ./perf stat -M pmt -I 5000 -a -o output.txt sleep 5')

out_file = open("output.txt", "r")
dev_blkrs_cnt = {}
dev_blkrs_percent = {}
dev_blkrs_ctgry = {}
dev_blkrs_ctgry_percent = {}
cstate_wakes = {}
cstate_wakes_percent = {}
cstate_residency = {}
cstate_residency_percent = {}
os_requested_cstate_res = {}
os_requested_cstate_res_percent = {}
res_core = {}
res_core_percent = {}

for line in out_file:
    list_list = line.split()
    if "PACKAGE_CSTATE_DEVICE_BLOCK_CAUSE" in line and not "PERCENT" in line:
        name = list_list[2].replace('PACKAGE_CSTATE_DEVICE_BLOCK_CAUSE_', '')
        dev_blkrs_cnt[name] = list_list[1]

    if "PACKAGE_CSTATE_BLOCK_CATEGORY" in line and not "PERCENT" in line:
        name = list_list[2].replace('PACKAGE_CSTATE_BLOCK_CATEGORY_', '')
        dev_blkrs_ctgry[name] = list_list[1]

    if "PACKAGE_CSTATE_WAKE" in line and not "PERCENT" in line:
        name = list_list[2].replace('PACKAGE_CSTATE_WAKE_', '')
        cstate_wakes[name] = list_list[1]
    elif "PACKAGE_CSTATE_WAKE_PUNIT" in line and not "PUNIT_PERCENT" in line:
        name = list_list[2].replace('PACKAGE_CSTATE_WAKE_', '')
        cstate_wakes[name] = list_list[1]

    if "PACKAGE_CSTATE_C" in line and "RESIDENCY" in line and not "PERCENT" in line:
        cstate_residency[list_list[2]] = list_list[1]

    if "OS_REQUESTED_CSTATE" in line and "RESIDENCY" in line:
        os_requested_cstate_res[list_list[2]] = list_list[1]

    if "_RESIDENCY_CORE" in line and not "PERCENT" in line:
        res_core[list_list[2]] = list_list[1]

    if "XTAL" in line:
        xtal = list_list[1]

out_file.seek(0)
for line in out_file:
    list_list = line.split()
    if len(list_list) == 7:
        name_idx = 6
        val_idx = 4
    else:
        name_idx = 4
        val_idx = 2

    if "PACKAGE_CSTATE_DEVICE_BLOCK_CAUSE" in line and "PERCENT" in line:
        name = list_list[name_idx].rstrip("PERCENT")
        name = name.rstrip("_")
        name = name.replace('PACKAGE_CSTATE_DEVICE_BLOCK_CAUSE_', '')
        dev_blkrs_percent[name] = list_list[val_idx]

    if "PACKAGE_CSTATE_BLOCK_CATEGORY" in line and "_PERCENT" in line:
        name = list_list[name_idx].rstrip("PERCENT")
        name = name.rstrip("_")
        name = name.replace('PACKAGE_CSTATE_BLOCK_CATEGORY_', '')
        dev_blkrs_ctgry_percent[name] = list_list[val_idx]
            
    if "PACKAGE_CSTATE_WAKE" in line and "_PERCENT" in line:
        name = list_list[name_idx].rstrip("PERCENT")
        name = name.rstrip("_")
        name = name.replace('PACKAGE_CSTATE_WAKE_', '')
        cstate_wakes_percent[name] = list_list[val_idx]

    if "PACKAGE_CSTATE_C" in line and "_RESIDENCY" in line and "PERCENT" in line:
        name = list_list[name_idx].rstrip("PERCENT")
        name = name.rstrip("_")
        cstate_residency_percent[name] = list_list[val_idx]

    if "OS_REQUESTED_CSTATE" in line and "_RESIDENCY" in line and "PERCENT" in line:
        name = list_list[name_idx].rstrip("PERCENT")
        name = name.rstrip("_")
        os_requested_cstate_res_percent[name] = list_list[val_idx]

    if "_RESIDENCY_CORE" in line and "PERCENT" in line:
        name = list_list[name_idx].rstrip("PERCENT")
        name = name.rstrip("_")
        res_core_percent[name] = list_list[val_idx]

print("\nPackage C-State Device Blocking Cause :")
print("---------------------------------------")
for sample in dev_blkrs_cnt:
    print("{:>20} : {:<10} : {:>0}%".format(sample, dev_blkrs_cnt[sample], dev_blkrs_percent[sample]))

print("\nPackage C-State Block Categories :")
print("----------------------------------")
for sample in dev_blkrs_ctgry:
    print("{:>20} : {:<10} : {:>0}%".format(sample, dev_blkrs_ctgry[sample], dev_blkrs_ctgry_percent[sample]))

print("\nPackage C-State Wake Reasons : ")
print("-------------------------------")
for sample in cstate_wakes:
    print("{:>20} : {:<10} : {:>0}%".format(sample, cstate_wakes[sample], cstate_wakes_percent[sample]))

print("\nPackage C-State Residencies : ")
print("-------------------------------")
for sample in cstate_residency:
    print("{:>24} : {:<20} : {:>0}%".format(sample, cstate_residency[sample], cstate_residency_percent[sample]))

print("\nOS Requested Package C-State Residencies : ")
print("-------------------------------------------")
for sample in os_requested_cstate_res_percent:
    print("{:>24} : {:<20} : {:>0}%".format(sample, os_requested_cstate_res[sample], os_requested_cstate_res_percent[sample]))

print("\nSoC Core C-State Counts")
print("\nOS Requested Package C-State Residencies : ")
print("-------------------------------------------")
for sample in res_core:
    print("{:>20} : {:<20} : {:>0}%".format(sample, res_core[sample], res_core_percent[sample]))

print(f"\n\nCrytal time was {xtal}")
print("-----------------------\n")

