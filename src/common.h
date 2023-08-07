#ifndef BPD_COMMON_H
#define BPD_COMMON_H

#include <pthread.h>

#include <concord/discord.h>

// common stuff

// Fallback in case this isn't set by the build system
#ifndef PROJECT_NAME
	#define PROJECT_NAME "bpd"
#endif // PROJECT_NAME

// Some text to prefix any user-exposed error message with
extern const char ERROR_PREFIX[];

// on_ready, will be populated with the application's ID required by certain calls
extern u64snowflake App_Id;

// rendering-related stuff

// structure to describe a finished rendering job, waiting to be sent to the user
struct llFinishedRender {
	bool success;
	const char* message;
	const char* finishedPath;
	const u64snowflake* channelId;
	volatile struct llFinishedRender* next;
};

// shared linked-list structure that holds finished rendering details
extern volatile struct llFinishedRender* finishedRenders;

// a mutex to make access to the shared LL thread-safe
extern pthread_mutex_t finishedRendersMutex;

#define ARRAY_LENGTH(arr) (sizeof (arr) / sizeof (arr[0]))

#endif // BPD_COMMON_H
