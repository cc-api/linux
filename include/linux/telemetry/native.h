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

// Notes about the API
// ===================
// This API is meant to track with what svobserve is currently doing, which in turn is affected by how to use Lantern
// Rock. This could have been decoupled, but the consequence is moving compile-time errors against the API into runtime
// problems with mismanaging the abstraction.
//
// Since we have to program this in C, making a "futureproof" API requires passing the arguments in as a struct with
// the first field being the API version. There are other variations on this where you pass the size of the struct as
// the first argument. Either way, a version has to be involved. Then the parties consuming the result have to deal
// with older record types along with whatever is a newer record type. So this is something that is very much possible
// to do, I chose instead to eat the problem of having to update out-of-tree software if we mess with the API.

// Example Usage:
// #include <linux/telemetry/native.h>
// #define SANDBOX_ID "00000000-1111-2222-3333-444444444444"
//
// ..
// int hello = 0;
// register_telemetry(SANDBOX_ID, "hello", "1.0.0");
// session_begin(SANDBOX_ID, "startup");
//
// ...
//
// {
//      // Recommend putting buffers in a closure so they only take up space for as long as they are in use.
//      char telemetry_buffer[256];
// 
// 	snprintf(telemetry_buffer,
// 		256,
//      	"{ hello_world: %s }",
//      	hello == 1 ? "true" : "false");
// 		telemetry_msg(SANDBOX_ID, "loaded", telemetry_buffer);
// }
// 
// session_end(SANDBOX_ID);
// unregister_telemetry(SANDBOX_ID);


/**
* Alerts any collecting agent that events should start coming for a specific database. This comes with the additional
* metadata of the "application" and its "version."
* 
* @param telemetry_id The Lantern Rock telemetry ID for the database that should be taking messages.
* @param appname The name of the application. This may just be the name used for registering the database, but multiple
*                applications are allowed to use the same database.
* @param version The application version. Consider using the SVOS build version if you have nothing else.
*/
void register_telemetry(char* telemetry_id, char* appname, char* version);

/**
* Begins a data collection "session." Multiple telemetry_msg calls can be made during one session, and their data will
* be grouped together under that session. Note that even if you intend to only have one message, you still need one session
* for it. The session will be ended with the next session_end call.
* 
* There should be only one session active at any time for a given database.
* 
* The symmetric opposite of session_begin is session_end.
* 
* @param telemetry_id The Lantern Rock telemetry ID for the database that should be taking messages.
* @param session The name of the session.
*/
void session_begin(char* telemetry_id, char* session);

/**
* Reports an actual "event" of data as a message.
* 
* @param telemetry_id The Lantern Rock telemetry ID for the database that should be taking messages.
* @param event_name The name of the event associated with the message. This of the event_name being a key while msg is the value.
* @param msg The message, as a string containing JSON metadata.
*/
void telemetry_msg(char* telemetry_id, char* event_name, char* msg);

/**
* Ends a previously-declared session for a given database based on telemetry ID. This is the symmetric opposite of session_begin.
* The monitoring agent will likely invoke data uploads to the upstream tracker of previous messages as a result of calling
* session_end.
* 
* @param telemetry_id The Lantern Rock telemetry ID for the database that should be taking messages.
*/
void session_end(char* telemetry_id);


/**
* Alerts monitoring agents that sessions and data will stop arriving for a given telemetry database. It helps agents with
* housekeeping and allows it to free up some tracking resources. This is the symmetric opposite of register_telemetry.
* 
* @param telemetry_id The Lantern Rock telemetry ID for the database that should be taking messages.
*/
void unregister_telemetry(char* telemetry_id);
