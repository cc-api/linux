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

#include <linux/kernel.h>
#include <linux/telemetry/native.h>

//#define SVOS_TELEMETRY_DEBUG 0

void register_telemetry(char* telemetry_id, char* appname, char* version)
{
#ifdef SVOS_TELEMETRY_DEBUG
	printk("native register_telemetry: %s %s %s\n", appname, version, telemetry_id);
#endif
}
EXPORT_SYMBOL(register_telemetry);

void telemetry_msg(char* telemetry_id, char* session, char* msg)
{
#ifdef SVOS_TELEMETRY_DEBUG
	printk("native telemetry_msg: %s %s %s\n", telemetry_id, session, msg);
#endif
}
EXPORT_SYMBOL(telemetry_msg);

void session_begin(char* telemetry_id, char* session)
{
#ifdef SVOS_TELEMETRY_DEBUG
	printk("native session_begin: %s %s\n", telemetry_id, session);
#endif
}
EXPORT_SYMBOL(session_begin);

void session_end(char* telemetry_id)
{
#ifdef SVOS_TELEMETRY_DEBUG
	printk("native session_end %s\n", telemetry_id);
#endif
}
EXPORT_SYMBOL(session_end);

void unregister_telemetry(char* telemetry_id)
{
#ifdef SVOS_TELEMETRY_DEBUG
	printk("native unregister_telemetry: %s\n", telemetry_id);
#endif
}
EXPORT_SYMBOL(unregister_telemetry);
