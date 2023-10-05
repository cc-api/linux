/*******************************************************************************
* INTEL CONFIDENTIAL
* Copyright 2023 Intel Corporation All Rights Reserved.
*
* The source code contained or described herein and all documents related to
* the source code ("Material") are owned by Intel Corporation or its suppliers
* or licensors. Title to the Material remains with Intel Corporation or its
* suppliers and licensors. The Material may contain trade secrets and proprie-
* tary and confidential information of Intel Corporation and its suppliers and
* licensors, and is protected by worldwide copyright and trade secret laws and
* treaty provisions. No part of the Material may be used, copied, reproduced,
* modified, published, uploaded, posted, transmitted, distributed, or disclos-
* ed in any way without Intel's prior express written permission.
*
* No license under any patent, copyright, trade secret or other intellectual
* property right is granted to or conferred upon you by disclosure or delivery
* of the Materials, either expressly, by implication, inducement, estoppel or
* otherwise. Any license under such intellectual property rights must be ex-
* press and approved by Intel in writing.
*******************************************************************************
*/

// This header was added to fix missing prototypes in svos_intel_idle_hooks_ext.c

/**
    * Enable c1e promotion if enable != 0, otherwise disable.
    */
void __set_c1e_promotion(void * enable);

/**
    * sets the c1e prometion bit in IA32_POWER_CTL MSR.
    * @enable: if enable == 1: allow for c1e cstate promotion, otherwise: only c1 cstate may be entered.
    *
    * C1 and C1E are mutually exclusive cstates. The actual cstate the cpu enters is determined by the IA32_POWER_CTL MSR.
    */
void set_c1e_promotion(int enable);

int is_c1e_promotion_enabled(void);

void reset_intel_idle_driver(void);

